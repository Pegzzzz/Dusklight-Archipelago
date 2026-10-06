cmake_minimum_required(VERSION 3.25)
project(randomizer CXX)
set(CMAKE_CXX_STANDARD 23)

# Sources and string literals are UTF-8 (the Archipelago get-item texts use \u escapes that must
# come out as UTF-8 bytes); MSVC otherwise uses the system code page
if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Fetch dusklight if we are invoking this as the root CMakeLists.txt
    if (NOT DUSKLIGHT_VERSION)
        set(DUSKLIGHT_VERSION "v2.0.0")
    endif ()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
    option(DUSK_MOD_USE_FULL_TREE "Use full build instead of the minimal mod SDK" OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if (DUSK_MOD_USE_FULL_TREE)
        add_subdirectory("${DUSKLIGHT_DIR}" dusk EXCLUDE_FROM_ALL)
    else ()
        add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)
    endif ()
endif ()

# Generator dependencies, linked statically into the mod library (no CRT crossing).
include(FetchContent)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(B_PRODUCTION_MODE ON)

message(STATUS "randomizer: Fetching yaml-cpp")
FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.9.0
)
message(STATUS "randomizer: Fetching base64pp")
set(_randomizer_exclude_from_all)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(_randomizer_exclude_from_all EXCLUDE_FROM_ALL)
endif ()
FetchContent_Declare(
        base64pp
        GIT_REPOSITORY https://github.com/matheusgomes28/base64pp.git
        GIT_TAG v0.2.0-rc0
        ${_randomizer_exclude_from_all}
)

message(STATUS "randomizer: Fetching battery-embed")
FetchContent_Declare(
        battery-embed
        GIT_REPOSITORY https://github.com/TwilitRealm/embed.git
        GIT_TAG        twilit-v2
)

message(STATUS "randomizer: Fetching nlohmann_json")
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
        ${_randomizer_exclude_from_all}
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(yaml-cpp base64pp battery-embed nlohmann_json)

set(RANDOMIZER_GENERATOR_SOURCES
        generator/logic/area.cpp
        generator/logic/dungeon.cpp
        generator/logic/entrance.cpp
        generator/logic/entrance_shuffle.cpp
        generator/logic/fill.cpp
        generator/logic/flatten/bits.cpp
        generator/logic/flatten/flatten.cpp
        generator/logic/flatten/simplify_algebraic.cpp
        generator/logic/hints.cpp
        generator/logic/item.cpp
        generator/logic/item_pool.cpp
        generator/logic/location.cpp
        generator/logic/plandomizer.cpp
        generator/logic/requirement.cpp
        generator/logic/search.cpp
        generator/logic/spoiler_log.cpp
        generator/logic/world.cpp
        generator/randomizer.cpp
        generator/seedgen/config.cpp
        generator/seedgen/seed.cpp
        generator/seedgen/settings.cpp
        generator/utility/color.cpp
        generator/utility/common.cpp
        generator/utility/endian.cpp
        generator/utility/file.cpp
        generator/utility/log.cpp
        generator/utility/path.cpp
        generator/utility/platform.cpp
        generator/utility/random.cpp
        generator/utility/string.cpp
        generator/utility/text.cpp
        generator/utility/time.cpp
)

set(RANDOMIZER_SOURCES
        src/draw.cpp
        src/flags.cpp
        src/messages.cpp
        src/randomizer_context.cpp
        src/stages.cpp
        src/tools.cpp
        src/verify_item_functions.cpp
        src/paths.cpp
        src/session.cpp
        src/hooks.cpp
        src/item.cpp
        src/ui/config_store.cpp
        src/ui/rando_seed_generation.cpp
        src/ui/rando_config.cpp
        src/ui/ui.cpp
        src/ui/tracker.cpp
        src/check_names.cpp
        src/archipelago/ap_client.cpp
        src/archipelago/ap_game.cpp
        src/archipelago/ap_locations.cpp
        src/archipelago/ap_seed.cpp
        src/archipelago/ap_transport.cpp
        src/archipelago/ap_ui.cpp
        src/archipelago/ws_codec.cpp
)

# Embed the generator's YAML data into the mod library. Paths are relative to this
# directory and must match the RANDO_DATA_PATH-based b::embed<> identifiers in code.
file(GLOB_RECURSE RANDOMIZER_DATA RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/generator/data/*")

# Create an OBJECT library to hold all battery-embed generated files so we can add it
# to both the mod and the standalone generator
add_library(randomizer_embeds OBJECT)

foreach (RANDOMIZER_FILE IN LISTS RANDOMIZER_DATA)
    if (RANDOMIZER_FILE MATCHES "^generator/data/tests")
        continue()
    endif ()
    b_embed(randomizer_embeds "${RANDOMIZER_FILE}")
endforeach ()

# Versioning
set(RANDOMIZER_VERSION "1.0.6")
# Version of the Archipelago mod (mod.json) and of the APWorld it goes with
set(ARCHIPELAGO_MOD_VERSION "0.1.0")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )

    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )

    execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_STATUS_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )

    set(FULL_RANDOMIZER_VERSION "v${RANDOMIZER_VERSION}-${GIT_BRANCH}-${GIT_COMMIT_HASH}")

    if(NOT GIT_STATUS_OUTPUT STREQUAL "")
        set(FULL_RANDOMIZER_VERSION "${FULL_RANDOMIZER_VERSION}-dirty")
    endif()
else()
    set(FULL_RANDOMIZER_VERSION "v${RANDOMIZER_VERSION}-unknown")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/mod.json.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/mod.json"
        @ONLY
        NEWLINE_STYLE LF
)

add_mod(tp_archipelago
    FEATURES game fmt
    SOURCES  src/mod.cpp ${RANDOMIZER_GENERATOR_SOURCES} ${RANDOMIZER_SOURCES}
    MOD_JSON mod.json
    RES_DIR  res
    BUNDLE
)

target_link_libraries(tp_archipelago PRIVATE yaml-cpp::yaml-cpp base64pp randomizer_embeds nlohmann_json::nlohmann_json)

string(LENGTH "${CMAKE_CURRENT_SOURCE_DIR}/" RANDOMIZER_SOURCE_PATH_SIZE)
target_compile_definitions(tp_archipelago PRIVATE
        FULL_RANDOMIZER_VERSION="${FULL_RANDOMIZER_VERSION}"
        ARCHIPELAGO_MOD_VERSION="${ARCHIPELAGO_MOD_VERSION}"
        RANDO_DATA_PATH="generator/data/"
        SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE}
)

# standalone randomizer generator for testing
add_executable(randomizer_generator ${CMAKE_CURRENT_SOURCE_DIR}/generator/generator_only.cpp ${RANDOMIZER_GENERATOR_SOURCES})
target_link_libraries(randomizer_generator PRIVATE
        dusklight_mod_api yaml-cpp::yaml-cpp base64pp fmt::fmt randomizer_embeds)
target_compile_definitions(randomizer_generator PRIVATE
        FULL_RANDOMIZER_VERSION="${FULL_RANDOMIZER_VERSION}"
        RANDO_DATA_PATH="generator/data/"
        SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE}
        RANDO_SAVE_PATH="${CMAKE_BINARY_DIR}/randomizer/"
        RANDOMIZER_ONLY
        RANDO_DEBUG)

# standalone tests
add_executable(randomizer_generator_tests ${CMAKE_CURRENT_SOURCE_DIR}/generator/test/test.cpp ${RANDOMIZER_GENERATOR_SOURCES})
target_link_libraries(randomizer_generator_tests PRIVATE
        dusklight_mod_api yaml-cpp::yaml-cpp base64pp fmt::fmt randomizer_embeds)
set(RANDO_LOGIC_TESTS_PATH "${CMAKE_SOURCE_DIR}/mods/randomizer/generator/data/tests/logic")
if (NOT EXISTS "${RANDO_LOGIC_TESTS_PATH}")
    set(RANDO_LOGIC_TESTS_PATH "${CMAKE_SOURCE_DIR}/generator/data/tests/logic")
endif()

target_compile_definitions(randomizer_generator_tests PRIVATE
        FULL_RANDOMIZER_VERSION="${FULL_RANDOMIZER_VERSION}"
        RANDO_DATA_PATH="generator/data/"
        SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE}
        RANDO_SAVE_PATH="${CMAKE_BINARY_DIR}/randomizer tests/"
        RANDO_LOGIC_TESTS_PATH="${RANDO_LOGIC_TESTS_PATH}"
        SETTINGS_PATH="${CMAKE_BINARY_DIR}/randomizer tests/settings.yaml"
        PREFERENCES_PATH="${CMAKE_BINARY_DIR}/randomizer tests/preferences.yaml"
        RANDOMIZER_ONLY)

# Archipelago: logic parity harness used by apworld/tools/parity_test.py
add_executable(ap_parity_harness EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/apworld/tools/parity/parity.cpp ${RANDOMIZER_GENERATOR_SOURCES})
target_link_libraries(ap_parity_harness PRIVATE
        dusklight_mod_api yaml-cpp::yaml-cpp base64pp fmt::fmt randomizer_embeds)
target_compile_definitions(ap_parity_harness PRIVATE
        FULL_RANDOMIZER_VERSION="${FULL_RANDOMIZER_VERSION}"
        RANDO_DATA_PATH="generator/data/"
        SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE}
        RANDOMIZER_ONLY)

# Archipelago: stand-alone checks of slot data -> randomizer seed (no game needed)
add_executable(ap_tool EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/apworld/tools/parity/ap_tool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/archipelago/ap_seed.cpp
        ${RANDOMIZER_GENERATOR_SOURCES})
target_link_libraries(ap_tool PRIVATE
        dusklight_mod_api yaml-cpp::yaml-cpp base64pp fmt::fmt randomizer_embeds nlohmann_json::nlohmann_json)
target_compile_definitions(ap_tool PRIVATE
        FULL_RANDOMIZER_VERSION="${FULL_RANDOMIZER_VERSION}"
        RANDO_DATA_PATH="generator/data/"
        SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE}
        RANDOMIZER_ONLY)

# Archipelago: the mod's network client on POSIX sockets, driven by apworld/tools/client_test.py
if (UNIX)
    add_executable(ap_client_driver EXCLUDE_FROM_ALL
            ${CMAKE_CURRENT_SOURCE_DIR}/apworld/tools/client/ap_client_driver.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/archipelago/ap_client.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/archipelago/ws_codec.cpp)
    target_link_libraries(ap_client_driver PRIVATE nlohmann_json::nlohmann_json)
endif ()

# Archipelago: the mod's new-save seed pipeline (slot data -> seed.dat -> in-game lookups) on the
# host, against the game headers but without the game (Linux only: game symbols stay unresolved)
if (UNIX AND NOT APPLE AND NOT ANDROID)
    # Text line breaking measures with the game's font in the mod; use the generator's own widths
    set(_ap_test_generator_sources ${RANDOMIZER_GENERATOR_SOURCES})
    list(REMOVE_ITEM _ap_test_generator_sources generator/utility/text.cpp)
    add_library(ap_test_text OBJECT EXCLUDE_FROM_ALL generator/utility/text.cpp)
    target_link_libraries(ap_test_text PRIVATE dusklight_mod_api yaml-cpp::yaml-cpp fmt::fmt randomizer_embeds)
    target_compile_definitions(ap_test_text PRIVATE RANDOMIZER_ONLY RANDO_DATA_PATH="generator/data/"
            SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE})
    add_executable(ap_seeddata_test EXCLUDE_FROM_ALL
            ${CMAKE_CURRENT_SOURCE_DIR}/apworld/tools/parity/seeddata_test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/archipelago/ap_seed.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/archipelago/ap_locations.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/check_names.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/randomizer_context.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/tools.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/stages.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/paths.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/rando_seed_generation.cpp
            ${_ap_test_generator_sources}
            $<TARGET_OBJECTS:ap_test_text>)
    target_link_libraries(ap_seeddata_test PRIVATE dusklight_mod_api dusklight_game_abi_headers
            yaml-cpp::yaml-cpp base64pp fmt::fmt randomizer_embeds nlohmann_json::nlohmann_json)
    target_compile_options(ap_seeddata_test PRIVATE "SHELL:-include global.h")
    target_compile_definitions(ap_seeddata_test PRIVATE
            DUSK_MOD_FEATURE_FMT=1 DUSK_MOD_FEATURE_GAME=1
            FULL_RANDOMIZER_VERSION="${FULL_RANDOMIZER_VERSION}"
            RANDO_DATA_PATH="generator/data/"
            SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE})
    target_link_options(ap_seeddata_test PRIVATE "LINKER:--unresolved-symbols=ignore-all")
endif ()
