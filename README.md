# Twilight Princess Archipelago (Dusklight)

[Archipelago](https://archipelago.gg) multiworld support for *Twilight Princess* on
[Dusklight](https://twilitrealm.dev), built on the official
[Dusklight Randomizer](https://github.com/TwilitRealm/dusklight-randomizer).

It comes in two parts, released together:

- **`tp_archipelago.dusk`**, a Dusklight mod. It adds an **Archipelago** game mode with its own save
  files, so it can be installed next to the Dusklight Randomizer and other mods. When you create a
  save it connects to your Archipelago room, builds your world from the room's data with the
  randomizer's own generator, and then sends your checks and receives your items while you play.
- **`tp_dusklight.apworld`**, the Archipelago world. Its options are the randomizer's settings, and
  its logic is a port of the randomizer's logic (form, time of day, twilight), tested against the
  C++ generator.

Players: start with the **[tutorial](docs/TUTORIAL.md)**, which goes from installing everything to
finishing a multiworld. The [setup guide](apworld/tp_dusklight/docs/setup_en.md) and the
[game page](<apworld/tp_dusklight/docs/en_Twilight Princess Dusklight.md>) are the short versions
shown by Archipelago.

## How it works

1. **Generation (Archipelago).** The APWorld builds the randomizer's world graph from the
   randomizer's data (`apworld/tp_dusklight/data`, converted from `generator/data` by
   `apworld/tools/convert_data.py`) and fills it like the randomizer does: vanilla items, dungeon
   rewards, required and barren dungeons, dungeon item placement modes. Its slot data carries the
   randomizer settings and the item at every location.
2. **New save (mod).** The mod receives the slot data when it connects, writes it as randomizer
   settings plus a plandomizer file, and runs the randomizer generator in an "Archipelago" mode
   (`generator/`, `src/archipelago/ap_seed.cpp`). Every patch, flag and text of a normal randomizer
   seed is produced; only the placements come from the multiworld. Other players' items become the
   "Archipelago Item" (0xDC), shown as the Archipelago logo in 3D (`overlay/res/Object/O_gD_ap.arc`,
   built by `tools/models/make_ap_item.py`) with a text naming the owner and the item.
3. **Playing (mod).** `src/archipelago/ap_game.cpp` reports locations from the save flags and from
   Dusklight's item give events, gives received items through Dusklight's item queue, detects the
   final blow on Ganondorf, and handles DeathLink. `ap_client.cpp` speaks Archipelago's protocol
   over Dusklight's WebSocket service (`wss://`, and `ws://` to localhost) or over its TCP service
   with a small WebSocket codec (`ws://` elsewhere).

## Building

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The mod is `build/mods/tp_archipelago.dusk`, for the platform you built on (CI builds every
platform Dusklight supports and merges them into one bundle). To build against an existing
Dusklight checkout instead of fetching one, add `-DDUSKLIGHT_DIR=~/path/to/dusklight`.

The APWorld:

```sh
python apworld/tools/package_apworld.py -o dist/tp_dusklight.apworld
```

## Tests

With an Archipelago checkout (0.6.4 or newer) prepared by `tools/ci/prepare_archipelago.sh`
(it links `apworld/tp_dusklight` into its `worlds`):

```sh
# APWorld unit tests and Archipelago's general tests
(cd archipelago && python -m pytest worlds/tp_dusklight/test test/general)

# Tools (Linux)
cmake --build build --target ap_parity_harness ap_tool ap_seeddata_test ap_client_driver

# The Python logic against the C++ generator, on presets and random settings
PYTHONPATH=archipelago python apworld/tools/parity_test.py --harness build/ap_parity_harness --random-presets 40

# Multiworlds -> slot data -> in-game seed data, read back the way the game reads it
(cd archipelago && python ../apworld/tools/seed_test.py --ap-tool ../build/ap_tool \
    --seeddata-test ../build/ap_seeddata_test)

# The mod's network client against a real MultiServer
python apworld/tools/client_test.py archipelago build/ap_client_driver

# The Archipelago item model: read back by an independent script, then loaded with Dusklight's own
# J3D loader and display list code, as the game loads it (needs xxhash's header, e.g. libxxhash-dev)
python tools/models/check_ap_item.py --preview ap_item.png
cmake --build build --target ap_model_harness && build/ap_model_harness overlay/res/Object/O_gD_ap.arc
```

What needs the game itself (save flags, the item queue, the get-item text, the menus) can only be
tested by playing.

## Credits

- The [Dusklight Randomizer](https://github.com/TwilitRealm/dusklight-randomizer) and
  [Dusklight](https://github.com/TwilitRealm/dusklight) by Twilit Realm. This project is a fork of
  the randomizer; everything it does in game rests on their work. The "Twilight Princess
  Archipelago" title logo comes from the randomizer's own `archipelago` branch.
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago).
