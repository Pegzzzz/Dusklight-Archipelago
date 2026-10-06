// Runs the mod's whole new-save seed pipeline on an Archipelago slot without the game:
//
//   slot data -> GenerateWorlds (plandomizer) -> WriteSeedData -> seed.dat -> LoadFromHash
//
// then reads every Archipelago location back the way the game does (getLocationItem on the
// loaded seed data) and checks it holds the multiworld's item: the item itself when it is this
// player's, the Archipelago placeholder (0xDC) otherwise. Also checks the mod's location
// tracking (ap_locations): every location has a save flag, and every item service check name
// that gives a location's item (formatted like the game does) maps back to that location.
//
//   seeddata_test <slot_data.json> <data dir>
//
// Built against the game headers, but linked without the game: the code under test must not
// touch game state (any such call crashes here, which is a finding in itself).

#include "mods/service.hpp"
#include "mods/svc/host.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

#include "../../../generator/randomizer.hpp"
#include "../../../generator/utility/text.hpp"
#include "../../../src/archipelago/ap_locations.hpp"
#include "../../../src/archipelago/ap_seed.hpp"
#include "../../../src/randomizer_context.hpp"
#include "../../../src/session.hpp"
#include "../../../src/stages.h"
#include "../../../src/tools.h"

#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <fstream>
#include <iostream>

extern "C" {
ModContext* mod_ctx = nullptr;
const LogService* svc_log = nullptr;
const HostService* svc_host = nullptr;
}

namespace {

std::string g_dataDir;

void LogWrite(ModContext*, LogLevel level, const char* message) {
    if (level >= LOG_LEVEL_WARN) {
        std::fprintf(stderr, "[log %d] %s\n", static_cast<int>(level), message);
    }
}

ModResult DataDir(ModContext*, const char** out) {
    *out = g_dataDir.c_str();
    return MOD_OK;
}

}  // namespace

namespace randomizer::session {
ServiceManager svc_mng{};
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: seeddata_test <slot_data.json> <data dir>" << std::endl;
        return 2;
    }
    g_dataDir = argv[2];
    static LogService log{};
    log.write = LogWrite;
    static HostService host{};
    host.data_dir = DataDir;
    svc_log = &log;
    svc_host = &host;
    static UiService ui{};
    ui.push_toast = [](ModContext*, const UiToastDesc*) { return MOD_OK; };
    randomizer::session::svc_mng.ui = &ui;
    randomizer::session::svc_mng.host = &host;
    randomizer::session::svc_mng.log = &log;

    std::ifstream file(argv[1]);
    std::string error;
    auto slot = randomizer::archi::SlotData::Parse(nlohmann::json::parse(file), error);
    if (!slot) {
        std::cout << "ERROR\t" << error << std::endl;
        return 1;
    }

    std::filesystem::create_directories(g_dataDir);
    randomizer::Randomizer rando{g_dataDir};
    if (auto result = randomizer::archi::GenerateWorlds(
            rando, std::filesystem::path{g_dataDir} / "archipelago" / "seeds" / slot->seed, *slot))
    {
        std::cout << "ERROR\tgeneration: " << *result << std::endl;
        return 1;
    }
    if (auto problems = randomizer::archi::VerifyPlacements(rando, *slot); !problems.empty()) {
        std::cout << "ERROR\tplacements: " << problems.front() << std::endl;
        return 1;
    }

    RandomizerContext written = WriteSeedData(rando.GetWorld());
    written.mHash = rando.GetConfig().GetHash();
    if (auto result = written.WriteToFile()) {
        std::cout << "ERROR\twrite: " << *result << std::endl;
        return 1;
    }

    auto& loaded = randomizer_GetContext();
    loaded = RandomizerContext{};
    if (auto result = loaded.LoadFromHash(written.mHash); result || loaded.mHash.empty()) {
        std::cout << "ERROR\tload: " << result.value_or("seed.dat missing") << std::endl;
        return 1;
    }

    // Read each location back as the game resolves it
    int mismatches = 0;
    int foreign = 0;
    auto* world = rando.GetWorld();
    for (const auto& ap : slot->locations) {
        auto* location = world->GetLocation(ap.name);
        const int actual = getLocationItem(location);
        int expected = randomizer::archi::kArchipelagoItemId;
        if (ap.own) {
            expected = location->GetCurrentItem()->GetID();  // VerifyPlacements checked the name
        } else {
            ++foreign;
        }
        if (actual != expected) {
            if (++mismatches <= 10) {
                std::cout << "MISMATCH\t" << ap.name << ": seed data has item " << actual << ", expected "
                          << expected << std::endl;
            }
        }
    }
    // Location tracking
    randomizer::archi::LocationIndex index;
    index.Build(*slot);
    int names = 0;
    auto expect = [&](const std::string& checkName, size_t want) {
        ++names;
        const auto got = index.ForCheck(checkName.c_str());
        if (!got || *got != want) {
            if (++mismatches <= 20) {
                std::cout << "MISMATCH\tcheck '" << checkName << "' maps to "
                          << (got ? index.Locations()[*got].ap->name : std::string("nothing")) << ", expected "
                          << index.Locations()[want].ap->name << std::endl;
            }
        }
    };
    for (size_t i = 0; i < index.Locations().size(); ++i) {
        const auto& tracked = index.Locations()[i];
        if (tracked.kind == randomizer::archi::FlagKind::None) {
            if (++mismatches <= 20) {
                std::cout << "MISMATCH\t" << tracked.ap->name << " has no save flag to detect it" << std::endl;
            }
        }
        const auto& meta = world->GetLocation(tracked.ap->name)->GetMetadata();
        auto stageName = [](const YAML::Node& node) { return std::string{allStages[node["Stage"].as<int>()]}; };
        for (const auto& chest : meta["Chest"]) {
            expect(fmt::format("chest:{}:{}", stageName(chest), chest["Tbox Id"].as<int>()), i);
        }
        for (const auto& poe : meta["Poe"]) {
            expect(fmt::format("poe:{}:{}", stageName(poe), poe["Flag"].as<int>()), i);
        }
        for (const auto& item : meta["Freestanding Item"]) {
            const int flag = item["Flag"].as<int>();
            if (flag == 0x9F) {
                expect(fmt::format("boss:{}", stageName(item)), i);
            } else if (stageName(item) != "D_MN05B") {  // Ook's arena: see ForCheck
                expect(fmt::format("freestanding:{}:{}", stageName(item), flag), i);
            }
        }
        for (const auto& wolf : meta["Golden Wolf"]) {
            expect(fmt::format("golden_wolf:{}", wolf["Flag"].as<int>()), i);
        }
        if (world->Setting("Shop Items") == "On") {
            for (const auto& shop : meta["Shop"]) {
                expect(fmt::format("shop:{}:{}:{}", stageName(shop), shop["Room"].as<int>(), shop["Item"].as<int>()), i);
            }
        }
        for (const auto& sky : meta["Sky Character"]) {
            expect(fmt::format("sky:{}:{}", stageName(sky), sky["Room"].as<int>()), i);
        }
        for (const auto& bug : meta["Bug Reward"]) {
            expect(fmt::format("bug:{}", bug["Item Id"].as<int>()), i);
        }
        for (const auto& lookup : meta["Name Lookup"]) {
            expect(nameLookupOverride(lookup.as<std::string>()), i);
        }
    }

    // Get-item texts for other players' items, built as the game builds them, in every language:
    // nothing may fall back to the error text or leave a tag unapplied
    int texts = 0;
    int dumpTexts = std::getenv("SEEDDATA_DUMP_TEXTS") ? std::atoi(std::getenv("SEEDDATA_DUMP_TEXTS")) : 0;
    auto checkText = [&](const randomizer::archi::ApLocation* ap) {
        for (const int language : {0, 1, 2, 3, 4, 6}) {
            ++texts;
            const std::string text = randomizer::archi::ForeignGetItemText(ap, language);
            if (dumpTexts > 0) {  // SEEDDATA_DUMP_TEXTS=n: show the first n texts
                --dumpTexts;
                std::cout << "TEXT\t" << language << "\t" << nlohmann::json(text).dump(-1, ' ', true,
                             nlohmann::json::error_handler_t::replace) << std::endl;
            }
            if (text.empty() || text.find("You found an item!") != std::string::npos ||
                text.find('<') != std::string::npos || text.find('>') != std::string::npos)
            {
                if (++mismatches <= 30) {
                    std::cout << "MISMATCH\tget-item text for " << (ap ? ap->name : std::string("(none)"))
                              << " in language " << language << ": " << nlohmann::json(text).dump(-1, ' ', true,
                                 nlohmann::json::error_handler_t::replace) << std::endl;
                }
            }
        }
    };
    checkText(nullptr);
    for (const auto& ap : slot->locations) {
        if (!ap.own) {
            checkText(&ap);
        }
    }
    // Line breaking ends on runs wider than a line (used to loop forever: Japanese text, or any
    // text after a newline)
    for (const auto& [input, language] : std::vector<std::pair<std::string, int>>{
             {"Hello\nABCDEFGHIJKLMNOPQRSTUVWXYZ end", randomizer::Text::ENGLISH},
             {"\x82\xa0" "ABCDEFGHIJKLMNOPQRSTUVWXYZ\x82\xa0\x82\xa0", randomizer::Text::JAPANESE},
             {"\x82\xa0\x82\xa0\x82\xa0\x82\xa0\x82\xa0\x82\xa0\x82\xa0\x82\xa0\x82\xa0\x82\xa0", randomizer::Text::JAPANESE},
         })
    {
        std::string text = input;
        randomizer::breakLines(text, randomizer::Text::MAX_LINE_WIDTH_ITEM_TEXTBOX, language);
        ++texts;
        if (text.size() > input.size() + 4 && ++mismatches <= 40) {
            std::cout << "MISMATCH\tline breaking added " << text.size() - input.size() << " bytes" << std::endl;
        }
    }
    // Names: what each encoding can show is kept, the rest becomes '?', markup is dropped
    const struct {
        const char* in;
        size_t max;
        bool japanese;
        const char* out;
    } nameCases[] = {
        {"Bob", 16, false, "Bob"},
        {"\xC3\x89lise", 16, false, "\xC3\x89lise"},                     // Élise
        {"\xC3\x89lise", 16, true, "?lise"},
        {"Zo\xC3\xAB \xF0\x9F\x8E\xAE", 16, false, "Zo\xC3\xAB ?"},       // Zoë 🎮
        {"\xE5\x90\x8D\xE5\x89\x8D", 16, true, "\xE5\x90\x8D\xE5\x89\x8D"},  // 名前
        {"\xE5\x90\x8D\xE5\x89\x8D", 16, false, "??"},
        {"\xC5\x9C" "a", 16, false, "?a"},                               // Ŝ (not CP1252)
        {"\xE2\x82\xAC" "5 \xE2\x84\xA2", 16, false, "\xE2\x82\xAC" "5 \xE2\x84\xA2"},  // €5 ™
        {"<red>Hack{x}", 16, false, "red Hack x"},
        {"a\x01\x1A" "b\n\tc", 16, false, "a b c"},
        {"\xFF\xFE" "ok", 16, false, "??ok"},                           // invalid UTF-8
        {"\xC2\x85" "x", 16, false, "?x"},                              // C1 control
        {"\xC0\xAF" "x", 16, false, "?x"},                              // overlong '/'
        {"\xE2\x82", 16, false, "??"},                                  // truncated sequence
        {"   ", 16, false, "?"},
        {"", 16, false, "?"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", 16, false, "ABCDEFGHIJKLM..."},
        {"\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 4, false, "\xC3\xA9..."},
        {"Long name with spaces at the cut", 13, false, "Long name..."},
    };
    for (const auto& c : nameCases) {
        const auto got = randomizer::archi::NameForGameText(c.in, c.max, c.japanese);
        if (got != c.out && ++mismatches <= 40) {
            std::cout << "MISMATCH\tname " << nlohmann::json(std::string(c.in)).dump(-1, ' ', true,
                         nlohmann::json::error_handler_t::replace) << " -> "
                      << nlohmann::json(got).dump(-1, ' ', true, nlohmann::json::error_handler_t::replace)
                      << ", expected " << nlohmann::json(std::string(c.out)).dump(-1, ' ', true,
                         nlohmann::json::error_handler_t::replace) << std::endl;
        }
        randomizer::archi::ApLocation fake{"(name test)", 0, c.in, c.in, randomizer::archi::kItemProgression, false};
        checkText(&fake);
    }

    std::cout << (mismatches == 0 ? "OK" : "FAIL") << "\t" << names << " check names, " << slot->locations.size() << " locations ("
              << foreign << " for other players), hash " << written.mHash << ", " << loaded.mTextOverrides.size()
              << " text languages, " << loaded.mItemLocations.size() << " named checks, " << texts << " get-item texts" << std::endl;
    return mismatches == 0 ? 0 : 1;
}
