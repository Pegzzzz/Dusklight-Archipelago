#include "ap_client.hpp"

#include <algorithm>
#include <cctype>

namespace randomizer::archi {

using nlohmann::json;

namespace {

constexpr int kDefaultPort = 38281;
constexpr int64_t kStepTimeoutMs = 20000;

std::string Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool IsLocalHost(const std::string& host) {
    const std::string lower = Lower(host);
    return lower == "localhost" || lower == "127.0.0.1" || lower == "::1" || lower == "[::1]";
}

int64_t RetryDelayMs(int attempt) {
    static constexpr int64_t kDelays[] = {2000, 5000, 10000, 20000, 30000};
    return kDelays[std::min<size_t>(attempt, std::size(kDelays) - 1)];
}

}  // namespace

std::vector<std::string> CandidateUrls(std::string_view input) {
    std::string address = Trim(input);
    if (Lower(address.substr(0, 9)) == "/connect ") {
        address = Trim(address.substr(9));
    }
    if (address.empty()) {
        return {};
    }
    const std::string lower = Lower(address);
    if (lower.starts_with("ws://") || lower.starts_with("wss://")) {
        return {address};
    }
    if (lower.starts_with("archipelago://")) {
        address = address.substr(14);
    }
    while (!address.empty() && address.back() == '/') {
        address.pop_back();
    }

    std::string host;
    std::string port;
    if (address.starts_with('[')) {
        const size_t close = address.find(']');
        if (close == std::string::npos) {
            return {};
        }
        host = address.substr(0, close + 1);
        if (close + 1 < address.size() && address[close + 1] == ':') {
            port = address.substr(close + 2);
        }
    } else if (std::ranges::count(address, ':') > 1) {
        host = "[" + address + "]";  // bare IPv6 address
    } else {
        const size_t colon = address.find(':');
        host = address.substr(0, colon);
        if (colon != std::string::npos) {
            port = address.substr(colon + 1);
        }
    }
    if (host.empty() || host.find_first_of(" /?#@") != std::string::npos) {
        return {};
    }
    if (port.empty()) {
        port = std::to_string(kDefaultPort);
    }
    if (!std::ranges::all_of(port, [](unsigned char c) { return std::isdigit(c); })) {
        return {};
    }
    const std::string authority = host + ":" + port;
    if (IsLocalHost(host)) {
        return {"ws://" + authority, "wss://" + authority};
    }
    return {"wss://" + authority, "ws://" + authority};
}

const char* ApStateName(ApState state) {
    switch (state) {
    case ApState::Idle:
        return "Not connected";
    case ApState::Connecting:
        return "Connecting";
    case ApState::Handshaking:
        return "Waiting for the server";
    case ApState::Authenticating:
        return "Joining the room";
    case ApState::Connected:
        return "Connected";
    case ApState::Waiting:
        return "Reconnecting";
    case ApState::Failed:
        return "Not connected";
    }
    return "?";
}

std::string ApMessage::Plain() const {
    std::string text;
    for (const auto& part : parts) {
        text += part.text;
    }
    return text;
}

ApClient::ApClient(TransportFactory factory, ApListener* listener)
    : mFactory{std::move(factory)}, mListener{listener} {}

ApClient::~ApClient() {
    if (mTransport) {
        mTransport->Close();
    }
}

void ApClient::SetState(ApState state, std::string detail) {
    if (state == mState && detail == mDetail) {
        return;
    }
    mState = state;
    mDetail = std::move(detail);
    mStateSinceMs = mNowMs;
    if (mListener) {
        mListener->OnStateChanged(mState, mDetail);
    }
}

void ApClient::Connect(const ApClientConfig& config, int64_t nowMs) {
    Disconnect();
    mNowMs = nowMs;
    mConfig = config;
    mUrls = CandidateUrls(config.address);
    mWorkingUrl.clear();
    mEverConnected = false;
    mStopRetrying = false;
    mRetryCount = 0;
    mLocalChecked.clear();
    mGoal = false;
    // Nothing from a previous room or slot may be taken for this one
    mItems.clear();
    mServerChecked.clear();
    mMissing.clear();
    mPlayers.clear();
    mSlotGames.clear();
    mTeam = 0;
    mSlot = 0;
    mHintPoints = 0;
    mSeedName.clear();
    if (mUrls.empty()) {
        SetState(ApState::Failed, "\"" + config.address + "\" is not a valid server address.");
        return;
    }
    mUrlIndex = 0;
    StartAttempt(nowMs);
}

void ApClient::Disconnect() {
    if (mTransport) {
        mTransport->Close();
        mTransport.reset();
    }
    mPendingPackages.clear();
    mPackageRequestInFlight = false;
    SetState(ApState::Idle);
}

void ApClient::StartAttempt(int64_t nowMs) {
    mNowMs = nowMs;
    while (mUrlIndex < mUrls.size()) {
        const std::string& url = mUrls[mUrlIndex];
        mTransport = mFactory(url);
        if (mTransport) {
            SetState(ApState::Connecting, url);
            return;
        }
        mLastFailure = "cannot open " + url;
        ++mUrlIndex;
    }
    HandleClosed(mLastFailure, nowMs);
}

void ApClient::ScheduleRetry(int64_t nowMs, const std::string& reason) {
    mRetryAtMs = nowMs + RetryDelayMs(mRetryCount++);
    SetState(ApState::Waiting, reason);
}

void ApClient::HandleClosed(const std::string& reason, int64_t nowMs) {
    const bool opened = mState == ApState::Handshaking || mState == ApState::Authenticating ||
                        mState == ApState::Connected;
    const bool wasConnected = mState == ApState::Connected;
    if (mTransport) {
        mTransport->Close();
        mTransport.reset();
    }
    mPendingPackages.clear();
    mPackageRequestInFlight = false;

    if (mStopRetrying) {
        return;  // already Failed with the refusal reason
    }
    if (!opened && mUrlIndex + 1 < mUrls.size()) {
        if (!reason.empty()) {
            mLastFailure = reason;
        }
        ++mUrlIndex;
        StartAttempt(nowMs);
        return;
    }
    const std::string why = reason.empty() ? mLastFailure : reason;
    if (mConfig.autoReconnect) {
        if (wasConnected) {
            mRetryCount = 0;
        }
        ScheduleRetry(nowMs, wasConnected ? "Connection lost: " + why : "Could not connect: " + why);
        return;
    }
    SetState(ApState::Failed, (wasConnected ? "Connection lost: " : "Could not connect to " +
                                                                   mConfig.address + ": ") + why);
}

void ApClient::Tick(int64_t nowMs) {
    mNowMs = nowMs;
    if (mState == ApState::Waiting && nowMs >= mRetryAtMs) {
        // Retry with the address that worked first
        if (!mWorkingUrl.empty()) {
            std::erase(mUrls, mWorkingUrl);
            mUrls.insert(mUrls.begin(), mWorkingUrl);
        }
        mUrlIndex = 0;
        StartAttempt(nowMs);
    }
    if (!mTransport) {
        return;
    }

    std::vector<TransportEvent> events;
    mTransport->Poll(events);
    for (auto& event : events) {
        if (!mTransport) {
            break;  // a callback disconnected
        }
        switch (event.type) {
        case TransportEvent::Type::Open:
            SetState(ApState::Handshaking, mUrls[mUrlIndex]);
            break;
        case TransportEvent::Type::Message:
            HandleText(event.data, nowMs);
            break;
        case TransportEvent::Type::Closed:
            HandleClosed(event.data, nowMs);
            return;  // any remaining events belong to the closed connection
        }
    }

    if (mTransport && mState != ApState::Connected && nowMs - mStateSinceMs > kStepTimeoutMs) {
        HandleClosed(mState == ApState::Connecting ? "timed out" : "the server did not answer", nowMs);
    }
}

void ApClient::Send(const json& commands) {
    if (mTransport) {
        mTransport->Send(commands.dump(-1, ' ', false, json::error_handler_t::replace));
    }
}

void ApClient::HandleText(const std::string& text, int64_t nowMs) {
    json commands;
    try {
        commands = json::parse(text);
    } catch (const std::exception& e) {
        ApMessage message;
        message.type = "ClientError";
        message.parts.push_back({MessagePart::Kind::Text, std::string("Bad message from the server: ") + e.what()});
        if (mListener) {
            mListener->OnMessage(message);
        }
        return;
    }
    if (!commands.is_array()) {
        commands = json::array({commands});
    }
    for (const auto& command : commands) {
        if (!mTransport) {
            return;
        }
        try {
            HandleCommand(command, nowMs);
        } catch (const std::exception& e) {
            ApMessage message;
            message.type = "ClientError";
            message.parts.push_back({MessagePart::Kind::Text,
                "Could not handle " + command.value("cmd", std::string("?")) + ": " + e.what()});
            if (mListener) {
                mListener->OnMessage(message);
            }
        }
    }
}

void ApClient::SendConnect() {
    json connect = {
        {"cmd", "Connect"},
        {"password", mConfig.password},
        {"game", mConfig.game},
        {"name", mConfig.slotName},
        {"uuid", mConfig.uuid},
        {"version", {{"major", mConfig.versionMajor}, {"minor", mConfig.versionMinor},
                        {"build", mConfig.versionBuild}, {"class", "Version"}}},
        {"items_handling", mConfig.itemsHandling},
        {"tags", mConfig.tags},
        {"slot_data", true},
    };
    Send(json::array({connect}));
    SetState(ApState::Authenticating, mUrls[mUrlIndex]);
}

void ApClient::SendPendingChecks() {
    if (mState != ApState::Connected) {
        return;
    }
    std::vector<int64_t> pending;
    for (const int64_t location : mLocalChecked) {
        if (!mServerChecked.contains(location)) {
            pending.push_back(location);
        }
    }
    if (!pending.empty()) {
        Send(json::array({{{"cmd", "LocationChecks"}, {"locations", pending}}}));
    }
}

void ApClient::RequestNextDataPackage() {
    if (mPackageRequestInFlight || mPendingPackages.empty() || !mTransport) {
        return;
    }
    const std::string game = mPendingPackages.back();
    mPendingPackages.pop_back();
    mPackageRequestInFlight = true;
    Send(json::array({{{"cmd", "GetDataPackage"}, {"games", json::array({game})}}}));
}

void ApClient::LoadGameData(const std::string& game, const json& data) {
    auto& items = mItemNames[game];
    auto& locations = mLocationNames[game];
    items.clear();
    locations.clear();
    for (const auto& [name, id] : data.at("item_name_to_id").items()) {
        items[id.get<int64_t>()] = name;
    }
    for (const auto& [name, id] : data.at("location_name_to_id").items()) {
        locations[id.get<int64_t>()] = name;
    }
}

void ApClient::HandleCommand(const json& command, int64_t nowMs) {
    const std::string cmd = command.value("cmd", "");

    if (cmd == "RoomInfo") {
        mSeedName = command.value("seed_name", "");
        mChecksums.clear();
        if (command.contains("datapackage_checksums")) {
            for (const auto& [game, checksum] : command.at("datapackage_checksums").items()) {
                mChecksums[game] = checksum.get<std::string>();
            }
        }
        mPendingPackages.clear();
        for (const auto& [game, checksum] : mChecksums) {
            std::optional<std::string> cached;
            if (mCache.load) {
                cached = mCache.load(game, checksum);
            }
            bool loaded = false;
            if (cached) {
                try {
                    LoadGameData(game, json::parse(*cached));
                    loaded = true;
                } catch (const std::exception&) {
                }
            }
            if (!loaded) {
                mPendingPackages.push_back(game);
            }
        }
        if (mListener) {
            mListener->OnRoomInfo(mSeedName);
        }
        if (mTransport) {
            SendConnect();
        }
        return;
    }

    if (cmd == "ConnectionRefused") {
        std::string reason = "The server refused the connection.";
        for (const auto& error : command.value("errors", json::array())) {
            const std::string code = error.get<std::string>();
            if (code == "InvalidSlot") {
                reason = "This room has no player named \"" + mConfig.slotName + "\".";
            } else if (code == "InvalidGame") {
                reason = "Player \"" + mConfig.slotName + "\" is not playing " + mConfig.game + ".";
            } else if (code == "InvalidPassword") {
                reason = mConfig.password.empty() ? "This room needs a password." : "Wrong password.";
            } else if (code == "IncompatibleVersion") {
                reason = "This server needs a newer version of the mod.";
            } else if (code == "InvalidItemsHandling") {
                reason = "The server rejected the connection settings (InvalidItemsHandling).";
            } else {
                reason = "The server refused the connection (" + code + ").";
            }
        }
        mStopRetrying = true;
        SetState(ApState::Failed, reason);
        if (mTransport) {
            mTransport->Close();
            mTransport.reset();
        }
        return;
    }

    if (cmd == "Connected") {
        mTeam = command.value("team", 0);
        mSlot = command.value("slot", 0);
        mHintPoints = command.value("hint_points", 0);
        mPlayers.clear();
        for (const auto& player : command.value("players", json::array())) {
            PlayerInfo info;
            info.team = player.value("team", 0);
            info.slot = player.value("slot", 0);
            info.alias = player.value("alias", "");
            info.name = player.value("name", "");
            if (info.team == mTeam) {
                mPlayers[info.slot] = info;
            }
        }
        mSlotGames.clear();
        if (command.contains("slot_info")) {
            for (const auto& [slot, info] : command.at("slot_info").items()) {
                const int slotId = std::stoi(slot);
                mSlotGames[slotId] = info.value("game", "");
                if (mPlayers.contains(slotId)) {
                    mPlayers[slotId].game = mSlotGames[slotId];
                } else {
                    // item link groups are not in "players"
                    PlayerInfo group;
                    group.team = mTeam;
                    group.slot = slotId;
                    group.name = info.value("name", "");
                    group.game = mSlotGames[slotId];
                    mPlayers[slotId] = group;
                }
            }
        }
        mServerChecked.clear();
        for (const auto& location : command.value("checked_locations", json::array())) {
            mServerChecked.insert(location.get<int64_t>());
        }
        mMissing.clear();
        for (const auto& location : command.value("missing_locations", json::array())) {
            mMissing.insert(location.get<int64_t>());
        }
        mItems.clear();
        mEverConnected = true;
        mRetryCount = 0;
        mWorkingUrl = mUrls[mUrlIndex];
        SetState(ApState::Connected, mWorkingUrl);
        if (mListener) {
            mListener->OnConnected(command.value("slot_data", json::object()));
        }
        if (mState != ApState::Connected) {
            return;  // the listener disconnected
        }
        SendPendingChecks();
        if (mGoal) {
            Send(json::array({{{"cmd", "StatusUpdate"}, {"status", 30}}}));
        }
        RequestNextDataPackage();
        if (mListener) {
            mListener->OnLocationsChecked();
        }
        return;
    }

    if (cmd == "ReceivedItems") {
        const size_t index = command.value("index", size_t{0});
        std::vector<NetworkItem> received;
        for (const auto& item : command.value("items", json::array())) {
            received.push_back({item.value("item", int64_t{0}), item.value("location", int64_t{0}),
                item.value("player", 0), item.value("flags", 0)});
        }
        if (index == 0) {
            mItems = std::move(received);
        } else if (index == mItems.size()) {
            mItems.insert(mItems.end(), received.begin(), received.end());
        } else {
            // Out of step with the server: ask for everything again
            Send(json::array({{{"cmd", "Sync"}}}));
            return;
        }
        if (mListener) {
            mListener->OnItems(mItems);
        }
        return;
    }

    if (cmd == "RoomUpdate") {
        bool changed = false;
        for (const auto& location : command.value("checked_locations", json::array())) {
            const int64_t id = location.get<int64_t>();
            changed |= mServerChecked.insert(id).second;
            mMissing.erase(id);
        }
        if (command.contains("hint_points")) {
            mHintPoints = command.at("hint_points").get<int>();
        }
        for (const auto& player : command.value("players", json::array())) {
            if (player.value("team", -1) == mTeam) {
                auto& info = mPlayers[player.value("slot", 0)];
                info.alias = player.value("alias", info.alias);
                info.name = player.value("name", info.name);
            }
        }
        if (changed && mListener) {
            mListener->OnLocationsChecked();
        }
        return;
    }

    if (cmd == "DataPackage") {
        mPackageRequestInFlight = false;
        const auto& games = command.at("data").at("games");
        for (const auto& [game, data] : games.items()) {
            LoadGameData(game, data);
            if (mCache.store) {
                const std::string checksum = data.value("checksum", mChecksums[game]);
                mCache.store(game, checksum, data.dump());
            }
        }
        RequestNextDataPackage();
        return;
    }

    if (cmd == "PrintJSON") {
        if (mListener) {
            mListener->OnMessage(ParsePrint(command));
        }
        return;
    }

    if (cmd == "Bounced") {
        const auto tags = command.value("tags", json::array());
        if (std::ranges::find(tags, "DeathLink") != tags.end() && DeathLinkEnabled()) {
            const json data = command.value("data", json::object());
            const double time = data.value("time", 0.0);
            if (time == mLastDeathLinkSent) {
                return;  // our own death coming back
            }
            if (mListener) {
                mListener->OnDeathLink(data.value("source", std::string("Someone")),
                    data.value("cause", std::string()));
            }
        }
        return;
    }

    if (cmd == "InvalidPacket") {
        ApMessage message;
        message.type = "ClientError";
        message.parts.push_back({MessagePart::Kind::Text,
            "The server rejected a message: " + command.value("text", std::string("?"))});
        if (mListener) {
            mListener->OnMessage(message);
        }
        return;
    }
    // LocationInfo, Retrieved, SetReply: not used
}

std::string ApClient::PlayerName(int slot) const {
    if (slot == 0) {
        return "Archipelago";
    }
    const auto it = mPlayers.find(slot);
    if (it == mPlayers.end()) {
        return "Player " + std::to_string(slot);
    }
    return !it->second.alias.empty() ? it->second.alias : it->second.name;
}

std::string ApClient::PlayerGame(int slot) const {
    const auto it = mSlotGames.find(slot);
    return it == mSlotGames.end() ? std::string{} : it->second;
}

std::string ApClient::ItemName(int64_t item, int receivingSlot) const {
    const auto game = mItemNames.find(PlayerGame(receivingSlot));
    if (game != mItemNames.end()) {
        if (const auto it = game->second.find(item); it != game->second.end()) {
            return it->second;
        }
    }
    return "Item " + std::to_string(item);
}

std::string ApClient::LocationName(int64_t location, int findingSlot) const {
    const auto game = mLocationNames.find(PlayerGame(findingSlot));
    if (game != mLocationNames.end()) {
        if (const auto it = game->second.find(location); it != game->second.end()) {
            return it->second;
        }
    }
    if (location == -1) {
        return "Cheat Console";
    }
    if (location == -2) {
        return "Starting Inventory";
    }
    return "Location " + std::to_string(location);
}

ApMessage ApClient::ParsePrint(const json& command) const {
    ApMessage message;
    message.type = command.value("type", "");
    message.receiving = command.value("receiving", 0);
    message.found = command.value("found", false);
    if (command.contains("item") && command.at("item").is_object()) {
        const auto& item = command.at("item");
        message.item = NetworkItem{item.value("item", int64_t{0}), item.value("location", int64_t{0}),
            item.value("player", 0), item.value("flags", 0)};
    }
    for (const auto& part : command.value("data", json::array())) {
        const std::string type = part.value("type", "text");
        const std::string text = part.value("text", "");
        MessagePart out;
        if (type == "player_id") {
            const int slot = std::atoi(text.c_str());
            out.kind = slot == mSlot ? MessagePart::Kind::OwnPlayer : MessagePart::Kind::Player;
            out.text = PlayerName(slot);
        } else if (type == "player_name") {
            out.kind = MessagePart::Kind::Player;
            out.text = text;
        } else if (type == "item_id") {
            out.kind = MessagePart::Kind::Item;
            out.text = ItemName(std::atoll(text.c_str()), part.value("player", 0));
            out.itemFlags = part.value("flags", 0);
        } else if (type == "item_name") {
            out.kind = MessagePart::Kind::Item;
            out.text = text;
            out.itemFlags = part.value("flags", 0);
        } else if (type == "location_id") {
            out.kind = MessagePart::Kind::Location;
            out.text = LocationName(std::atoll(text.c_str()), part.value("player", 0));
        } else if (type == "location_name") {
            out.kind = MessagePart::Kind::Location;
            out.text = text;
        } else if (type == "entrance_name") {
            out.kind = MessagePart::Kind::Entrance;
            out.text = text;
        } else if (type == "color") {
            out.kind = MessagePart::Kind::Color;
            out.text = text;
            out.color = part.value("color", "");
        } else {
            out.text = text;
        }
        message.parts.push_back(std::move(out));
    }
    return message;
}

void ApClient::CheckLocations(const std::vector<int64_t>& locations) {
    std::vector<int64_t> fresh;
    for (const int64_t location : locations) {
        if (mLocalChecked.insert(location).second && !mServerChecked.contains(location)) {
            fresh.push_back(location);
        }
    }
    if (!fresh.empty() && mState == ApState::Connected) {
        Send(json::array({{{"cmd", "LocationChecks"}, {"locations", fresh}}}));
    }
}

void ApClient::SetGoalReached() {
    if (mGoal) {
        return;
    }
    mGoal = true;
    if (mState == ApState::Connected) {
        Send(json::array({{{"cmd", "StatusUpdate"}, {"status", 30}}}));
    }
}

bool ApClient::DeathLinkEnabled() const {
    return std::ranges::find(mConfig.tags, "DeathLink") != mConfig.tags.end();
}

void ApClient::SetDeathLink(bool enabled) {
    if (enabled == DeathLinkEnabled()) {
        return;
    }
    if (enabled) {
        mConfig.tags.push_back("DeathLink");
    } else {
        std::erase(mConfig.tags, "DeathLink");
    }
    if (mState == ApState::Connected) {
        Send(json::array({{{"cmd", "ConnectUpdate"}, {"tags", mConfig.tags}}}));
    }
}

void ApClient::SendDeathLink(const std::string& cause, double nowSeconds) {
    if (mState != ApState::Connected || !DeathLinkEnabled()) {
        return;
    }
    mLastDeathLinkSent = nowSeconds;
    json data = {{"time", nowSeconds}, {"source", mConfig.slotName}};
    if (!cause.empty()) {
        data["cause"] = cause;
    }
    Send(json::array({{{"cmd", "Bounce"}, {"tags", json::array({"DeathLink"})}, {"data", data}}}));
}

void ApClient::Say(const std::string& text) {
    if (mState == ApState::Connected && !text.empty()) {
        Send(json::array({{{"cmd", "Say"}, {"text", text}}}));
    }
}

}  // namespace randomizer::archi
