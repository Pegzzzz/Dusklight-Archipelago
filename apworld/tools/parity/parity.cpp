// Logic parity harness: builds a world exactly like Randomizer::GenerateWorlds does (up to the
// fill), then answers accessibility queries so the APWorld's Python port of the logic can be
// compared against the C++ generator.
//
// Usage: ap_parity_harness <dir containing settings.yaml>
//   stdout line 1: "POOL\t<item>*<count>|..."      remaining item pool (after vanilla placement)
//   stdout line 2: "START\t<item>*<count>|..."     starting inventory
//   stdout line 3: "VANILLA\t<location>=<item>|..." locations with a known item before the fill
//   then for every stdin line "<item>*<count>|..." (assumed owned items, starting inventory is
//   added automatically): "REACH\t<location>|..." sorted reachable locations.

#include "../../../generator/randomizer.hpp"
#include "../../../generator/logic/fill.hpp"
#include "../../../generator/logic/item_pool.hpp"
#include "../../../generator/logic/search.hpp"
#include "../../../generator/logic/entrance_shuffle.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace randomizer;

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim)) {
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

static std::string join_counts(const logic::item_pool::ItemPool& items) {
    std::map<std::string, int> counts;
    for (auto* item : items) counts[item->GetName()]++;
    std::string out;
    for (auto& [name, count] : counts) {
        if (!out.empty()) out += "|";
        out += name + "*" + std::to_string(count);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ap_parity_harness <dir>" << std::endl;
        return 2;
    }
    try {
        Randomizer r{argv[1]};
        auto& config = r.GetConfig();
        config.LoadFromFile(r.GetConfigPath(), r.GetPrefPath());
        seedgen::config::SeedRNG(config, true, false);

        auto& worlds = r.GetWorlds();
        int worldId = 1;
        for (const auto& settings : config.GetSettingsList()) {
            auto world = std::make_unique<logic::world::World>(worldId++, &r);
            world->SetSettings(settings);
            world->ResolveRandomSettings();
            world->ResolveConflictingSettings();
            world->Build();
            worlds.emplace_back(std::move(world));
        }
        for (auto& world : worlds) world->PerformPreEntranceShuffleTasks();
        for (auto& world : worlds) logic::entrance_shuffle::ShuffleWorldEntrances(world.get());
        for (auto& world : worlds) world->PerformPostEntranceShuffleTasks();
        logic::fill::CacheExitTimeForms(worlds);

        auto* world = worlds.at(0).get();
        std::cout << "POOL\t" << join_counts(world->GetItemPool()) << "\n";
        std::cout << "START\t" << join_counts(world->GetStartingItemPool()) << "\n";
        std::vector<std::string> vanilla;
        for (auto* location : world->GetAllLocations(true)) {
            if (!location->IsEmpty()) {
                vanilla.push_back(location->GetName() + "=" + location->GetCurrentItem()->GetName());
            }
        }
        std::sort(vanilla.begin(), vanilla.end());
        std::cout << "VANILLA\t";
        for (size_t i = 0; i < vanilla.size(); ++i) std::cout << (i ? "|" : "") << vanilla[i];
        std::cout << std::endl;

        std::string line;
        while (std::getline(std::cin, line)) {
            logic::item_pool::ItemPool items;
            for (const auto& entry : split(line, '|')) {
                auto star = entry.rfind('*');
                auto name = entry.substr(0, star);
                int count = std::stoi(entry.substr(star + 1));
                auto* item = world->GetItem(name);
                for (int i = 0; i < count; ++i) items.push_back(item);
            }
            auto search = logic::search::Search::Accessible(&worlds, items);
            search.SearchWorlds();
            std::vector<std::string> reached;
            for (auto* location : search._visitedLocations) reached.push_back(location->GetName());
            std::sort(reached.begin(), reached.end());
            std::cout << "REACH\t";
            for (size_t i = 0; i < reached.size(); ++i) std::cout << (i ? "|" : "") << reached[i];
            std::cout << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "ERROR\t" << e.what() << std::endl;
        return 1;
    }
    return 0;
}
