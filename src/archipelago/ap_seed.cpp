#include "ap_seed.hpp"

#include "../../generator/logic/world.hpp"
#include "../../generator/randomizer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace randomizer::archi {

std::optional<SlotData> SlotData::Parse(const nlohmann::json& json, std::string& error) {
    try {
        SlotData slot{};
        slot.version = json.at("slot_data_version").get<int>();
        if (slot.version != kSlotDataVersion) {
            error = "This seed was generated with a different version of the Twilight Princess Dusklight "
                    "APWorld (slot data version " + std::to_string(slot.version) + ", this mod reads version " +
                    std::to_string(kSlotDataVersion) + "). Use matching versions of the mod and the APWorld.";
            return std::nullopt;
        }
        slot.apworldVersion = json.value("apworld_version", "");
        slot.randomizerData = json.value("randomizer_data", "");
        slot.seed = json.at("seed").get<std::string>();
        // Used in file names: only what the APWorld produces ("AP" + hex digits)
        const bool seedOk = !slot.seed.empty() && slot.seed.size() <= 64 &&
                            std::ranges::all_of(slot.seed, [](unsigned char c) {
                                return std::isalnum(c) || c == '-' || c == '_';
                            });
        if (!seedOk) {
            error = "The Archipelago slot data has an invalid seed name.";
            return std::nullopt;
        }
        slot.itemIdBase = json.at("item_id_base").get<int64_t>();
        slot.deathLink = json.value("death_link", false);
        for (const auto& [name, value] : json.at("settings").items()) {
            slot.settings.emplace_back(name, value.get<std::string>());
        }
        if (json.contains("required_dungeons")) {
            slot.requiredDungeons = json.at("required_dungeons").get<std::vector<std::string>>();
        }
        for (const auto& entry : json.at("locations")) {
            ApLocation location{};
            location.name = entry.at(0).get<std::string>();
            location.id = entry.at(1).get<int64_t>();
            location.owner = entry.at(2).get<std::string>();
            location.item = entry.at(3).get<std::string>();
            location.flags = entry.at(4).get<int>();
            location.own = entry.at(5).get<int>() != 0;
            slot.byName[location.name] = slot.locations.size();
            slot.byId[location.id] = slot.locations.size();
            slot.locations.push_back(std::move(location));
        }
        return slot;
    } catch (const std::exception& e) {
        error = std::string("Could not read the Archipelago slot data: ") + e.what();
        return std::nullopt;
    }
}

const ApLocation* SlotData::FindByName(const std::string& name) const {
    const auto it = byName.find(name);
    return it == byName.end() ? nullptr : &locations[it->second];
}

const ApLocation* SlotData::FindById(int64_t id) const {
    const auto it = byId.find(id);
    return it == byId.end() ? nullptr : &locations[it->second];
}

std::string SanitizeForGameText(const std::string& text, size_t maxLength) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 0x80) {
            // Skip the rest of a UTF-8 sequence and show one placeholder for it
            while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
                ++i;
            }
            out += '?';
            continue;
        }
        if (ch < 0x20 || ch == 0x7F || ch == '{' || ch == '}' || ch == '<' || ch == '>') {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
            continue;
        }
        out += static_cast<char>(ch);
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    if (out.size() > maxLength) {
        out.resize(maxLength - 3);
        out += "...";
    }
    return out.empty() ? std::string("?") : out;
}

std::string ForeignItemText(const ApLocation& location) {
    return SanitizeForGameText(location.owner, 24) + "'s " + SanitizeForGameText(location.item);
}

static void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not write " + path.string());
    }
    file << contents;
}

void WriteGenerationFiles(const std::filesystem::path& dir, const SlotData& slot) {
    std::filesystem::create_directories(dir);
    const auto plandoPath = dir / "plando.yaml";

    YAML::Emitter settings;
    settings << YAML::BeginMap;
    settings << YAML::Key << "Seed" << YAML::Value << YAML::DoubleQuoted << slot.seed;
    for (const auto& [name, value] : slot.settings) {
        settings << YAML::Key << name << YAML::Value << YAML::DoubleQuoted << value;
    }
    settings << YAML::Key << "Plandomizer" << YAML::Value << true;
    settings << YAML::Key << "Generate Spoiler Log" << YAML::Value << false;
    settings << YAML::Key << "Starting Inventory" << YAML::Value << YAML::BeginMap << YAML::EndMap;
    settings << YAML::Key << "Excluded Locations" << YAML::Value << YAML::BeginSeq << YAML::EndSeq;
    settings << YAML::Key << "Mixed Entrance Pools" << YAML::Value << YAML::BeginSeq << YAML::EndSeq;
    settings << YAML::Key << "Archipelago" << YAML::Value << YAML::BeginMap;
    settings << YAML::Key << "Enabled" << YAML::Value << true;
    settings << YAML::Key << "Required Dungeons" << YAML::Value << YAML::BeginSeq;
    for (const auto& dungeon : slot.requiredDungeons) {
        settings << YAML::DoubleQuoted << dungeon;
    }
    settings << YAML::EndSeq;
    settings << YAML::Key << "Item Text" << YAML::Value << YAML::BeginMap;
    for (const auto& location : slot.locations) {
        if (!location.own) {
            settings << YAML::Key << location.name << YAML::Value << YAML::DoubleQuoted
                     << ForeignItemText(location);
        }
    }
    settings << YAML::EndMap << YAML::EndMap << YAML::EndMap;
    WriteFile(dir / "settings.yaml", std::string(settings.c_str()) + "\n");

    YAML::Emitter preferences;
    const auto plandoText = plandoPath.generic_u8string();  // read back as UTF-8 (config.cpp)
    preferences << YAML::BeginMap << YAML::Key << "Plandomizer Path" << YAML::Value << YAML::DoubleQuoted
                << std::string(reinterpret_cast<const char*>(plandoText.data()), plandoText.size())
                << YAML::EndMap;
    WriteFile(dir / "preferences.yaml", std::string(preferences.c_str()) + "\n");

    YAML::Emitter plando;
    plando << YAML::BeginMap << YAML::Key << "World 1" << YAML::Value << YAML::BeginMap;
    plando << YAML::Key << "Locations" << YAML::Value << YAML::BeginMap;
    for (const auto& location : slot.locations) {
        plando << YAML::Key << location.name << YAML::Value << YAML::DoubleQuoted
               << (location.own ? location.item : std::string(kArchipelagoItemName));
    }
    plando << YAML::EndMap << YAML::EndMap << YAML::EndMap;
    WriteFile(plandoPath, std::string(plando.c_str()) + "\n");
}

std::optional<std::string> GenerateWorlds(Randomizer& randomizer, const std::filesystem::path& workDir,
    const SlotData& slot) {
    try {
        WriteGenerationFiles(workDir, slot);
    } catch (const std::exception& e) {
        return std::string(e.what());
    }
    randomizer.SetConfigPaths(workDir / "settings.yaml", workDir / "preferences.yaml");
    return randomizer.Generate();
}

static bool IsBottle(const std::string& name) {
    return name == "Empty Bottle" || name.starts_with("Bottle");
}

std::vector<std::string> VerifyPlacements(Randomizer& randomizer, const SlotData& slot) {
    std::vector<std::string> problems;
    auto* world = randomizer.GetWorld();
    if (world == nullptr) {
        problems.emplace_back("no world was generated");
        return problems;
    }
    for (const auto& apLocation : slot.locations) {
        try {
            auto* location = world->GetLocation(apLocation.name);
            const auto actual = location->GetCurrentItem()->GetName();
            const auto expected = apLocation.own ? apLocation.item : std::string(kArchipelagoItemName);
            const bool bottleOk = IsBottle(expected) && IsBottle(actual);
            if (actual != expected && !bottleOk) {
                problems.push_back(apLocation.name + ": expected " + expected + ", got " + actual);
            }
        } catch (const std::exception& e) {
            problems.push_back(apLocation.name + ": " + e.what());
        }
    }
    for (auto* location : world->GetAllLocations()) {
        if (location->IsEmpty()) {
            problems.push_back(location->GetName() + " is empty");
        }
    }
    return problems;
}

}  // namespace randomizer::archi
