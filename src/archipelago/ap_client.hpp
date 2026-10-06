#pragma once

// Archipelago network protocol client.
//
// Pure C++ (no Dusklight services): the connection itself is a Transport created by a factory,
// so the same client runs in the game (Dusklight WebSocket/net services) and in the host test
// harness (POSIX sockets). Everything happens in Tick(), on the caller's thread; results come
// back through the ApListener callbacks from inside Tick().
//
// Protocol reference: https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/network%20protocol.md

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace randomizer::archi {

// ---------------------------------------------------------------------------------------------
// Transport

struct TransportEvent {
    enum class Type { Open, Message, Closed };
    Type type;
    std::string data;  // message text, or the reason a connection closed or failed
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void Send(const std::string& text) = 0;
    virtual void Close() = 0;
    /// Appends everything that happened since the last call.
    virtual void Poll(std::vector<TransportEvent>& out) = 0;
};

/// Creates a transport for a ws:// or wss:// URL, or returns nullptr if it can't be reached
/// that way (the client then tries the next candidate URL).
using TransportFactory = std::function<std::unique_ptr<Transport>(const std::string& url)>;

/// The URLs to try for what a player typed: "archipelago.gg:38281", "localhost", "wss://..."
std::vector<std::string> CandidateUrls(std::string_view address);

// ---------------------------------------------------------------------------------------------
// Protocol data

inline constexpr int kItemsHandlingOtherWorlds = 0b001;
inline constexpr int kItemsHandlingOwnWorld = 0b010;
inline constexpr int kItemsHandlingStartInventory = 0b100;

enum NetworkItemFlags : int {
    kNetItemProgression = 0b001,
    kNetItemUseful = 0b010,
    kNetItemTrap = 0b100,
};

struct NetworkItem {
    int64_t item = 0;
    int64_t location = 0;  // -1 cheat, -2 starting inventory
    int player = 0;        // the player the item came from (the finder)
    int flags = 0;
};

struct PlayerInfo {
    int team = 0;
    int slot = 0;
    std::string alias;
    std::string name;
    std::string game;
};

/// One piece of a server text message (PrintJSON), already resolved to display text.
struct MessagePart {
    enum class Kind { Text, Player, OwnPlayer, Item, Location, Entrance, Color };
    Kind kind = Kind::Text;
    std::string text;
    int itemFlags = 0;   // Kind::Item
    std::string color;   // Kind::Color
};

struct ApMessage {
    std::string type;  // ItemSend, Hint, Chat, Join, ...
    std::vector<MessagePart> parts;
    // ItemSend/ItemCheat/Hint details
    std::optional<NetworkItem> item;
    int receiving = 0;
    bool found = false;

    std::string Plain() const;
};

enum class ApState {
    Idle,           // not connected and not trying
    Connecting,     // opening a connection
    Handshaking,    // connected, waiting for RoomInfo
    Authenticating, // Connect sent
    Connected,
    Waiting,        // connection lost; retrying after a delay
    Failed,         // gave up (refused, or could not connect and not retrying)
};

const char* ApStateName(ApState state);

struct ApClientConfig {
    std::string address;
    std::string slotName;
    std::string password;
    std::string game;
    std::string uuid;
    int itemsHandling = kItemsHandlingOtherWorlds | kItemsHandlingStartInventory;
    std::vector<std::string> tags;
    /// Keep retrying after the connection drops (in game). The first connection from the
    /// new-save screen reports failures instead.
    bool autoReconnect = false;
    int versionMajor = 0, versionMinor = 6, versionBuild = 4;
};

class ApListener {
public:
    virtual ~ApListener() = default;
    virtual void OnStateChanged(ApState, const std::string& /*detail*/) {}
    /// Room seed name, from RoomInfo (before Connected).
    virtual void OnRoomInfo(const std::string& /*seedName*/) {}
    virtual void OnConnected(const nlohmann::json& /*slotData*/) {}
    /// Every item the server has sent this connection, in order. Called when the list grows or
    /// is resent; the receiver keeps its own count of what it already applied.
    virtual void OnItems(const std::vector<NetworkItem>& /*items*/) {}
    virtual void OnLocationsChecked() {}
    virtual void OnMessage(const ApMessage&) {}
    virtual void OnDeathLink(const std::string& /*source*/, const std::string& /*cause*/) {}
};

/// Lets the client keep other games' item/location names between sessions.
struct DataPackageCache {
    std::function<std::optional<std::string>(const std::string& game, const std::string& checksum)> load;
    std::function<void(const std::string& game, const std::string& checksum, const std::string& json)> store;
};

class ApClient {
public:
    ApClient(TransportFactory factory, ApListener* listener);
    ~ApClient();

    void SetDataPackageCache(DataPackageCache cache) { mCache = std::move(cache); }
    void SetAutoReconnect(bool enabled) { mConfig.autoReconnect = enabled; }

    void Connect(const ApClientConfig& config, int64_t nowMs);
    void Disconnect();
    void Tick(int64_t nowMs);

    ApState State() const { return mState; }
    const std::string& Detail() const { return mDetail; }
    const ApClientConfig& Config() const { return mConfig; }
    bool IsConnected() const { return mState == ApState::Connected; }

    // Session information (valid once connected)
    int Team() const { return mTeam; }
    int Slot() const { return mSlot; }
    const std::string& SeedName() const { return mSeedName; }
    const std::vector<NetworkItem>& Items() const { return mItems; }
    const std::set<int64_t>& ServerCheckedLocations() const { return mServerChecked; }
    const std::set<int64_t>& MissingLocations() const { return mMissing; }
    std::string PlayerName(int slot) const;
    std::string PlayerGame(int slot) const;
    std::string ItemName(int64_t item, int receivingSlot) const;
    std::string LocationName(int64_t location, int findingSlot) const;
    int HintPoints() const { return mHintPoints; }

    // Requests
    void CheckLocations(const std::vector<int64_t>& locations);
    void SetGoalReached();
    void SetDeathLink(bool enabled);
    bool DeathLinkEnabled() const;
    void SendDeathLink(const std::string& cause, double nowSeconds);
    void Say(const std::string& text);

private:
    void StartAttempt(int64_t nowMs);
    void HandleClosed(const std::string& reason, int64_t nowMs);
    void HandleText(const std::string& text, int64_t nowMs);
    void HandleCommand(const nlohmann::json& command, int64_t nowMs);
    void SetState(ApState state, std::string detail = {});
    void Send(const nlohmann::json& commands);
    void SendConnect();
    void SendPendingChecks();
    void RequestNextDataPackage();
    void LoadGameData(const std::string& game, const nlohmann::json& data);
    ApMessage ParsePrint(const nlohmann::json& command) const;
    void ScheduleRetry(int64_t nowMs, const std::string& reason);

    TransportFactory mFactory;
    ApListener* mListener;
    DataPackageCache mCache;
    ApClientConfig mConfig;
    std::unique_ptr<Transport> mTransport;
    ApState mState = ApState::Idle;
    std::string mDetail;

    std::vector<std::string> mUrls;
    size_t mUrlIndex = 0;
    std::string mWorkingUrl;
    std::string mLastFailure;
    bool mEverConnected = false;
    bool mStopRetrying = false;
    int mRetryCount = 0;
    int64_t mRetryAtMs = 0;
    int64_t mStateSinceMs = 0;
    int64_t mNowMs = 0;

    // Session
    int mTeam = 0;
    int mSlot = 0;
    int mHintPoints = 0;
    std::string mSeedName;
    std::map<std::string, std::string> mChecksums;
    std::vector<std::string> mPendingPackages;
    bool mPackageRequestInFlight = false;
    std::unordered_map<int, PlayerInfo> mPlayers;  // by slot, own team
    std::unordered_map<int, std::string> mSlotGames;
    std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> mItemNames;
    std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> mLocationNames;
    std::vector<NetworkItem> mItems;
    std::set<int64_t> mServerChecked;
    std::set<int64_t> mMissing;
    std::set<int64_t> mLocalChecked;
    bool mGoal = false;
    double mLastDeathLinkSent = -1;
};

}  // namespace randomizer::archi
