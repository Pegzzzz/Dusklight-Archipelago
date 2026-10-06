#include "ap_game.hpp"

#include "ap_locations.hpp"
#include "ap_seed.hpp"
#include "ap_transport.hpp"
#include "ap_ui.hpp"

#include "../item_ids.h"
#include "../paths.hpp"
#include "../randomizer_context.hpp"
#include "../session.hpp"
#include "../stages.h"
#include "../tools.h"
#include "../verify_item_functions.h"
#include "../../generator/randomizer.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_b_gnd.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2_info.h"
#include "d/d_msg_object.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"

#include <mods/items.h>
#include <mods/svc/config.h>
#include <mods/svc/flow.hpp>
#include <mods/svc/log.hpp>
#include <mods/svc/ui.h>

#include <fmt/format.h>

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifndef ARCHIPELAGO_MOD_VERSION
#define ARCHIPELAGO_MOD_VERSION "unknown"
#endif

namespace randomizer {
// generator/utility/text.cpp
std::string UTF8ToShiftJIS(const std::string& utf8Str);
std::string UTF8ToCP1252(const std::string& utf8Str);
void breakLines(std::string& str, float maxStrLength, int lang);
void applyMessageCodes(std::string& str);
}

namespace randomizer::archi::game {
namespace {

using nlohmann::json;
using session::svc_mng;

constexpr const char* kGame = "Twilight Princess Dusklight";
constexpr const char* kStateBlobName = "archipelago";
constexpr const char* kReceivePrefix = "ap:recv:";
constexpr uint16_t kForeignItemMessage = 0xDC + 0x65;  // get-item text of item 0xDC
constexpr int kScanInterval = 10;                      // frames between flag scans
constexpr size_t kMaxLogLines = 200;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

double UnixSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------------------------
// Files

std::filesystem::path ArchipelagoDir() {
    return paths::GetRandomizerPath() / "archipelago";
}

std::string SafeFileName(const std::string& text);

std::filesystem::path SeedWorkDir(const std::string& seed) {
    return ArchipelagoDir() / "seeds" / SafeFileName(seed.empty() ? std::string{"unknown"} : seed);
}

std::filesystem::path SlotDataPath(const std::string& seed) {
    return SeedWorkDir(seed) / "slot_data.json";
}

// The game ends with the credits and no save after Ganondorf, so the goal is also remembered
// here until the room has been told.
std::filesystem::path GoalMarkerPath(const std::string& seed) {
    return SeedWorkDir(seed) / "goal_complete";
}

bool WriteText(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

std::optional<std::string> ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string SafeFileName(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        out += std::isalnum(c) || c == '-' || c == '_' ? static_cast<char>(c) : '_';
    }
    return out.substr(0, 80);
}

// ---------------------------------------------------------------------------------------------
// Config

ConfigVarHandle s_cfgServer{};
ConfigVarHandle s_cfgSlot{};
ConfigVarHandle s_cfgUuid{};
ConfigVarHandle s_cfgToasts{};

std::string GetConfigString(ConfigVarHandle var) {
    size_t length = 0;
    if (var == 0 || svc_mng.config->get_string(mod_ctx, var, nullptr, 0, &length) != MOD_OK) {
        return {};
    }
    std::string value(length + 1, '\0');
    if (svc_mng.config->get_string(mod_ctx, var, value.data(), value.size(), &length) != MOD_OK) {
        return {};
    }
    value.resize(length);
    return value;
}

void RegisterConfig() {
    auto registerString = [](const char* name, ConfigVarHandle& out) {
        ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
        desc.name = name;
        desc.type = CONFIG_VAR_STRING;
        svc_mng.config->register_var(mod_ctx, &desc, &out);
    };
    registerString("ap_last_server", s_cfgServer);
    registerString("ap_last_slot", s_cfgSlot);
    registerString("ap_client_uuid", s_cfgUuid);
    ConfigVarDesc toasts = CONFIG_VAR_DESC_INIT;
    toasts.name = "ap_notifications";
    toasts.type = CONFIG_VAR_BOOL;
    toasts.default_bool = true;
    svc_mng.config->register_var(mod_ctx, &toasts, &s_cfgToasts);

    if (GetConfigString(s_cfgUuid).empty()) {
        std::mt19937_64 random{std::random_device{}()};
        svc_mng.config->set_string(mod_ctx, s_cfgUuid, fmt::format("{:016x}", random()).c_str());
    }
}

// ---------------------------------------------------------------------------------------------
// Items and locations of the active save

struct ItemInfo {
    std::string name;
};

const std::unordered_map<uint8_t, std::string>& ItemNames() {
    static const std::unordered_map<uint8_t, std::string> names = [] {
        std::unordered_map<uint8_t, std::string> out;
        auto tree = LOAD_EMBED_YAML(RANDO_DATA_PATH "items.yaml");
        for (const auto& node : tree) {
            out[static_cast<uint8_t>(node["Id"].as<int>())] = node["Name"].as<std::string>();
        }
        return out;
    }();
    return names;
}

struct SaveState {
    ConnectionForm connection;
    std::string seed;        // slot seed ("AP...") the save was created for
    std::string seedHash;    // randomizer seed hash
    size_t received = 0;     // items from the server applied to this save
    bool deathLink = false;
    bool goal = false;
};

struct Runtime {
    SaveState save;
    bool active = false;  // a save of this game mode is loaded
    std::optional<SlotData> slot;
    LocationIndex index;
    std::set<int64_t> checked;
    size_t queuedItems = 0;      // server items handed to the give queue (index)
    std::set<size_t> doneItems;  // completed gives not yet contiguous with save.received
    int scanTimer = 0;
    bool wasDead = false;
    bool deathLinkKill = false;  // Link's life was set to 0 by a DeathLink; his death is not sent back
    bool goalSent = false;
    // Checks resolved to another player's item, for the get-item text (see ForeignLocationShown)
    struct ForeignNote {
        const ApLocation* location;
        uint64_t frame;
    };
    std::deque<ForeignNote> foreignNotes;
    std::unordered_map<uint32_t, const ApLocation*> foreignByTag;  // item give tag -> location
    std::unordered_map<const ApLocation*, std::array<std::string, 7>> foreignTexts;  // per language
    bool messageChecked = false;
    // Received items announced as "N items received" instead of one by one
    std::vector<std::string> pendingReceived;
};

Runtime s_rt;
uint64_t s_frame = 0;

std::unique_ptr<ApClient> s_client;
// The slot seed the client's current item list was checked against (see OnConnected): items are
// only given to a save of that seed.
std::string s_verifiedSeed;
// The save state as it was when the game copied its data for writing (see OnSaveCaptured)
std::optional<std::string> s_capturedState;
std::deque<std::string> s_log;
uint64_t s_logVersion = 0;
std::vector<mods::flow::MessageOverride> s_messageOverrides;
// Set when the game has no native message 321 to override (see CheckForeignMessage)
bool s_foreignMessageMissing = false;
mods::flow::RegisteredMessage s_fallbackMessage;

// ---------------------------------------------------------------------------------------------
// Notifications

bool NotificationsOn() {
    bool value = true;
    if (s_cfgToasts != 0) {
        svc_mng.config->get_bool(mod_ctx, s_cfgToasts, &value);
    }
    return value;
}

void Toast(const std::string& titleRml, const std::string& bodyRml, const char* type = nullptr,
    uint32_t durationMs = 0) {
    if (!NotificationsOn()) {
        return;
    }
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.type = type;
    desc.title_rml = titleRml.empty() ? nullptr : titleRml.c_str();
    desc.body_rml = bodyRml.empty() ? nullptr : bodyRml.c_str();
    desc.duration_ms = durationMs;
    svc_mng.ui->push_toast(mod_ctx, &desc);
}

std::string Colored(const std::string& text, const char* color) {
    return fmt::format("<span style=\"color: {};\">{}</span>", color, EscapeRml(text));
}

const char* ItemColor(int flags) {
    if (flags & kNetItemProgression) {
        return "#af99ef";
    }
    if (flags & kNetItemUseful) {
        return "#6d8be8";
    }
    if (flags & kNetItemTrap) {
        return "#fa8072";
    }
    return "#00eeee";
}

void AddLog(const std::string& rml) {
    s_log.push_back(rml);
    while (s_log.size() > kMaxLogLines) {
        s_log.pop_front();
    }
    ++s_logVersion;
}

std::string MessageRml(const ApMessage& message) {
    std::string rml;
    for (const auto& part : message.parts) {
        switch (part.kind) {
        case MessagePart::Kind::OwnPlayer:
            rml += Colored(part.text, "#ee00ee");
            break;
        case MessagePart::Kind::Player:
            rml += Colored(part.text, "#fafad2");
            break;
        case MessagePart::Kind::Item:
            rml += Colored(part.text, ItemColor(part.itemFlags));
            break;
        case MessagePart::Kind::Location:
            rml += Colored(part.text, "#00ff7f");
            break;
        case MessagePart::Kind::Entrance:
            rml += Colored(part.text, "#6495ed");
            break;
        default:
            rml += EscapeRml(part.text);
            break;
        }
    }
    return rml;
}

// ---------------------------------------------------------------------------------------------
// Location checks

// The save's own flags only: the tracker's temporary flags (tracker_is*) can outlive a reload
bool IsStageItem(int stage, int flag) {
    if (dComIfGp_getStageStagInfo() && stage == dStage_stagInfo_GetSaveTbl(dComIfGp_getStageStagInfo())) {
        return dComIfGs_isItem(flag, -1);
    }
    // isItem on the save table: the bit number without the MEMORY_ITEM offset (as tools.cpp)
    return g_dComIfG_gameInfo.info.getSavedata().getSave(stage).getBit().isItem(flag - 0x80);
}

bool IsObtained(const TrackedLocation& location) {
    switch (location.kind) {
    case FlagKind::Tbox:
        return dComIfGs_isStageTbox(location.stage, location.flag);
    case FlagKind::Switch:
        return dComIfGs_isStageSwitch(location.stage, location.flag);
    case FlagKind::Item:
        return IsStageItem(location.stage, location.flag);
    case FlagKind::Event:
        return dComIfGs_isEventBit(location.flag);
    case FlagKind::None:
        break;
    }
    return false;
}

void MarkChecked(const std::vector<int64_t>& ids) {
    std::vector<int64_t> fresh;
    for (const int64_t id : ids) {
        if (s_rt.checked.insert(id).second) {
            fresh.push_back(id);
        }
    }
    if (!fresh.empty() && s_client) {
        s_client->CheckLocations(fresh);
    }
}

std::vector<int64_t> ScanFlags() {
    std::vector<int64_t> found;
    for (const auto& location : s_rt.index.Locations()) {
        if (location.ap != nullptr && !s_rt.checked.contains(location.ap->id) && IsObtained(location)) {
            found.push_back(location.ap->id);
        }
    }
    return found;
}

// ---------------------------------------------------------------------------------------------
// Save blob

json SaveToJson(const SaveState& save) {
    return {
        {"version", 1},
        {"server", save.connection.server},
        {"slot", save.connection.slot},
        {"password", save.connection.password},
        {"seed", save.seed},
        {"seed_hash", save.seedHash},
        {"received", save.received},
        {"death_link", save.deathLink},
        {"goal", save.goal},
    };
}

std::optional<SaveState> ReadSaveBlob() {
    size_t size = 0;
    if (svc_mng.save->get_blob(mod_ctx, kStateBlobName, nullptr, &size) != MOD_OK || size == 0) {
        return std::nullopt;
    }
    std::string text(size, '\0');
    if (svc_mng.save->get_blob(mod_ctx, kStateBlobName, text.data(), &size) != MOD_OK) {
        return std::nullopt;
    }
    try {
        const json data = json::parse(text);
        SaveState save;
        save.connection.server = data.value("server", "");
        save.connection.slot = data.value("slot", "");
        save.connection.password = data.value("password", "");
        save.seed = data.value("seed", "");
        save.seedHash = data.value("seed_hash", "");
        save.received = data.value("received", size_t{0});
        save.deathLink = data.value("death_link", false);
        save.goal = data.value("goal", false);
        return save;
    } catch (const std::exception& e) {
        mods::log::error("Archipelago: unreadable save state: {}", e.what());
        return std::nullopt;
    }
}

void WriteSaveBlob() {
    if (!s_rt.active) {
        return;
    }
    const std::string text = SaveToJson(s_rt.save).dump();
    svc_mng.save->set_blob(mod_ctx, kStateBlobName, text.data(), text.size());
}

// ---------------------------------------------------------------------------------------------
// Get-item text for other players' items

// The location whose item the get-item box being shown is about. Checks can be resolved early
// (actors preview their item when they spawn) and several at once (the Master Sword pedestal
// gives two), so: the item actor's give tag when it was seen, otherwise the earliest check
// resolved in the most recent frame that had any, among those not given yet.
const ApLocation* ForeignLocationShown() {
    if (const auto* partner = static_cast<const fopAc_ac_c*>(dComIfGp_event_getItemPartner());
        partner != nullptr && partner->mItemGiveTag != 0)
    {
        if (const auto it = s_rt.foreignByTag.find(partner->mItemGiveTag); it != s_rt.foreignByTag.end()) {
            return it->second;
        }
    }
    if (s_rt.foreignNotes.empty()) {
        return nullptr;
    }
    const uint64_t latest = s_rt.foreignNotes.back().frame;
    for (const auto& note : s_rt.foreignNotes) {
        if (note.frame == latest) {
            return note.location;
        }
    }
    return nullptr;
}

std::string GameTextFromUtf8(const std::string& text, int language) {
    try {
        return language == MESSAGE_LANGUAGE_JAPANESE ? UTF8ToShiftJIS(text) : UTF8ToCP1252(text);
    } catch (const std::exception&) {
        return text;  // the templates are valid; player names were already made ASCII
    }
}

// "You found Bob's Moon Pearl!", laid out like the randomizer's own get-item texts
std::string ForeignGetText(const ApLocation* location, int language) {
    if (location == nullptr) {
        switch (language) {
        case MESSAGE_LANGUAGE_GERMAN:
            return GameTextFromUtf8("<fast>Du hast einen Gegenstand f\u00fcr\neine andere Welt gefunden!", language);
        case MESSAGE_LANGUAGE_FRENCH:
            return GameTextFromUtf8("<fast>Vous avez trouv\u00e9 un objet\npour un autre monde !", language);
        case MESSAGE_LANGUAGE_SPANISH:
            return GameTextFromUtf8("<fast>\u00a1Has encontrado un objeto\npara otro mundo!", language);
        case MESSAGE_LANGUAGE_ITALIAN:
            return GameTextFromUtf8("<fast>Hai trovato un oggetto\nper un altro mondo!", language);
        case MESSAGE_LANGUAGE_JAPANESE:
            return GameTextFromUtf8("<fast>\u5225\u306e\u4e16\u754c\u306e\u30a2\u30a4\u30c6\u30e0\u3092\n\u898b\u3064\u3051\u305f\uff01", language);
        default:
            return "<fast>You found an item for\nanother world!";
        }
    }
    const char* itemColor = "<green>";  // filler
    if (location->flags & kItemProgression) {
        itemColor = "<purple>";
    } else if (location->flags & kItemUseful) {
        itemColor = "<light blue>";
    } else if (location->flags & kItemTrap) {
        itemColor = "<red>";
    }
    const std::string owner = "<yellow>" + SanitizeForGameText(location->owner, 16) + "<white>";
    const std::string item = std::string{itemColor} + SanitizeForGameText(location->item, 48) + "<white>";
    std::string text;
    switch (language) {
    case MESSAGE_LANGUAGE_GERMAN:
        text = "<fast>Du hast " + item + " f\u00fcr " + owner + " gefunden!";
        break;
    case MESSAGE_LANGUAGE_FRENCH:
        text = "<fast>Vous avez trouv\u00e9 " + item + " pour " + owner + " !";
        break;
    case MESSAGE_LANGUAGE_SPANISH:
        text = "<fast>\u00a1Has encontrado " + item + " para " + owner + "!";
        break;
    case MESSAGE_LANGUAGE_ITALIAN:
        text = "<fast>Hai trovato " + item + " per " + owner + "!";
        break;
    case MESSAGE_LANGUAGE_JAPANESE:
        text = "<fast>" + owner + "\u306e" + item + "\u3092\u898b\u3064\u3051\u305f\uff01";
        break;
    default:
        text = "<fast>You found " + owner + "'s " + item + "!";
        break;
    }
    return GameTextFromUtf8(text, language);
}

// Final game text (control codes applied, lines broken with the game's font like the
// randomizer's texts), cached per location and language
const std::string& ForeignMessageText(const ApLocation* location, int language) {
    static std::array<std::string, 7> s_generic;
    const size_t slot = static_cast<size_t>(language) < 7 ? static_cast<size_t>(language) : 0;
    std::string& cached = location != nullptr ? s_rt.foreignTexts[location][slot] : s_generic[slot];
    if (cached.empty()) {
        std::string text = ForeignGetText(location, language);
        breakLines(text, 14.0f, language);  // Text::MAX_LINE_WIDTH_ITEM_TEXTBOX
        applyMessageCodes(text);
        cached = std::move(text);
    }
    return cached;
}

bool ForeignItemMessage(
    ModContext*, const MessageOverrideContext* message, MessageTextData* outText, void*) {
    if (message == nullptr || outText == nullptr || !s_rt.active) {
        return false;
    }
    thread_local std::vector<uint8_t> buffer;
    const std::string& text = ForeignMessageText(ForeignLocationShown(), message->language);
    buffer.assign(text.begin(), text.end());
    buffer.push_back(0);
    outText->text = buffer.data();
    outText->text_size = buffer.size();
    return true;
}

void RegisterMessageOverrides() {
    s_messageOverrides.clear();
    for (const auto language : {MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN, MESSAGE_LANGUAGE_FRENCH,
             MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN, MESSAGE_LANGUAGE_JAPANESE})
    {
        auto handle = mods::flow::override_message_fn(0, kForeignItemMessage, language, ForeignItemMessage);
        if (!handle) {
            mods::log::warn("Archipelago: could not override get-item message {} ({})", kForeignItemMessage,
                static_cast<int>(handle.result()));
        }
        s_messageOverrides.push_back(std::move(handle));
    }
}

// The get-item text of item 0xDC is message 321, which is overridden above. Dusklight only
// overrides messages the game has; the unused items around 0xDC all have one (the randomizer
// overrides 317-320 and 325-335), so 321 should too. Checked once in game, and if it is missing
// the get-item box uses a registered message instead (ForeignFallbackMessageId).
void CheckForeignMessage() {
    auto* messages = dComIfGp_getMsgObjectClass();
    if (messages == nullptr || dMeter2Info_getMsgResource() == nullptr) {
        return;
    }
    s_rt.messageChecked = true;
    const u32 index = messages->getMessageIndexAlways(kForeignItemMessage);
    s_foreignMessageMissing = messages->getMessageIDAlways(index) != kForeignItemMessage;
    if (s_foreignMessageMissing) {
        mods::log::error("Archipelago: the game has no message {}; using a registered message for other "
                         "players' items", kForeignItemMessage);
    }
}

// ---------------------------------------------------------------------------------------------
// Network listener

class Listener final : public ApListener {
public:
    void OnStateChanged(ApState state, const std::string& detail) override;
    void OnConnected(const json& slotData) override;
    void OnMessage(const ApMessage& message) override;
    void OnDeathLink(const std::string& source, const std::string& cause) override;
};

Listener s_listener;

// New save flow
ConnectionForm s_newForm;
NewSavePhase s_newPhase = NewSavePhase::Idle;
std::string s_newMessage;
std::string s_pendingHash;
std::optional<SlotData> s_newSlot;
std::string s_newSlotJson;

struct GenerationJob {
    std::thread thread;
    std::atomic<bool> done{false};
    std::string error;
    std::string hash;
};
std::unique_ptr<GenerationJob> s_job;

ApClient& EnsureClient() {
    if (!s_client) {
        s_client = std::make_unique<ApClient>(transport::Create, &s_listener);
        const auto cacheDir = ArchipelagoDir() / "datapackage";
        s_client->SetDataPackageCache({
            [cacheDir](const std::string& game, const std::string& checksum) {
                return ReadText(cacheDir / (SafeFileName(game) + "_" + SafeFileName(checksum) + ".json"));
            },
            [cacheDir](const std::string& game, const std::string& checksum, const std::string& data) {
                WriteText(cacheDir / (SafeFileName(game) + "_" + SafeFileName(checksum) + ".json"), data);
            },
        });
    }
    return *s_client;
}

ApClientConfig MakeConfig(const ConnectionForm& form, bool autoReconnect, bool deathLink) {
    ApClientConfig config;
    config.address = form.server;
    config.slotName = form.slot;
    config.password = form.password;
    config.game = kGame;
    config.uuid = GetConfigString(s_cfgUuid);
    config.itemsHandling = kItemsHandlingOtherWorlds | kItemsHandlingStartInventory;
    config.autoReconnect = autoReconnect;
    if (deathLink) {
        config.tags.push_back("DeathLink");
    }
    return config;
}

void ConnectClient(const ApClientConfig& config) {
    s_verifiedSeed.clear();
    EnsureClient().Connect(config, NowMs());
}

void Listener::OnStateChanged(ApState state, const std::string& detail) {
    if (s_newPhase == NewSavePhase::Connecting && state == ApState::Failed) {
        s_newPhase = NewSavePhase::Error;
        s_newMessage = detail;
        return;
    }
    if (!s_rt.active) {
        return;
    }
    if (state == ApState::Connected) {
        AddLog(EscapeRml("Connected to " + s_rt.save.connection.server + " as " + s_rt.save.connection.slot + "."));
        Toast("Archipelago", EscapeRml("Connected as " + s_rt.save.connection.slot));
    } else if (state == ApState::Waiting) {
        AddLog(EscapeRml(detail + " (retrying)"));
        Toast("Archipelago", EscapeRml(detail), "warning");
    } else if (state == ApState::Failed) {
        AddLog(EscapeRml(detail));
        Toast("Archipelago", EscapeRml(detail), "warning", 8000);
    }
}

void Listener::OnConnected(const json& slotData) {
    if (s_newPhase == NewSavePhase::Connecting) {
        std::string error;
        auto slot = SlotData::Parse(slotData, error);
        if (!slot) {
            s_newPhase = NewSavePhase::Error;
            s_newMessage = error;
            s_client->Disconnect();
            return;
        }
        s_newSlot = std::move(slot);
        s_newSlotJson = slotData.dump();
        s_verifiedSeed = s_newSlot->seed;
        s_newPhase = NewSavePhase::Generating;
        s_newMessage = "Connected. Generating the seed...";

        // Generation inputs and outputs live in the mod data folder
        const std::string seed = s_newSlot->seed;
        const auto workDir = SeedWorkDir(seed);
        WriteText(SlotDataPath(seed), s_newSlotJson);
        const auto dataDir = paths::GetRandomizerPath();
        s_job = std::make_unique<GenerationJob>();
        GenerationJob* job = s_job.get();
        SlotData slotCopy = *s_newSlot;
        job->thread = std::thread([job, slotCopy, dataDir, workDir] {
            try {
                Randomizer rando{dataDir};
                if (auto error = GenerateWorlds(rando, workDir, slotCopy)) {
                    job->error = *error;
                } else if (auto problems = VerifyPlacements(rando, slotCopy); !problems.empty()) {
                    job->error = fmt::format("{} location(s) do not match the multiworld, for example {}",
                        problems.size(), problems.front());
                } else {
                    RandomizerContext context = WriteSeedData(rando.GetWorld());
                    context.mHash = rando.GetConfig().GetHash();
                    if (auto error = context.WriteToFile()) {
                        job->error = *error;
                    } else {
                        job->hash = context.mHash;
                    }
                }
            } catch (const std::exception& e) {
                job->error = e.what();
            }
            job->done = true;
        });
        return;
    }

    if (!s_rt.active) {
        return;
    }
    // Make sure this is the room the save belongs to
    const std::string seed = slotData.value("seed", "");
    if (seed != s_rt.save.seed || seed.empty()) {
        s_verifiedSeed.clear();
        const std::string message = "This room is not the one this save was created for (seed " + seed +
                                    ", save " + s_rt.save.seed + "). Disconnected.";
        mods::log::error("Archipelago: {}", message);
        s_client->Disconnect();
        AddLog(EscapeRml(message));
        Toast("Archipelago", EscapeRml(message), "warning", 10000);
        return;
    }
    s_verifiedSeed = seed;
    if (s_rt.save.goal) {
        s_client->SetGoalReached();
    }
    s_rt.goalSent = s_rt.save.goal;
}

void Listener::OnMessage(const ApMessage& message) {
    if (message.type == "" && message.Plain().find("compressed websocket") != std::string::npos) {
        // The server suggests a compressed connection; nothing a player can act on
        mods::log::info("Archipelago: {}", message.Plain());
        return;
    }
    AddLog(MessageRml(message));
    if (!s_rt.active || !s_client) {
        return;
    }
    const int me = s_client->Slot();
    if ((message.type == "ItemSend" || message.type == "ItemCheat") && message.item) {
        const auto& item = *message.item;
        if (item.player == me && message.receiving != me) {
            // An item from this world went to someone else
            std::string itemName = s_client->ItemName(item.item, message.receiving);
            if (const auto* location = s_rt.slot ? s_rt.slot->FindById(item.location) : nullptr) {
                itemName = location->item;
            }
            Toast("Sent", Colored(itemName, ItemColor(item.flags)) + " to " +
                              Colored(s_client->PlayerName(message.receiving), "#fafad2"));
        }
    } else if (message.type == "Hint" && message.item) {
        if (!message.found && (message.receiving == me || message.item->player == me)) {
            Toast("Hint", MessageRml(message), nullptr, 8000);
        }
    } else if (message.type == "Chat" || message.type == "ServerChat") {
        Toast("", MessageRml(message));
    } else if (message.type == "Countdown") {
        Toast("", MessageRml(message), nullptr, 1500);
    }
}

void Listener::OnDeathLink(const std::string& source, const std::string& cause) {
    if (!s_rt.active || !s_rt.save.deathLink) {
        return;
    }
    const std::string text = cause.empty() ? source + " died." : cause;
    AddLog(EscapeRml("DeathLink: " + text));
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == nullptr || playerIsOnTitleScreen() || link->mProcID == daAlink_c::PROC_DEAD) {
        return;
    }
    Toast("DeathLink", EscapeRml(text), "warning");
    s_rt.deathLinkKill = true;
    dComIfGs_setLife(0);
}

// ---------------------------------------------------------------------------------------------
// Per-frame work

void ProcessReceivedItems() {
    if (!s_client || !s_rt.active || !s_rt.slot || !randomizer_IsActive() ||
        randomizer_GetContext().mCreatingSave)
    {
        return;
    }
    if (s_verifiedSeed.empty() || s_verifiedSeed != s_rt.save.seed) {
        return;  // the item list is not (yet) known to be this save's
    }
    const auto& items = s_client->Items();
    s_rt.queuedItems = std::max(s_rt.queuedItems, s_rt.save.received);
    const int64_t itemBase = s_rt.slot->itemIdBase;
    while (s_rt.queuedItems < items.size()) {
        const size_t index = s_rt.queuedItems++;
        const auto& item = items[index];
        const int64_t local = item.item - itemBase;
        if (local < 0 || local > 0xFE || !ItemNames().contains(static_cast<uint8_t>(local))) {
            mods::log::warn("Archipelago: ignoring unknown item {} (index {})", item.item, index);
            s_rt.doneItems.insert(index);
            continue;
        }
        const std::string name = std::string{kReceivePrefix} + std::to_string(index);
        svc_mng.item->give_item(mod_ctx, name.c_str(), static_cast<uint8_t>(local),
            ITEM_GIVE_SILENT | ITEM_GIVE_RESOLVE);
    }
    while (s_rt.doneItems.erase(s_rt.save.received) != 0) {
        ++s_rt.save.received;
    }
}

void FlushReceivedToasts() {
    auto& pending = s_rt.pendingReceived;
    if (pending.empty()) {
        return;
    }
    if (pending.size() <= 3) {
        for (const auto& line : pending) {
            Toast("Received", line);
        }
    } else {
        Toast("Received", fmt::format("{} items, including {}", pending.size(), pending.back()));
    }
    pending.clear();
}

void CheckGoalAndDeath() {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == nullptr) {
        return;
    }

    // DeathLink: entering the death state (a fairy revival never gets there)
    const bool dead = link->mProcID == daAlink_c::PROC_DEAD;
    if (dead && !s_rt.wasDead) {
        if (s_rt.deathLinkKill) {
            s_rt.deathLinkKill = false;  // the death another player caused
        } else if (s_rt.save.deathLink && s_client) {
            s_client->SendDeathLink(s_rt.save.connection.slot + " fell in Hyrule.", UnixSeconds());
        }
    } else if (!dead && s_rt.deathLinkKill && dComIfGs_getLife() > 0) {
        s_rt.deathLinkKill = false;  // saved by a fairy
    }
    s_rt.wasDead = dead;

    // Goal: the final blow on Ganondorf (daB_GND enters its end action)
    if (!s_rt.save.goal) {
        const char* stage = dComIfGp_getStartStageName();
        if (stage != nullptr && (std::strcmp(stage, "D_MN09B") == 0 || std::strcmp(stage, "D_MN09C") == 0)) {
            if (auto* ganondorf = static_cast<b_gnd_class*>(fopAcM_SearchByName(fpcNm_B_GND_e));
                ganondorf != nullptr && ganondorf->mActionMode == 22 /* ACTION_END */)
            {
                s_rt.save.goal = true;
                WriteText(GoalMarkerPath(s_rt.save.seed), "1\n");
                mods::log::info("Archipelago: Ganondorf defeated, goal complete");
                AddLog("Goal complete!");
                Toast("Archipelago", "Goal complete!", nullptr, 8000);
            }
        }
    }
    if (s_rt.save.goal && !s_rt.goalSent && s_client) {
        s_client->SetGoalReached();
        s_rt.goalSent = true;
    }
}

void TickNewSave() {
    if (s_newPhase != NewSavePhase::Generating || !s_job || !s_job->done) {
        return;
    }
    if (s_job->thread.joinable()) {
        s_job->thread.join();
    }
    if (!s_job->error.empty() || s_job->hash.empty()) {
        s_newPhase = NewSavePhase::Error;
        s_newMessage = "Could not generate the seed: " + (s_job->error.empty() ? "unknown error" : s_job->error);
        if (s_newSlot && s_newSlot->apworldVersion != ARCHIPELAGO_MOD_VERSION) {
            s_newMessage += " (the multiworld was made with version " + s_newSlot->apworldVersion +
                            " of the APWorld and this mod is version " ARCHIPELAGO_MOD_VERSION
                            "; use the same version of both)";
        }
        mods::log::error("Archipelago: {}", s_newMessage);
        if (s_client) {
            s_client->Disconnect();
        }
    } else {
        s_pendingHash = s_job->hash;
        s_newPhase = NewSavePhase::Ready;
        s_newMessage = "Ready! Seed " + s_pendingHash + " for " + s_newForm.slot + ".";
        svc_mng.config->set_string(mod_ctx, s_cfgServer, s_newForm.server.c_str());
        svc_mng.config->set_string(mod_ctx, s_cfgSlot, s_newForm.slot.c_str());
    }
    s_job.reset();
}

void ResetRuntime() {
    s_rt = Runtime{};
}

bool LoadSlot(const std::string& seed) {
    const auto text = ReadText(SlotDataPath(seed));
    if (!text) {
        mods::log::error("Archipelago: missing slot data for seed {}", seed);
        return false;
    }
    try {
        std::string error;
        s_rt.slot = SlotData::Parse(json::parse(*text), error);
        if (!s_rt.slot) {
            mods::log::error("Archipelago: {}", error);
            return false;
        }
    } catch (const std::exception& e) {
        mods::log::error("Archipelago: bad slot data for seed {}: {}", seed, e.what());
        return false;
    }
    s_rt.index.Build(*s_rt.slot);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public

ModResult Initialize() {
    RegisterConfig();
    return MOD_OK;
}

ModResult OnActivated() {
    RegisterMessageOverrides();
    return ui::RegisterMenuTab();
}

void OnDeactivated() {
    CancelNewSave();
    if (s_client) {
        s_client->Disconnect();
    }
    s_client.reset();
    s_messageOverrides.clear();
    ui::UnregisterMenuTab();
    ResetRuntime();
}

ModResult OnNewSaveSelect(GameModeNewSaveState* state) {
    CancelNewSave();
    s_newForm.server = GetConfigString(s_cfgServer);
    if (s_newForm.server.empty()) {
        s_newForm.server = "archipelago.gg:38281";
    }
    s_newForm.slot = GetConfigString(s_cfgSlot);
    s_newForm.password.clear();
    s_newPhase = NewSavePhase::Idle;
    s_newMessage.clear();
    s_pendingHash.clear();
    return ui::OpenNewSaveWindow(state);
}

const std::string& PendingSeedHash() {
    return s_pendingHash;
}

void StartNewSaveConnection() {
    if (s_newPhase == NewSavePhase::Connecting || s_newPhase == NewSavePhase::Generating) {
        return;
    }
    if (s_newForm.server.empty() || s_newForm.slot.empty()) {
        s_newPhase = NewSavePhase::Error;
        s_newMessage = "Enter the server address and your slot name.";
        return;
    }
    // A new save is not loaded yet; anything from a previous save stops here
    ResetRuntime();
    s_pendingHash.clear();
    s_newSlot.reset();
    s_newPhase = NewSavePhase::Connecting;
    s_newMessage = "Connecting to " + s_newForm.server + "...";
    ConnectClient(MakeConfig(s_newForm, false, false));
}

void CancelNewSave() {
    if (s_job) {
        if (s_job->thread.joinable()) {
            s_job->thread.join();
        }
        s_job.reset();
    }
    if (s_newPhase == NewSavePhase::Connecting || s_newPhase == NewSavePhase::Generating) {
        if (s_client) {
            s_client->Disconnect();
        }
    }
    s_newPhase = NewSavePhase::Idle;
}

ConnectionForm& NewSaveForm() {
    return s_newForm;
}

NewSavePhase GetNewSavePhase() {
    return s_newPhase;
}

const std::string& NewSaveMessage() {
    return s_newMessage;
}

ModResult OnNewSave() {
    if (!s_newSlot || s_pendingHash.empty()) {
        return MOD_ERROR;
    }
    ResetRuntime();
    s_rt.active = true;
    s_rt.save.connection = s_newForm;
    s_rt.save.seed = s_newSlot->seed;
    s_rt.save.seedHash = s_pendingHash;
    s_rt.save.deathLink = s_newSlot->deathLink;
    s_rt.slot = std::move(s_newSlot);
    s_newSlot.reset();
    s_rt.index.Build(*s_rt.slot);
    WriteSaveBlob();

    // Keep the connection from the new-save screen, now retrying if it drops
    if (s_client && s_client->IsConnected()) {
        s_client->SetAutoReconnect(true);
        s_client->SetDeathLink(s_rt.save.deathLink);
    } else {
        ConnectClient(MakeConfig(s_rt.save.connection, true, s_rt.save.deathLink));
    }
    s_newPhase = NewSavePhase::Idle;
    s_pendingHash.clear();
    mods::log::info("Archipelago: new save for {} ({} locations)", s_rt.save.connection.slot,
        s_rt.index.Locations().size());
    return MOD_OK;
}

std::string PrepareSeed(const std::string& seedHash) {
    auto state = ReadSaveBlob();
    if (!state || state->seed.empty()) {
        return seedHash;
    }
    const auto seedFile = paths::GetRandomizerSeedsPath() / seedHash / "seed.dat";
    try {
        std::error_code ec;
        if (std::filesystem::exists(seedFile, ec) &&
            LoadYAML(seedFile)["formatVersion"].as<u32>(0) == RandomizerContext::FORMAT_VERSION)
        {
            return seedHash;  // seed files are fine
        }
    } catch (const std::exception&) {
    }
    // Missing or obsolete (written by an older version of the mod): generate again
    const auto text = ReadText(SlotDataPath(state->seed));
    if (!text) {
        mods::log::error("Archipelago: seed {} is missing and there is no slot data to rebuild it", seedHash);
        return seedHash;
    }
    try {
        std::string parseError;
        auto slot = SlotData::Parse(json::parse(*text), parseError);
        if (!slot) {
            mods::log::error("Archipelago: {}", parseError);
            return seedHash;
        }
        Randomizer rando{paths::GetRandomizerPath()};
        if (auto generateError = GenerateWorlds(rando, SeedWorkDir(state->seed), *slot)) {
            mods::log::error("Archipelago: could not rebuild seed {}: {}", seedHash, *generateError);
            return seedHash;
        }
        RandomizerContext context = WriteSeedData(rando.GetWorld());
        context.mHash = rando.GetConfig().GetHash();
        if (auto writeError = context.WriteToFile()) {
            mods::log::error("Archipelago: could not write seed {}: {}", context.mHash, *writeError);
            return seedHash;
        }
        mods::log::info("Archipelago: rebuilt seed {} from slot data (save had {})", context.mHash, seedHash);
        return context.mHash;
    } catch (const std::exception& e) {
        mods::log::error("Archipelago: could not rebuild seed {}: {}", seedHash, e.what());
    }
    return seedHash;
}

ModResult OnSaveLoaded() {
    if (s_job) {
        CancelNewSave();
    }
    ResetRuntime();
    auto state = ReadSaveBlob();
    if (!state) {
        mods::log::error("Archipelago: this save has no Archipelago state");
        if (s_client) {
            s_client->Disconnect();
        }
        return MOD_OK;
    }
    s_rt.save = *state;
    s_rt.active = true;
    s_capturedState.reset();
    if (!LoadSlot(s_rt.save.seed)) {
        Toast("Archipelago", "The data for this save's seed is missing. Locations will not be sent.", "warning",
            10000);
    }
    s_rt.save.seedHash = randomizer_GetContext().mHash;
    s_rt.goalSent = false;
    std::error_code ec;
    if (std::filesystem::exists(GoalMarkerPath(s_rt.save.seed), ec)) {
        s_rt.save.goal = true;
    }
    // Locations already done in this save are found by the first scan in game and sent then
    const auto& connection = s_rt.save.connection;
    if (!connection.server.empty() && !connection.slot.empty()) {
        // A new save keeps the connection it was created with
        const bool keep = s_client && !s_verifiedSeed.empty() && s_verifiedSeed == s_rt.save.seed &&
                          s_client->State() != ApState::Idle && s_client->State() != ApState::Failed &&
                          s_client->Config().address == connection.server &&
                          s_client->Config().slotName == connection.slot &&
                          s_client->Config().password == connection.password;
        if (keep) {
            s_client->SetAutoReconnect(true);
            s_client->SetDeathLink(s_rt.save.deathLink);
        } else {
            ConnectClient(MakeConfig(connection, true, s_rt.save.deathLink));
        }
    } else if (s_client) {
        s_client->Disconnect();
        s_verifiedSeed.clear();
    }
    return MOD_OK;
}

void OnSaveCaptured() {
    if (s_rt.active) {
        s_capturedState = SaveToJson(s_rt.save).dump();
    }
}

void OnSaveWritten() {
    if (!s_rt.active) {
        return;
    }
    // What the game wrote was captured earlier (the write is asynchronous): items given since
    // then are not in it, so they must not be counted in it either
    const std::string text = s_capturedState.value_or(SaveToJson(s_rt.save).dump());
    s_capturedState.reset();
    svc_mng.save->set_blob(mod_ctx, kStateBlobName, text.data(), text.size());
}

void OnGameReset() {
    CancelNewSave();
    if (s_client) {
        s_client->Disconnect();
    }
    ResetRuntime();
}

void Tick() {
    ++s_frame;
    transport::Pump();
    if (s_client) {
        s_client->Tick(NowMs());
    }
    TickNewSave();
    if (!s_rt.active || !randomizer_IsActive() || playerIsOnTitleScreen() ||
        daAlink_getAlinkActorClass() == nullptr || dComIfGp_getStageStagInfo() == nullptr)
    {
        return;  // only while playing: flags are read from the loaded stage and save
    }
    if (!s_rt.messageChecked) {
        CheckForeignMessage();
    }
    if (++s_rt.scanTimer >= kScanInterval) {
        s_rt.scanTimer = 0;
        MarkChecked(ScanFlags());
    }
    ProcessReceivedItems();
    CheckGoalAndDeath();
    FlushReceivedToasts();
}

bool ResolveCheck(const ItemCheckInfo* info, ItemCheckResolution* outResult) {
    if (info == nullptr || info->name == nullptr || std::strncmp(info->name, kReceivePrefix,
                                                         std::strlen(kReceivePrefix)) != 0)
    {
        return false;
    }
    outResult->item = static_cast<uint8_t>(verifyProgressiveItem(info->vanilla_item));
    return true;
}

void NoteResolution(const ItemCheckInfo* info, uint8_t item) {
    if (info == nullptr || item != kArchipelagoItemId || !s_rt.active) {
        return;
    }
    const auto index = s_rt.index.ForCheck(info->name);
    if (!index) {
        return;
    }
    const ApLocation* location = s_rt.index.Locations()[*index].ap;
    auto& notes = s_rt.foreignNotes;
    std::erase_if(notes, [location](const auto& note) { return note.location == location; });
    notes.push_back({location, s_frame});
    while (notes.size() > 64) {
        notes.pop_front();
    }
    // Item actors carry the give tag their get-item box will have
    if (const auto* giver = static_cast<const fopAc_ac_c*>(info->giver_actor);
        giver != nullptr && giver->mItemGiveTag != 0)
    {
        s_rt.foreignByTag[giver->mItemGiveTag] = location;
    }
}

bool ForeignItemIsProgression() {
    const ApLocation* location = s_rt.active ? ForeignLocationShown() : nullptr;
    return location == nullptr || (location->flags & kItemProgression) != 0;
}

uint16_t ForeignFallbackMessageId() {
    if (!s_foreignMessageMissing || !s_rt.active) {
        return 0;
    }
    const ApLocation* location = ForeignLocationShown();
    const auto style = mods::flow::MessageStyle{}.box_kind(MESSAGE_BOX_ITEM_GET);
    std::vector<mods::flow::MessageVariant> variants;
    for (const auto language : {MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN, MESSAGE_LANGUAGE_FRENCH,
             MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN, MESSAGE_LANGUAGE_JAPANESE})
    {
        const std::string& text = ForeignMessageText(location, language);
        std::vector<uint8_t> bytes{text.begin(), text.end()};
        bytes.push_back(0);
        variants.emplace_back(language, style.data(), std::move(bytes));
    }
    s_fallbackMessage = mods::flow::register_message(0, variants);
    return s_fallbackMessage ? s_fallbackMessage.id() : 0;
}

void ObserveGive(const ItemGiveInfo* info) {
    if (info == nullptr || info->check_name == nullptr || !s_rt.active) {
        return;
    }
    const char* name = info->check_name;
    const size_t prefixLength = std::strlen(kReceivePrefix);
    if (std::strncmp(name, kReceivePrefix, prefixLength) == 0) {
        const size_t index = std::strtoull(name + prefixLength, nullptr, 10);
        s_rt.doneItems.insert(index);
        while (s_rt.doneItems.erase(s_rt.save.received) != 0) {
            ++s_rt.save.received;
        }
        if (s_client && s_rt.slot && s_verifiedSeed == s_rt.save.seed && index < s_client->Items().size()) {
            const auto& item = s_client->Items()[index];
            const int64_t local = item.item - s_rt.slot->itemIdBase;
            std::string itemName = s_client->ItemName(item.item, s_client->Slot());
            if (const auto it = ItemNames().find(static_cast<uint8_t>(local)); it != ItemNames().end()) {
                itemName = it->second;
            }
            const std::string from = item.location == -2 ? std::string{"your starting inventory"} :
                                                           s_client->PlayerName(item.player);
            s_rt.pendingReceived.push_back(Colored(itemName, ItemColor(item.flags)) + " from " +
                                           Colored(from, "#fafad2"));
            AddLog("Received " + s_rt.pendingReceived.back());
        }
        return;
    }
    if (const auto location = s_rt.index.ForCheck(name)) {
        if (const auto* ap = s_rt.index.Locations()[*location].ap) {
            MarkChecked({ap->id});
            // Given: its get-item box (if any) has been shown
            std::erase_if(s_rt.foreignNotes, [ap](const auto& note) { return note.location == ap; });
        }
    }
}

ConnectionForm& SaveForm() {
    return s_rt.save.connection;
}

bool SaveActive() {
    return s_rt.active;
}

void ConnectSave() {
    if (!s_rt.active || s_rt.save.connection.server.empty() || s_rt.save.connection.slot.empty()) {
        return;
    }
    ConnectClient(MakeConfig(s_rt.save.connection, true, s_rt.save.deathLink));
    if (!s_rt.checked.empty()) {
        s_client->CheckLocations({s_rt.checked.begin(), s_rt.checked.end()});
    }
    s_rt.goalSent = false;
}

void DisconnectSave() {
    if (s_client) {
        s_client->Disconnect();
    }
}

ApClient* Client() {
    return s_client.get();
}

std::string StatusText() {
    if (!s_client) {
        return "Not connected";
    }
    const ApState state = s_client->State();
    std::string text = ApStateName(state);
    if (state == ApState::Connected) {
        text += " to " + s_client->Detail() + " as " + s_client->Config().slotName;
    } else if (!s_client->Detail().empty() && state != ApState::Idle) {
        text += ": " + s_client->Detail();
    }
    return text;
}

const std::deque<std::string>& MessageLog() {
    return s_log;
}

uint64_t MessageLogVersion() {
    return s_logVersion;
}

void Say(const std::string& text) {
    if (s_client && !text.empty()) {
        s_client->Say(text);
    }
}

bool DeathLinkEnabled() {
    return s_rt.save.deathLink;
}

void SetDeathLinkEnabled(bool enabled) {
    s_rt.save.deathLink = enabled;
    if (s_client) {
        s_client->SetDeathLink(enabled);
    }
}

bool ToastsEnabled() {
    return NotificationsOn();
}

void SetToastsEnabled(bool enabled) {
    if (s_cfgToasts != 0) {
        svc_mng.config->set_bool(mod_ctx, s_cfgToasts, enabled);
    }
}

size_t LocationCount() {
    return s_rt.index.Locations().size();
}

size_t CheckedLocationCount() {
    if (s_client && s_client->IsConnected()) {
        std::set<int64_t> all = s_client->ServerCheckedLocations();
        all.insert(s_rt.checked.begin(), s_rt.checked.end());
        return all.size();
    }
    return s_rt.checked.size();
}

std::vector<std::string> RemainingLocations() {
    std::vector<std::string> names;
    for (const auto& location : s_rt.index.Locations()) {
        if (location.ap == nullptr || s_rt.checked.contains(location.ap->id)) {
            continue;
        }
        if (s_client && s_client->ServerCheckedLocations().contains(location.ap->id)) {
            continue;
        }
        names.push_back(location.ap->name);
    }
    std::ranges::sort(names);
    return names;
}

size_t ReceivedItemCount() {
    return s_rt.save.received;
}

std::string EscapeRml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

}  // namespace randomizer::archi::game
