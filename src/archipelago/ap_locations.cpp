#include "ap_locations.hpp"

#include "../session.hpp"
#include "../stages.h"
#include "../tools.h"

#include <mods/items.h>
#include <mods/svc/log.hpp>

#include <cstdlib>
#include <cstring>

namespace randomizer::archi {
namespace {

enum CheckKind : uint64_t {
    kCheckChest = 1,
    kCheckPoe,
    kCheckFreestanding,
    kCheckGoldenWolf,
    kCheckShop,
    kCheckSky,
    kCheckBug,
};

uint64_t CheckKey(CheckKind kind, uint64_t key) {
    return (static_cast<uint64_t>(kind) << 32) | (key & 0xFFFFFFFFu);
}

}  // namespace

void LocationIndex::Clear() {
    mLocations.clear();
    mByKey.clear();
    mByName.clear();
}

void LocationIndex::Build(const SlotData& slot) {
    Clear();
    std::unordered_map<std::string, YAML::Node> metadata;
    auto tree = LOAD_EMBED_YAML(RANDO_DATA_PATH "locations.yaml");
    for (const auto& node : tree) {
        metadata[node["Name"].as<std::string>()] = node["Metadata"];
    }

    for (const auto& ap : slot.locations) {
        TrackedLocation tracked;
        tracked.ap = &ap;
        const size_t index = mLocations.size();
        mByName[ap.name] = index;
        auto found = metadata.find(ap.name);
        if (found == metadata.end() || !found->second.IsMap()) {
            mods::log::warn("Archipelago: no metadata for location {}", ap.name);
            mLocations.push_back(tracked);
            continue;
        }
        YAML::Node meta = found->second;
        auto stageOf = [](const YAML::Node& node) { return getStageSaveId(node["Stage"].as<int>()); };

        // Save flag, with the same precedence as the tracker (isLocationMetadataObtained)
        if (auto chest = meta["Chest"]) {
            tracked.kind = FlagKind::Tbox;
            tracked.stage = stageOf(chest[0]);
            tracked.flag = chest[0]["Tbox Id"].as<uint16_t>();
        } else if (auto poe = meta["Poe"]) {
            tracked.kind = FlagKind::Switch;
            tracked.stage = stageOf(poe[0]);
            tracked.flag = poe[0]["Flag"].as<uint16_t>();
        } else if (auto item = meta["Freestanding Item"]) {
            // The Big Baba key is a chest-like item that uses a treasure box flag
            tracked.kind = ap.name == "Forest Temple Big Baba Key" ? FlagKind::Tbox : FlagKind::Item;
            tracked.stage = stageOf(item[0]);
            tracked.flag = item[0]["Flag"].as<uint16_t>();
        } else if (auto event = meta["Event Flag"]) {
            tracked.kind = FlagKind::Event;
            tracked.flag = event.as<uint16_t>();
        } else if (auto wolf = meta["Golden Wolf"]) {
            tracked.kind = FlagKind::Event;
            tracked.flag = wolf[0]["Flag"].as<uint16_t>();
        } else if (auto sw = meta["Switch Flag"]) {
            tracked.kind = FlagKind::Switch;
            tracked.stage = stageOf(sw);
            tracked.flag = sw["Flag"].as<uint16_t>();
        } else if (auto itemFlag = meta["Item Flag"]) {
            tracked.kind = FlagKind::Item;
            tracked.stage = stageOf(itemFlag);
            tracked.flag = itemFlag["Flag"].as<uint16_t>();
        } else if (auto insect = meta["Twilit Insect"]) {
            tracked.kind = FlagKind::Tbox;
            tracked.stage = stageOf(insect[0]);
            tracked.flag = insect[0]["Flag"].as<uint16_t>();
        }
        if (tracked.kind != FlagKind::Event && tracked.kind != FlagKind::None &&
            (tracked.stage < 0 || tracked.stage == 0xFF))
        {
            mods::log::warn("Archipelago: location {} has no save table", ap.name);
            tracked.kind = FlagKind::None;
        }

        // Check names that give this location's item (the keys the seed's overrides use)
        auto stageKey = [](const YAML::Node& node, const char* field) {
            return (node["Stage"].as<uint64_t>() << 8) | (node[field].as<uint64_t>() & 0xFF);
        };
        for (const auto& chest : meta["Chest"]) {
            mByKey[CheckKey(kCheckChest, stageKey(chest, "Tbox Id"))] = index;
        }
        for (const auto& poe : meta["Poe"]) {
            mByKey[CheckKey(kCheckPoe, stageKey(poe, "Flag"))] = index;
        }
        for (const auto& item : meta["Freestanding Item"]) {
            mByKey[CheckKey(kCheckFreestanding, stageKey(item, "Flag"))] = index;
        }
        for (const auto& wolf : meta["Golden Wolf"]) {
            mByKey[CheckKey(kCheckGoldenWolf, wolf["Flag"].as<uint64_t>())] = index;
        }
        for (const auto& shop : meta["Shop"]) {
            const uint64_t key = (shop["Stage"].as<uint64_t>() << 16) | (shop["Room"].as<uint64_t>() << 8) |
                                 (shop["Item"].as<uint64_t>() & 0xFF);
            mByKey[CheckKey(kCheckShop, key)] = index;
        }
        for (const auto& sky : meta["Sky Character"]) {
            mByKey[CheckKey(kCheckSky, stageKey(sky, "Room"))] = index;
        }
        for (const auto& bug : meta["Bug Reward"]) {
            mByKey[CheckKey(kCheckBug, bug["Item Id"].as<uint64_t>() & 0xFF)] = index;
        }
        for (const auto& lookup : meta["Name Lookup"]) {
            mByName[nameLookupOverride(lookup.as<std::string>())] = index;
        }
        mLocations.push_back(tracked);
    }
}

std::optional<size_t> LocationIndex::ForCheck(const char* name) const {
    if (name == nullptr) {
        return std::nullopt;
    }
    auto byKey = [this](CheckKind kind, uint64_t key) -> std::optional<size_t> {
        const auto it = mByKey.find(CheckKey(kind, key));
        return it == mByKey.end() ? std::nullopt : std::optional{it->second};
    };
    if (const auto it = mByName.find(name); it != mByName.end()) {
        return it->second;
    }
    if (auto key = session::parse_derived(name, ITEM_CHECK_CHEST_PREFIX)) {
        return byKey(kCheckChest, key->key);
    }
    if (auto key = session::parse_derived(name, ITEM_CHECK_FREESTANDING_PREFIX)) {
        // The Gale Boomerang falls from Ook as a freestanding item (session.cpp resolve_check)
        if (key->stage_id == Ook) {
            if (const auto it = mByName.find("Forest Temple Gale Boomerang"); it != mByName.end()) {
                return it->second;
            }
        }
        return byKey(kCheckFreestanding, key->key);
    }
    if (auto flag = session::parse_flag_check(name, ITEM_CHECK_GOLDEN_WOLF_PREFIX)) {
        return byKey(kCheckGoldenWolf, *flag);
    }
    if (auto key = session::parse_derived(name, ITEM_CHECK_POE_PREFIX)) {
        return byKey(kCheckPoe, key->key);
    }
    if (auto stage = session::parse_stage_check(name, ITEM_CHECK_BOSS_PREFIX)) {
        return byKey(kCheckFreestanding, (static_cast<uint64_t>(*stage) << 8) | 0x9F);
    }
    if (auto key = session::parse_shop_check(name, ITEM_CHECK_SHOP_PREFIX)) {
        return byKey(kCheckShop, *key);
    }
    if (auto key = session::parse_derived(name, ITEM_CHECK_SKY_PREFIX)) {
        return byKey(kCheckSky, key->key);
    }
    constexpr std::string_view bugPrefix{ITEM_CHECK_BUG_PREFIX};
    if (std::strncmp(name, bugPrefix.data(), bugPrefix.size()) == 0) {
        return byKey(kCheckBug, static_cast<uint64_t>(std::atoi(name + bugPrefix.size())) & 0xFF);
    }
    return std::nullopt;
}

}  // namespace randomizer::archi
