#include "ap_ui.hpp"

#include "ap_game.hpp"

#include "../session.hpp"
#include "../ui/rando_seed_generation.hpp"

#include "Z2AudioLib/Z2SeMgr.h"
#include "m_Do/m_Do_audio.h"

#include <fmt/format.h>

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace randomizer::archi::ui {
namespace {

using session::svc_mng;

const UiService* Ui() {
    return svc_mng.ui;
}

void AddText(UiElementHandle pane, const char* text) {
    Ui()->pane_add_text(mod_ctx, pane, text, nullptr);
}

UiElementHandle AddRml(UiElementHandle pane, const std::string& rml) {
    UiElementHandle handle{};
    Ui()->pane_add_rml(mod_ctx, pane, rml.c_str(), &handle);
    return handle;
}

void AddStringField(UiElementHandle pane, const char* label, const char* help, std::string* target,
    int32_t maxLength = 200) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_STRING;
    desc.label = label;
    desc.help_rml = help;
    desc.max_length = maxLength;
    desc.string_set_mode = UI_STRING_SET_ON_CHANGE;
    desc.user_data = target;
    desc.get = [](ModContext*, void* userData, UiControlValue* out) {
        out->string_value = static_cast<std::string*>(userData)->c_str();
    };
    desc.set = [](ModContext*, void* userData, const UiControlValue* value) {
        *static_cast<std::string*>(userData) = value->string_value != nullptr ? value->string_value : "";
    };
    Ui()->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void AddButton(UiElementHandle pane, const char* label, const char* help, UiPressedFn onPressed,
    UiPredicateFn isDisabled = nullptr, void* userData = nullptr) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_BUTTON;
    desc.label = label;
    desc.help_rml = help;
    desc.on_pressed = onPressed;
    desc.is_disabled = isDisabled;
    desc.user_data = userData;
    Ui()->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void AddToggle(UiElementHandle pane, const char* label, const char* help, UiControlGetFn get,
    UiControlSetFn set) {
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_TOGGLE;
    desc.label = label;
    desc.help_rml = help;
    desc.get = get;
    desc.set = set;
    Ui()->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

constexpr const char* kServerHelp =
    "The address of the Archipelago room, for example <b>archipelago.gg:38281</b>. The port is shown "
    "on the room page and can change when the room restarts. Secure (wss) and plain (ws) connections "
    "are both tried.";
constexpr const char* kSlotHelp = "Your player (slot) name, as written in your YAML file.";
constexpr const char* kPasswordHelp = "The room password. Leave empty if the room has none.";

// ---------------------------------------------------------------------------------------------
// New save

struct NewSaveWindow {
    GameModeNewSaveState* state = nullptr;
    UiWindowHandle window{};
    UiElementHandle status{};
    std::string shownStatus;
};
NewSaveWindow s_newSave;

std::string NewSaveStatusRml() {
    using game::NewSavePhase;
    const std::string message = game::EscapeRml(game::NewSaveMessage());
    switch (game::GetNewSavePhase()) {
    case NewSavePhase::Idle:
        return "Not connected.";
    case NewSavePhase::Connecting:
        return message;
    case NewSavePhase::Generating: {
        // The generator reports its steps the same way as for randomizer seeds
        const std::string step = randomizer::ui::ReadGenerationStatusMsg();
        return message + (step.empty() ? "" : "<br/>" + game::EscapeRml(step));
    }
    case NewSavePhase::Ready:
        return "<span style=\"color: #7fff7f;\">" + message + "</span> Press Start to name your file.";
    case NewSavePhase::Error:
        return "<span style=\"color: #ff8080;\">" + message + "</span>";
    }
    return {};
}

ModResult BuildConnectTab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    auto& form = game::NewSaveForm();
    AddStringField(left, "Server", kServerHelp, &form.server);
    AddStringField(left, "Slot Name", kSlotHelp, &form.slot, 16);
    AddStringField(left, "Password", kPasswordHelp, &form.password);
    AddButton(left, "Connect",
        "Connects to the room and builds this world from the room's data. This takes a few seconds.",
        [](ModContext*, void*) { game::StartNewSaveConnection(); },
        [](ModContext*, void*) {
            const auto phase = game::GetNewSavePhase();
            return phase == game::NewSavePhase::Connecting || phase == game::NewSavePhase::Generating;
        });
    s_newSave.shownStatus = NewSaveStatusRml();
    s_newSave.status = AddRml(left, s_newSave.shownStatus);
    AddButton(left, "Start", "Creates the save file for this Archipelago slot.",
        [](ModContext*, void*) {
            if (s_newSave.state != nullptr) {
                *s_newSave.state = GAME_MODE_STATE_PROCEED;
            }
            Ui()->window_close(mod_ctx, s_newSave.window);
            mDoAud_seStartMenu(Z2SE_SY_NEW_FILE);
        },
        [](ModContext*, void*) { return game::GetNewSavePhase() != game::NewSavePhase::Ready; });

    AddRml(right,
        "<b>Archipelago</b><br/>"
        "This file plays your slot of an Archipelago multiworld. Items found here can belong to other "
        "players, and theirs can be yours.<br/><br/>"
        "Generate the multiworld with the <b>Twilight Princess Dusklight</b> APWorld, host it, then enter "
        "the room's address and your slot name here. The game is built from the room's data, so other "
        "Dusklight mods keep working alongside it.");
    return MOD_OK;
}

ModResult UpdateConnectTab(ModContext*, void*, ModError*) {
    const std::string status = NewSaveStatusRml();
    if (status != s_newSave.shownStatus && s_newSave.status != 0) {
        s_newSave.shownStatus = status;
        Ui()->elem_set_rml(mod_ctx, s_newSave.status, status.c_str());
    }
    return MOD_OK;
}

// ---------------------------------------------------------------------------------------------
// In-game menu

struct GameWindow {
    UiElementHandle status{};
    UiElementHandle stats{};
    UiElementHandle log{};
    std::string shownStatus;
    std::string shownStats;
    uint64_t shownLog = 0;
    std::string chat;
};
GameWindow s_game;
UiMenuTabHandle s_menuTab{};

std::string GameStatusRml() {
    if (!game::SaveActive()) {
        return "Load an Archipelago save to connect.";
    }
    auto* client = game::Client();
    const bool connected = client != nullptr && client->IsConnected();
    return fmt::format("<span style=\"color: {};\">{}</span>", connected ? "#7fff7f" : "#ffd27f",
        game::EscapeRml(game::StatusText()));
}

std::string GameStatsRml() {
    if (!game::SaveActive()) {
        return {};
    }
    return fmt::format("Locations checked: {} / {}<br/>Items received: {}", game::CheckedLocationCount(),
        game::LocationCount(), game::ReceivedItemCount());
}

std::string LogRml(size_t lines) {
    const auto& log = game::MessageLog();
    if (log.empty()) {
        return "<i>No messages yet.</i>";
    }
    std::string rml;
    const size_t start = log.size() > lines ? log.size() - lines : 0;
    for (size_t i = log.size(); i-- > start;) {
        rml += log[i] + "<br/>";
    }
    return rml;
}

ModResult BuildConnectionTab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    s_game.shownStatus = GameStatusRml();
    s_game.status = AddRml(left, s_game.shownStatus);
    s_game.shownStats = GameStatsRml();
    s_game.stats = AddRml(left, s_game.shownStats);
    if (game::SaveActive()) {
        auto& form = game::SaveForm();
        AddStringField(left, "Server", kServerHelp, &form.server);
        AddStringField(left, "Slot Name", kSlotHelp, &form.slot, 16);
        AddStringField(left, "Password", kPasswordHelp, &form.password);
        AddButton(left, "Connect", "Connects (again) with the settings above. They are kept in this save.",
            [](ModContext*, void*) { game::ConnectSave(); });
        AddButton(left, "Disconnect", "Stops sending and receiving until you connect again.",
            [](ModContext*, void*) { game::DisconnectSave(); });
        AddToggle(left, "DeathLink",
            "When you die, everyone with DeathLink dies, and the other way around. Saved in this file.",
            [](ModContext*, void*, UiControlValue* out) { out->bool_value = game::DeathLinkEnabled(); },
            [](ModContext*, void*, const UiControlValue* value) { game::SetDeathLinkEnabled(value->bool_value); });
    }
    AddToggle(left, "Notifications", "Pop-ups for items sent and received, hints and chat.",
        [](ModContext*, void*, UiControlValue* out) { out->bool_value = game::ToastsEnabled(); },
        [](ModContext*, void*, const UiControlValue* value) { game::SetToastsEnabled(value->bool_value); });
    AddRml(right,
        "<b>Archipelago</b><br/>Locations you check are sent to the room as you find them, and items "
        "from other players arrive in your inventory while you play.<br/><br/>If you play offline, "
        "everything you found is sent the next time you connect.");
    return MOD_OK;
}

ModResult UpdateConnectionTab(ModContext*, void*, ModError*) {
    const std::string status = GameStatusRml();
    if (status != s_game.shownStatus && s_game.status != 0) {
        s_game.shownStatus = status;
        Ui()->elem_set_rml(mod_ctx, s_game.status, status.c_str());
    }
    const std::string stats = GameStatsRml();
    if (stats != s_game.shownStats && s_game.stats != 0) {
        s_game.shownStats = stats;
        Ui()->elem_set_rml(mod_ctx, s_game.stats, stats.c_str());
    }
    return MOD_OK;
}

ModResult BuildMessagesTab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    UiControlDesc chat = UI_CONTROL_DESC_INIT;
    chat.kind = UI_CONTROL_STRING;
    chat.label = "Say";
    chat.help_rml = "Sends a chat message or a server command such as <b>!hint Clawshot</b> or "
                    "<b>!remaining</b>.";
    chat.max_length = 400;
    chat.string_set_mode = UI_STRING_SET_ON_COMMIT;
    chat.get = [](ModContext*, void*, UiControlValue* out) { out->string_value = s_game.chat.c_str(); };
    chat.set = [](ModContext*, void*, const UiControlValue* value) {
        const std::string text = value->string_value != nullptr ? value->string_value : "";
        game::Say(text);
        s_game.chat.clear();
    };
    Ui()->pane_add_control(mod_ctx, left, &chat, nullptr);
    s_game.shownLog = game::MessageLogVersion();
    s_game.log = AddRml(left, LogRml(60));
    AddRml(right, "Messages from the room, newest first. Hints for your items also show up here.");
    return MOD_OK;
}

ModResult UpdateMessagesTab(ModContext*, void*, ModError*) {
    if (game::MessageLogVersion() != s_game.shownLog && s_game.log != 0) {
        s_game.shownLog = game::MessageLogVersion();
        Ui()->elem_set_rml(mod_ctx, s_game.log, LogRml(60).c_str());
    }
    return MOD_OK;
}

struct LocationsTab {
    UiListHandle list{};
    UiElementHandle count{};
    std::vector<std::string> names;
    size_t shownChecked = SIZE_MAX;
};
LocationsTab s_locations;

void RefreshLocations() {
    s_locations.names = game::RemainingLocations();
    std::vector<UiListItem> items;
    items.reserve(s_locations.names.size());
    for (size_t i = 0; i < s_locations.names.size(); ++i) {
        UiListItem item = UI_LIST_ITEM_INIT;
        item.key = i + 1;
        item.label = s_locations.names[i].c_str();
        items.push_back(item);
    }
    if (s_locations.list != 0) {
        Ui()->list_set_items(mod_ctx, s_locations.list, items.data(), items.size());
    }
    if (s_locations.count != 0) {
        const std::string text = fmt::format("{} location(s) left to check.", s_locations.names.size());
        Ui()->elem_set_text(mod_ctx, s_locations.count, text.c_str());
    }
}

ModResult BuildLocationsTab(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    s_locations = LocationsTab{};
    if (!game::SaveActive()) {
        AddText(left, "Load an Archipelago save to see its locations.");
        return MOD_OK;
    }
    Ui()->pane_add_text(mod_ctx, left, "", &s_locations.count);
    UiListDesc desc = UI_LIST_DESC_INIT;
    desc.on_pressed = [](ModContext*, UiListHandle, uint64_t, void*) {};
    Ui()->pane_add_list(mod_ctx, left, &desc, &s_locations.list);
    s_locations.shownChecked = game::CheckedLocationCount();
    RefreshLocations();
    AddRml(right, "The locations of this world that have not been checked yet, from your save and from "
                  "the room. Use <b>!hint</b> in the Messages tab to find where your items are.");
    return MOD_OK;
}

ModResult UpdateLocationsTab(ModContext*, void*, ModError*) {
    if (game::SaveActive() && game::CheckedLocationCount() != s_locations.shownChecked) {
        s_locations.shownChecked = game::CheckedLocationCount();
        RefreshLocations();
    }
    return MOD_OK;
}

void OpenGameWindow(ModContext* ctx, void*) {
    s_game = GameWindow{};
    UiTabDesc tabs[3]{};
    tabs[0] = UI_TAB_DESC_INIT;
    tabs[0].title = "Connection";
    tabs[0].build = BuildConnectionTab;
    tabs[0].update = UpdateConnectionTab;
    tabs[1] = UI_TAB_DESC_INIT;
    tabs[1].title = "Messages";
    tabs[1].build = BuildMessagesTab;
    tabs[1].update = UpdateMessagesTab;
    tabs[2] = UI_TAB_DESC_INIT;
    tabs[2].title = "Locations";
    tabs[2].build = BuildLocationsTab;
    tabs[2].update = UpdateLocationsTab;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 3;
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {
        s_game.status = s_game.stats = s_game.log = 0;
        s_locations.list = 0;
        s_locations.count = 0;
    };
    UiWindowHandle window{};
    Ui()->window_push(ctx, &desc, &window);
}

}  // namespace

ModResult OpenNewSaveWindow(GameModeNewSaveState* state) {
    s_newSave = NewSaveWindow{};
    s_newSave.state = state;
    UiTabDesc tab = UI_TAB_DESC_INIT;
    tab.title = "Archipelago";
    tab.build = BuildConnectTab;
    tab.update = UpdateConnectTab;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = &tab;
    desc.tab_count = 1;
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {
        s_newSave.status = 0;
        if (s_newSave.state != nullptr && *s_newSave.state == GAME_MODE_STATE_PENDING) {
            *s_newSave.state = GAME_MODE_STATE_RETURN;
            game::CancelNewSave();
        }
    };
    return Ui()->window_push(mod_ctx, &desc, &s_newSave.window);
}

ModResult RegisterMenuTab() {
    UiMenuTabDesc desc = UI_MENU_TAB_DESC_INIT;
    desc.label = "Archipelago";
    desc.on_selected = OpenGameWindow;
    return Ui()->register_menu_tab(mod_ctx, &desc, &s_menuTab);
}

void UnregisterMenuTab() {
    if (s_menuTab != 0) {
        Ui()->unregister_menu_tab(mod_ctx, s_menuTab);
        s_menuTab = 0;
    }
}

ModResult BuildModsPanel(ModContext* ctx, UiElementHandle pane, void*, ModError*) {
    AddText(pane, "To play, select \"Archipelago\" from the Dusklight menu, create a new save and connect "
                  "to your Archipelago room.");
    AddText(pane, "While playing, the Archipelago tab of the pause menu shows the connection and the "
                  "room's messages.");
    AddToggle(pane, "Notifications", "Pop-ups for items sent and received, hints and chat.",
        [](ModContext*, void*, UiControlValue* out) { out->bool_value = game::ToastsEnabled(); },
        [](ModContext*, void*, const UiControlValue* value) { game::SetToastsEnabled(value->bool_value); });
    (void)ctx;
    return MOD_OK;
}

}  // namespace randomizer::archi::ui
