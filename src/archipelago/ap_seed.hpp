#pragma once

// Turning an Archipelago slot's data into a Dusklight randomizer seed.
//
// The APWorld sends the randomizer settings and the item at every Archipelago location in
// slot_data. This writes those as a settings file and a plandomizer file and runs the
// randomizer's own generator on them, so every patch, flag and text the randomizer normally
// produces is still produced; only the item placements come from the multiworld.
//
// No Dusklight services are used here, so this also builds into the stand-alone ap_tool.

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace randomizer {
class Randomizer;
}

namespace randomizer::archi {

inline constexpr int kSlotDataVersion = 1;
inline constexpr const char* kArchipelagoItemName = "Archipelago Item";
inline constexpr unsigned char kArchipelagoItemId = 0xDC;

enum ItemFlags : int {
    kItemProgression = 1,
    kItemUseful = 2,
    kItemTrap = 4,
};

struct ApLocation {
    std::string name;       // randomizer location name
    int64_t id = 0;         // Archipelago location id
    std::string owner;      // player the item belongs to
    std::string item;       // item name (a randomizer item name when own is true)
    int flags = 0;          // ItemFlags
    bool own = false;       // the item belongs to this slot (and this game)
};

struct SlotData {
    int version = 0;
    std::string apworldVersion;
    std::string randomizerData;
    std::string seed;
    int64_t itemIdBase = 0;
    bool deathLink = false;
    std::vector<std::pair<std::string, std::string>> settings;
    std::vector<std::string> requiredDungeons;
    std::vector<ApLocation> locations;

    // Lookups built by Parse()
    std::unordered_map<std::string, size_t> byName;
    std::unordered_map<int64_t, size_t> byId;

    static std::optional<SlotData> Parse(const nlohmann::json& json, std::string& error);
    const ApLocation* FindByName(const std::string& name) const;
    const ApLocation* FindById(int64_t id) const;
};

/// Text shown in game for an item that belongs to someone else: "Bob's Moon Pearl".
/// Characters the game's text engine can't show (or treats as markup) are dropped.
std::string ForeignItemText(const ApLocation& location);
std::string SanitizeForGameText(const std::string& text, size_t maxLength = 60);

/// Writes settings.yaml, preferences.yaml and plando.yaml for the slot into dir.
void WriteGenerationFiles(const std::filesystem::path& dir, const SlotData& slot);

/// Runs the randomizer generator for the slot. baseDir is the randomizer data directory
/// (seeds/ and logs go there as for normal seeds); generation inputs go to workDir.
/// On success, returns nullopt and leaves the generated worlds in `randomizer`.
std::optional<std::string> GenerateWorlds(Randomizer& randomizer, const std::filesystem::path& workDir,
    const SlotData& slot);

/// Checks that the generated world holds exactly the multiworld's placements.
/// Returns a list of problems (empty when everything matches).
std::vector<std::string> VerifyPlacements(Randomizer& randomizer, const SlotData& slot);

}  // namespace randomizer::archi
