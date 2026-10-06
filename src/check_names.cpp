// Parsing of Dusklight item service check names (mods/items.h) into the seed's override keys.
// Shared by the randomizer's check resolver and the Archipelago location tracking.

#include "session.hpp"

#include "tools.h"

#include <cstdlib>
#include <cstring>

namespace randomizer::session {

std::optional<int> parse_stage_check(const char* name, std::string_view prefix) {
    if (std::strncmp(name, prefix.data(), prefix.size()) != 0) {
        return std::nullopt;
    }
    const char* stage = name + prefix.size();
    if (*stage == '\0' || std::strchr(stage, ':') != nullptr) {
        return std::nullopt;
    }
    const int stageId = getStageID(stage);
    return stageId >= 0 ? std::optional{stageId} : std::nullopt;
}

std::optional<DerivedKey> parse_derived(const char* name, std::string_view prefix) {
    if (std::strncmp(name, prefix.data(), prefix.size()) != 0) {
        return std::nullopt;
    }
    const char* stage_begin = name + prefix.size();
    const char* stage_end = std::strchr(stage_begin, ':');
    if (stage_end == nullptr) {
        return std::nullopt;
    }
    const std::string stage{stage_begin, stage_end};
    const int stage_id = getStageID(stage.c_str());
    if (stage_id < 0) {
        return std::nullopt;
    }
    const int n = std::atoi(stage_end + 1);
    return DerivedKey{stage_id, static_cast<u16>((stage_id << 8) | (n & 0xFF))};
}

std::optional<u32> parse_shop_check(const char* name, std::string_view prefix) {
    if (std::strncmp(name, prefix.data(), prefix.size()) != 0) {
        return std::nullopt;
    }
    const char* stage_begin = name + prefix.size();
    const char* stage_end = std::strchr(stage_begin, ':');
    if (stage_end == nullptr) {
        return std::nullopt;
    }
    const std::string stage{stage_begin, stage_end};
    const int stage_id = getStageID(stage.c_str());
    if (stage_id < 0) {
        return std::nullopt;
    }
    const char* room_begin = stage_end + 1;
    const char* room_end = std::strchr(room_begin, ':');
    if (room_end == nullptr) {
        return std::nullopt;
    }
    const std::string roomStr{room_begin, room_end};
    u8 roomNo = std::atoi(roomStr.c_str());
    const int itemNo = std::atoi(room_end + 1);
    return static_cast<u32>((stage_id << 16) | (roomNo << 8) | (itemNo & 0xFF));
}

std::optional<u16> parse_flag_check(const char* name, std::string_view prefix) {
    if (std::strncmp(name, prefix.data(), prefix.size()) != 0) {
        return std::nullopt;
    }
    const char* value = name + prefix.size();
    char* end = nullptr;
    const unsigned long flag = std::strtoul(value, &end, 10);
    if (value == end || *end != '\0' || flag > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<u16>(flag);
}

}  // namespace randomizer::session
