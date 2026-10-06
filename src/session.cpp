#include "session.hpp"

#include <mods/svc/log.hpp>

#include "randomizer_context.hpp"
#include "hooks.hpp"
#include "ui/ui.hpp"
#include "ui/rando_config.hpp"
#include "flags.h"
#include "item_ids.h"
#include "tools.h"
#include "stages.h"
#include "item.hpp"
#include "messages.hpp"
#include "verify_item_functions.h"
#include "archipelago/ap_game.hpp"
#include "archipelago/ap_seed.hpp"
#include "archipelago/ap_ui.hpp"
#include "../generator/utility/text.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_meter2_info.h"

#include <mods/items.h>

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <thread>

namespace randomizer::session {
ServiceManager svc_mng;
std::string g_pending_seed_hash{};
bool g_seedActivated = false;

SaveObserverHandle s_save_observer{};
ItemCheckHandle s_check_resolver{};
ItemGiveHandle s_check_observer{};
std::vector<StageActorHandle> s_stage_edits{};

constexpr const char* kSeedHashBlobName = "seed_hash";

bool set_resolution(
    const ItemCheckInfo* info, ItemCheckResolution* outResult, uint8_t resolvedItem) {
    outResult->item = resolvedItem;
    if (resolvedItem == dItemNo_Randomizer_FOOLISH_ITEM_e) {
        outResult->display_item = randomizer_getRandomFoolishItemModelID(info->name);
    }
    return true;
}

template <typename Map, typename Key>
bool lookup_override(
    const Map& map, Key key, const ItemCheckInfo* info, ItemCheckResolution* outResult) {
    const auto it = map.find(key);
    if (it == map.end()) {
        return false;
    }
    return set_resolution(info, outResult, static_cast<uint8_t>(verifyProgressiveItem(it->second)));
}

bool resolve_seed_check(const ItemCheckInfo* info, ItemCheckResolution* outResult) {
    auto& ctx = randomizer_GetContext();

    if (auto it = ctx.mItemLocations.find(info->name); it != ctx.mItemLocations.end()) {
        return set_resolution(
            info, outResult, static_cast<uint8_t>(verifyProgressiveItem(it->second.itemId)));
    }

    if (auto key = parse_derived(info->name, ITEM_CHECK_CHEST_PREFIX)) {
        return lookup_override(ctx.mTreasureChestOverrides, key->key, info, outResult);
    }
    if (auto key = parse_derived(info->name, ITEM_CHECK_FREESTANDING_PREFIX)) {
        if (key->stage_id == Ook && info->vanilla_item == dItemNo_BOOMERANG_e) {
            if (auto it = ctx.mItemLocations.find("Forest Temple Gale Boomerang");
                it != ctx.mItemLocations.end()) {
                return set_resolution(info, outResult,
                    static_cast<uint8_t>(verifyProgressiveItem(it->second.itemId)));
            }
            return false;
        }
        return lookup_override(ctx.mFreestandingItemOverrides, key->key, info, outResult);
    }
    if (auto flag = parse_flag_check(info->name, ITEM_CHECK_GOLDEN_WOLF_PREFIX)) {
        return lookup_override(ctx.mGoldenWolfOverrides, *flag, info, outResult);
    }
    if (auto key = parse_derived(info->name, ITEM_CHECK_POE_PREFIX)) {
        return lookup_override(ctx.mPoeOverrides, key->key, info, outResult);
    }
    if (auto stageId = parse_stage_check(info->name, ITEM_CHECK_BOSS_PREFIX)) {
        const u16 key = static_cast<u16>((*stageId << 8) | 0x9F);
        return lookup_override(ctx.mFreestandingItemOverrides, key, info, outResult);
    }
    if (auto key = parse_shop_check(info->name, ITEM_CHECK_SHOP_PREFIX)) {
        return lookup_override(ctx.mShopOverrides, *key, info, outResult);
    }
    if (auto key = parse_derived(info->name, ITEM_CHECK_SKY_PREFIX)) {
        return lookup_override(ctx.mSkyCharacterOverrides, key->key, info, outResult);
    }

    constexpr std::string_view bugPrefix{ITEM_CHECK_BUG_PREFIX};
    if (std::strncmp(info->name, bugPrefix.data(), bugPrefix.size()) == 0) {
        const u8 insect = static_cast<u8>(std::atoi(info->name + bugPrefix.size()));
        if (auto it = ctx.mBugRewardOverrides.find(insect); it != ctx.mBugRewardOverrides.end()) {
            return set_resolution(
                info, outResult, static_cast<uint8_t>(verifyProgressiveItem(it->second)));
        }
        return false;
    }

    return false;
}

bool resolve_check(ModContext*, const ItemCheckInfo* info, ItemCheckResolution* outResult, void*) {
    // Items from other Archipelago worlds
    if (archi::game::ResolveCheck(info, outResult)) {
        return true;
    }
    const bool resolved = resolve_seed_check(info, outResult);
    if (resolved) {
        // An item for another world: remember where it came from for the get-item text
        archi::game::NoteResolution(info, outResult->item);
    }
    return resolved;
}

void observe_give(ModContext*, const ItemGiveInfo* info, void*) {
    if (info->check_name == nullptr) {
        return;
    }
    archi::game::ObserveGive(info);

    auto& ctx = randomizer_GetContext();
    if (const auto it = ctx.mItemLocations.find(info->check_name);
        it != ctx.mItemLocations.end())
    {
        randomizer_setTempFlag(it->second);
    }

    if (auto key = parse_derived(info->check_name, ITEM_CHECK_FREESTANDING_PREFIX);
        key && key->stage_id == Ook)
    {
        if (const auto it = ctx.mItemLocations.find("Forest Temple Gale Boomerang");
            it != ctx.mItemLocations.end())
        {
            randomizer_setTempFlag(it->second);
        }
    }
}

bool activateSeed(const char* hash) {
    auto& ctx = randomizer_GetContext();
    ctx = RandomizerContext();
    if (auto err = ctx.LoadFromHash(hash); err.has_value() || ctx.mHash.empty()) {
        mods::log::error("failed to load seed {}", hash);
        return false;
    }

    if (messages::activate(ctx) != MOD_OK) {
        ctx = RandomizerContext{};
        return false;
    }

    item::apply_item_data_tables();

    svc_mng.item->set_check_resolver(mod_ctx, nullptr, resolve_check, nullptr, &s_check_resolver);
    svc_mng.item->observe_gives(mod_ctx, observe_give, nullptr, &s_check_observer);

    registerStageEdits();
    mods::log::info("activated seed {}", ctx.mHash);
    g_seedActivated = true;
    return true;
}

void deactivateSeed() {
    if (s_check_resolver != 0) {
        svc_mng.item->clear_check_resolver(mod_ctx, s_check_resolver);
        s_check_resolver = 0;
    }

    if (s_check_observer != 0) {
        svc_mng.item->unobserve_gives(mod_ctx, s_check_observer);
        s_check_observer = 0;
    }

    for (auto handle : s_stage_edits) {
        svc_mng.stage->remove_actor_edit(mod_ctx, handle);
    }
    s_stage_edits.clear();

    messages::deactivate();
    item::restore_item_data_tables();
    randomizer_GetContext() = RandomizerContext{};
    g_randomizerState = RandomizerState{};
    g_seedActivated = false;
}

void setupRandomizerFile() {
    // Setup file based on randomizer data
    auto& randoData = randomizer_GetContext();
    randoData.mCreatingSave = true;

    // Set starting flags
    // Event Flags
    for (const auto& flag : randoData.mStartEventFlags) {
        dComIfGs_onEventBit(flag);
    }
    // Region Flags
    for (const auto& [region, flags] : randoData.mStartRegionFlags) {
        for (const auto& flag : flags) {
            onRegionFlag(region, flag);
        }
    }

    // Map bits (fills in overworld on map)
    setRegionBit(randoData.mMapBits);

    // Other flags based on starting flags
    if (dComIfGs_isEventBit(CLEARED_FARON_TWILIGHT))
    {
        dComIfGs_onDarkClearLV(0);
        dComIfGs_setLightDropNum(0, 0x10);
        item::exec_item_get(dItemNo_Randomizer_DROP_CONTAINER_e);
        item::exec_item_get(dItemNo_Randomizer_WEAR_KOKIRI_e);
    }

    if (dComIfGs_isEventBit(CLEARED_ELDIN_TWILIGHT))
    {
        dComIfGs_onDarkClearLV(1);
        dComIfGs_setLightDropNum(1, 0x10);
        item::exec_item_get(dItemNo_Randomizer_DROP_CONTAINER02_e);
    }

    if (dComIfGs_isEventBit(CLEARED_LANAYRU_TWILIGHT))
    {
        dComIfGs_onDarkClearLV(2);
        dComIfGs_setLightDropNum(2, 0x10);
        item::exec_item_get(dItemNo_Randomizer_DROP_CONTAINER03_e);
    }

    if (randoData.mSettings[RandomizerContext::SKIP_MINOR_CUTSCENES] == RandomizerContext::ON)
    {
        // Add letter data in this order to more or less reflect an order they can be obtained in game
        static const int letterOrder[] = {3, 2, 4, 7, 5, 6, 13, 12, 10, 9, 8, 15, 0, 14, 11};
        int letterNum = 0;
        for (int i : letterOrder) {
            if (dMenu_Letter::getLetterName(i) != 0) {
                dComIfGs_onLetterGetFlag(i);
                dComIfGs_setGetNumber(letterNum++, i + 1);
            }
        }
        setAllLetterRead();
    }

    // If MDH and the twilights are pre-completed
    if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED))
    {
        if ((dComIfGs_getSaveData()->getPlayer().getPlayerStatusB().mDarkClearLevelFlag & 0x7) == 0x7)
        {
            dComIfGs_onDarkClearLV(3);
            dComIfGs_onTransformLV(3); // Puts Midna on players back
        }
    }

    // Set starting inventory
    for (const auto& itemId: randoData.mStartingInventory) {
        item::exec_item_get(itemId);
    }

    g_randomizerState = RandomizerState();
    mods::log::debug("Created Rando Save");
    randoData.mCreatingSave = false;
}

void registerStageEdits() {
    auto& ctx = randomizer_GetContext();
    auto stage_of = [](u32 key) -> const char* {
        const u32 stage_id = key >> 16;
        if (stage_id >= sizeof(allStages) / sizeof(allStages[0])) {
            return nullptr;
        }
        return allStages[stage_id];
    };

    for (const auto& [key, patches] : ctx.mObjectPatches) {
        const char* stage = stage_of(key);
        if (stage == nullptr) {
            continue;
        }

        const u8 room = (key >> 8) & 0xFF;
        const s8 layer = static_cast<s8>(key & 0xFF);
        for (const auto& [crc, actor] : patches) {
            StageActorHandle handle{};

            ModResult res;
            if (actor.bytes.size() == RandomizerContext::OBJ_DELETE_SIZE) {
                res = svc_mng.stage->delete_actor(mod_ctx, stage, room, layer, crc, &handle);
            } else {
                res = svc_mng.stage->patch_actor(mod_ctx, stage, room, layer, crc,
                    actor.bytes.data(), actor.bytes.size(), &handle);
            }

            if (res == MOD_OK) {
                s_stage_edits.push_back(handle);
            }
        }
    }

    for (const auto& [key, additions] : ctx.mObjectAdditions) {
        const char* stage = stage_of(key);
        if (stage == nullptr) {
            continue;
        }

        const u8 room = (key >> 8) & 0xFF;
        const s8 layer = static_cast<s8>(key & 0xFF);
        for (const auto& actor : additions) {
            StageActorHandle handle{};
            ModResult rt;
            rt = svc_mng.stage->add_actor(mod_ctx, stage, room, layer, actor.bytes.data(),
                actor.bytes.size(), &handle);
            if (rt == MOD_OK) {
                s_stage_edits.push_back(handle);
            }
        }
    }
}

ModResult onNewSave(void*, ModError* error) {
    const std::string hash = archi::game::PendingSeedHash();
    if (hash.empty())
        return mods::set_error(error, MOD_ERROR, "no Archipelago seed was prepared for this save");

    deactivateSeed();
    if (!activateSeed(hash.c_str()))
        return mods::set_error(error, MOD_ERROR, "failed to activate the Archipelago seed");

    svc_mng.save->set_blob(svc_mng.mod_ctx, kSeedHashBlobName, hash.data(), hash.size());
    setAncientDocumentNum(0);
    setupRandomizerFile();
    saveAncientDocumentNum();
    return archi::game::OnNewSave();
}

ModResult onSaveLoaded(void*, ModError*) {
    size_t size = 0;
    if (svc_mng.save->get_blob(mod_ctx, kSeedHashBlobName, nullptr, &size) != MOD_OK || size == 0) {
        mods::log::error("seed_hash not found!");
        deactivateSeed();
        return MOD_ERROR;
    }

    std::string hash(size, '\0');
    if (svc_mng.save->get_blob(mod_ctx, kSeedHashBlobName, hash.data(), &size) != MOD_OK) {
        mods::log::error("failed to get seed_hash!");
        deactivateSeed();
        return MOD_ERROR;
    }

    // Rebuilds the seed from the Archipelago slot data if its files are missing or obsolete
    const std::string activeHash = archi::game::PrepareSeed(hash);
    if (activeHash != hash) {
        svc_mng.save->set_blob(svc_mng.mod_ctx, kSeedHashBlobName, activeHash.data(), activeHash.size());
    }

    if (randomizer_GetContext().mHash != activeHash) {
        deactivateSeed();
        activateSeed(activeHash.c_str());
    }

    loadAncientDocumentNum();
    return archi::game::OnSaveLoaded();
}

void onSaveWritten(ModContext*, uint32_t, void*) {
    const std::string hash = randomizer_GetContext().mHash;
    svc_mng.save->set_blob(svc_mng.mod_ctx, kSeedHashBlobName, hash.data(), hash.size());
    saveAncientDocumentNum();
    archi::game::OnSaveWritten();
}

void preLoadRandomizerData() {
    // Load text database now so that we don't hitch when opening up the item wheel the first time
    getTextDatabase();

    // Verify current seeds now so we don't hitch when getting seeds in the future
    ui::get_compatible_seed_hashes();

    // Load the excluded locations catalog for the excluded locations menu
    ui::load_excluded_locations();
}

TextureReplacementHandle logoTexHandle{};

ModResult onGameModeActivated(void*, ModError* error) {
    ModResult result = hooks::initialize();
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to initialize hooks");
    }

    result = svc_mng.save->observe_saves(
        svc_mng.mod_ctx,
        nullptr,
        nullptr,
        onSaveWritten,
        nullptr,
        &s_save_observer);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to initialize save observation");
    }

    result = svc_mng.texture->register_file(mod_ctx, "res/tex1_608x100_0c1c70378fb8cb46_6.png", &logoTexHandle);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to register texture replacement");
    }

    result = ui::initialize();
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to initialize ui");
    }

    result = archi::game::OnActivated();
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to initialize Archipelago");
    }

    // Preload certain data to prevent hitching that would happen if loading the data as necessary
    std::thread preLoadDataThread{preLoadRandomizerData};
    preLoadDataThread.detach();

    mods::log::info("Archipelago game mode activated");
    return MOD_OK;
}

ModResult onNewSaveSelect(void* user_data, GameModeNewSaveState* state, ModError* out_error) {
    ModResult rt = archi::game::OnNewSaveSelect(state);
    if (rt != MOD_OK) {
        return mods::set_error(out_error, rt, "Failed to build menu");
    }

    return MOD_OK;
}

ModResult onGameReset(void*, ModError*) {
    archi::game::OnGameReset();
    return MOD_OK;
}

void shutdown() {
    archi::game::OnDeactivated();
    deactivateSeed();
    hooks::uninstall();
    ui::shutdown();
    svc_mng.save->unobserve_saves(mod_ctx, s_save_observer);
    svc_mng.texture->unregister(mod_ctx, logoTexHandle);
}

ModResult onGameModeDeactivated(void*, ModError*) {
    shutdown();

    mods::log::info("Archipelago game mode deactivated");
    return MOD_OK;
}

ModResult onGameModeUpdate(void*, ModError*) {
    ui::update();
    session::update();
    archi::game::Tick();
    return MOD_OK;
}

ModResult initialize(const ServiceManager& services) {
    svc_mng = services;

    auto result = messages::initialize();
    if (result != MOD_OK) {
        return result;
    }

    result = archi::game::Initialize();
    if (result != MOD_OK) {
        return result;
    }

    // Its own game mode and save files, so it can be installed next to the randomizer mod
    constexpr GameModeDesc gameModeDesc{
        .struct_size = sizeof(GameModeDesc),
        .game_mode_id = "archipelago",
        .full_name = "Archipelago",
        .save_name = "tpd-archipelago",
        .user_data = nullptr,
        .on_activated = onGameModeActivated,
        .on_deactivated = onGameModeDeactivated,
        .on_save_loaded = onSaveLoaded,
        .on_new_save = onNewSave,
        .on_new_save_select = onNewSaveSelect,
        .on_game_reset = onGameReset,
        .on_tick = onGameModeUpdate,
    };
    result = svc_game_mode->register_game_mode(mod_ctx, &gameModeDesc);
    if (result != MOD_OK) {
        return result;
    }

    UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
    panelDesc.build = archi::ui::BuildModsPanel;
    result = svc_ui->register_mods_panel(mod_ctx, &panelDesc);
    return result;
}

void update() {
    if (!g_randomizerState.mInitialized) {
        g_randomizerState._create();
    }
    g_randomizerState.execute();
}

}
