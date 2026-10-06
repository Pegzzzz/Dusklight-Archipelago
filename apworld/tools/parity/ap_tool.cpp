// Stand-alone checks for the Archipelago side of the mod, without the game.
//
//   ap_tool generate <slot_data.json> <work dir>
//       Runs the randomizer generator on an Archipelago slot exactly like the mod does when a
//       new save is created, then verifies every location holds the multiworld's item.

#include "../../../generator/randomizer.hpp"
#include "../../../src/archipelago/ap_seed.hpp"

#include <chrono>
#include <fstream>
#include <iostream>

using namespace randomizer;

static int Generate(const std::filesystem::path& slotPath, const std::filesystem::path& workDir) {
    std::ifstream file(slotPath);
    if (!file) {
        std::cout << "ERROR\tcannot open " << slotPath << std::endl;
        return 2;
    }
    nlohmann::json json = nlohmann::json::parse(file);
    std::string error;
    auto slot = archi::SlotData::Parse(json, error);
    if (!slot) {
        std::cout << "ERROR\t" << error << std::endl;
        return 1;
    }

    std::filesystem::create_directories(workDir);
    Randomizer rando{workDir};
    const auto start = std::chrono::steady_clock::now();
    auto result = archi::GenerateWorlds(rando, workDir / "archipelago" / slot->seed, *slot);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (result) {
        std::cout << "ERROR\tgeneration failed: " << *result << std::endl;
        return 1;
    }
    auto problems = archi::VerifyPlacements(rando, *slot);
    for (const auto& problem : problems) {
        std::cout << "MISMATCH\t" << problem << std::endl;
    }
    std::cout << (problems.empty() ? "OK" : "FAIL") << "\t" << slot->locations.size() << " locations, hash "
              << rando.GetConfig().GetHash() << ", " << ms << " ms" << std::endl;
    return problems.empty() ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "generate") {
        return Generate(argv[2], argv[3]);
    }
    std::cerr << "usage: ap_tool generate <slot_data.json> <work dir>" << std::endl;
    return 2;
}
