#pragma once

// Archipelago windows: the connection screen when creating a save, the in-game menu tab
// (connection, DeathLink, server messages and chat) and the Mods window panel.

#include <mods/api.h>
#include <mods/svc/game_mode.h>
#include <mods/svc/ui.h>

namespace randomizer::archi::ui {

ModResult OpenNewSaveWindow(GameModeNewSaveState* state);
ModResult RegisterMenuTab();
void UnregisterMenuTab();
ModResult BuildModsPanel(ModContext* ctx, UiElementHandle pane, void* userData, ModError* error);

}  // namespace randomizer::archi::ui
