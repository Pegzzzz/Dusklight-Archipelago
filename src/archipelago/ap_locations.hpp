#pragma once

// How the Archipelago locations of a slot are detected in game:
//  - the save flag that is set once a location is done (same precedence as the randomizer's
//    tracker), read periodically;
//  - the item service check names that hand out each location's item, seen as items are given.

#include "ap_seed.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace randomizer::archi {

enum class FlagKind : uint8_t { None, Tbox, Switch, Item, Event };

struct TrackedLocation {
    const ApLocation* ap = nullptr;
    FlagKind kind = FlagKind::None;
    int stage = -1;  // stage save table id (getStageSaveId), unused for event flags
    uint16_t flag = 0;
};

class LocationIndex {
public:
    /// Builds the index for the slot's locations. `slot` must outlive the index.
    void Build(const SlotData& slot);
    void Clear();

    const std::vector<TrackedLocation>& Locations() const { return mLocations; }
    /// The location whose item an item service check name gives, if it is one of the slot's.
    std::optional<size_t> ForCheck(const char* checkName) const;

private:
    std::vector<TrackedLocation> mLocations;
    std::unordered_map<uint64_t, size_t> mByKey;
    std::unordered_map<std::string, size_t> mByName;
};

}  // namespace randomizer::archi
