#pragma once

// The Archipelago game mode: ties the network client to the running game.
//
//  - New save: connect to the room, generate the randomizer seed from the slot data
//    (ap_seed.hpp), then create the save.
//  - In game: report checked locations (save flags + item give observation), give items from
//    other worlds through Dusklight's item queue, report the goal, DeathLink, notifications.
//  - Per-save state (server, slot, how many items were applied...) lives in a save blob written
//    with the game's own saves, so a reloaded save and its received items always agree.

#include "ap_client.hpp"

#include <mods/svc/game_mode.h>
#include <mods/svc/item.h>

#include <deque>
#include <string>
#include <vector>

namespace randomizer::archi::game {

// ---- Session hooks (src/session.cpp) ----
ModResult Initialize();
ModResult OnActivated();
void OnDeactivated();
ModResult OnNewSaveSelect(GameModeNewSaveState* state);
/// Seed hash generated for the save being created (empty if none).
const std::string& PendingSeedHash();
ModResult OnNewSave();
/// Before the randomizer seed is activated on load: makes sure its files exist, rebuilding them
/// from the cached slot data if they are missing or obsolete. Returns the hash to activate.
std::string PrepareSeed(const std::string& seedHash);
ModResult OnSaveLoaded();
/// The game copied its save data for writing (dComIfGs_setMemoryToCard).
void OnSaveCaptured();
void OnSaveWritten();
void OnGameReset();
void Tick();

/// Check resolution for items given by the Archipelago client ("ap:recv:<n>").
bool ResolveCheck(const ItemCheckInfo* info, ItemCheckResolution* outResult);
/// Every resolved check, to know which location a get-item message is about.
void NoteResolution(const ItemCheckInfo* info, uint8_t item);
/// For the get-item fanfare of another player's item.
bool ForeignItemIsProgression();
/// A registered get-item message for another player's item, if the game has no message 321 (else 0).
uint16_t ForeignFallbackMessageId();
void ObserveGive(const ItemGiveInfo* info);

// ---- For the UI (ap_ui.cpp) ----
struct ConnectionForm {
    std::string server;
    std::string slot;
    std::string password;
};

enum class NewSavePhase { Idle, Connecting, Generating, Ready, Error };

ConnectionForm& NewSaveForm();
NewSavePhase GetNewSavePhase();
const std::string& NewSaveMessage();
void StartNewSaveConnection();
void CancelNewSave();

/// The connection settings of the loaded save (edited from the in-game menu).
ConnectionForm& SaveForm();
bool SaveActive();
void ConnectSave();
void DisconnectSave();

ApClient* Client();
std::string StatusText();
const std::deque<std::string>& MessageLog();
uint64_t MessageLogVersion();
void Say(const std::string& text);

bool DeathLinkEnabled();
void SetDeathLinkEnabled(bool enabled);
bool ToastsEnabled();
void SetToastsEnabled(bool enabled);

size_t LocationCount();
/// Names of this world's Archipelago locations that are not checked yet.
std::vector<std::string> RemainingLocations();
size_t CheckedLocationCount();
size_t ReceivedItemCount();

std::string EscapeRml(const std::string& text);

}  // namespace randomizer::archi::game
