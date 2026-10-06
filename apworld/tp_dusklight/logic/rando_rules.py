"""Ports of the randomizer's item pool and pre-fill placement rules.

generator/logic/item_pool.cpp  -> item_pool(), starting_items()
generator/logic/world.cpp      -> vanilla_placements(), nonprogress_locations(), junk_items()

Everything here works on names so the Archipelago world and the parity tests can share it.
"""

from __future__ import annotations

import random
from collections import Counter
from typing import Iterable

from .world_graph import Location, LogicWorld

MINIMAL_ITEM_POOL = {
    "Shadow Crystal": 1, "Slingshot": 1, "Lantern": 1, "Gale Boomerang": 1, "Iron Boots": 1,
    "Bomb Bag": 1, "Spinner": 1, "Ball and Chain": 1,
    "Progressive Fishing Rod": 2, "Progressive Sword": 4, "Progressive Bow": 1,
    "Progressive Clawshot": 2, "Progressive Dominion Rod": 2, "Progressive Wallet": 2,
    "Progressive Sky Book": 7,
    "Aurus Memo": 1, "Asheis Sketch": 1, "Renados Letter": 1, "Invoice": 1, "Wooden Statue": 1,
    "Ilias Charm": 1, "Zora Armor": 1, "Hylian Shield": 1, "Ordon Shield": 1, "Empty Bottle": 4,
    "Progressive Hidden Skill": 1, "Poe Soul": 60,
    "Progressive Fused Shadow": 3, "Progressive Mirror Shard": 4,
    "Male Ant": 1, "Female Ant": 1, "Male Beetle": 1, "Female Beetle": 1, "Male Pill Bug": 1,
    "Female Pill Bug": 1, "Male Phasmid": 1, "Female Phasmid": 1, "Male Grasshopper": 1,
    "Female Grasshopper": 1, "Male Stag Beetle": 1, "Female Stag Beetle": 1, "Male Butterfly": 1,
    "Female Butterfly": 1, "Male Ladybug": 1, "Female Ladybug": 1, "Male Mantis": 1,
    "Female Mantis": 1, "Male Dragonfly": 1, "Female Dragonfly": 1, "Male Dayfly": 1,
    "Female Dayfly": 1, "Male Snail": 1, "Female Snail": 1,
    "Gate Keys": 1, "Gerudo Desert Bulblin Camp Key": 1, "North Faron Woods Gate Key": 1,
    "Faron Woods Coro Key": 1, "Forest Temple Small Key": 4, "Goron Mines Small Key": 3,
    "Lakebed Temple Small Key": 3, "Arbiters Grounds Small Key": 5, "Snowpeak Ruins Small Key": 4,
    "Ordon Pumpkin": 1, "Ordon Cheese": 1, "Temple of Time Small Key": 3,
    "City in the Sky Small Key": 1, "Palace of Twilight Small Key": 7, "Hyrule Castle Small Key": 3,
    "Forest Temple Big Key": 1, "Goron Mines Key Shard": 3, "Lakebed Temple Big Key": 1,
    "Arbiters Grounds Big Key": 1, "Snowpeak Ruins Bedroom Key": 1, "Temple of Time Big Key": 1,
    "City in the Sky Big Key": 1, "Palace of Twilight Big Key": 1, "Hyrule Castle Big Key": 1,
    "Forest Temple Compass": 1, "Goron Mines Compass": 1, "Lakebed Temple Compass": 1,
    "Arbiters Grounds Compass": 1, "Snowpeak Ruins Compass": 1, "Temple of Time Compass": 1,
    "City in the Sky Compass": 1, "Palace of Twilight Compass": 1, "Hyrule Castle Compass": 1,
    "Forest Temple Dungeon Map": 1, "Goron Mines Dungeon Map": 1, "Lakebed Temple Dungeon Map": 1,
    "Arbiters Grounds Dungeon Map": 1, "Snowpeak Ruins Dungeon Map": 1,
    "Temple of Time Dungeon Map": 1, "City in the Sky Dungeon Map": 1,
    "Palace of Twilight Dungeon Map": 1, "Hyrule Castle Dungeon Map": 1,
    "Ordon Spring Portal": 1, "South Faron Portal": 1, "North Faron Portal": 1,
    "Sacred Grove Portal": 1, "Kakariko Gorge Portal": 1, "Kakariko Village Portal": 1,
    "Death Mountain Portal": 1, "Bridge of Eldin Portal": 1, "Zoras Domain Portal": 1,
    "Lake Hylia Portal": 1, "Castle Town Portal": 1, "Upper Zoras River Portal": 1,
    "Snowpeak Portal": 1, "Gerudo Desert Portal": 1, "Mirror Chamber Portal": 1,
    "Faron Twilight Tear": 16, "Eldin Twilight Tear": 16, "Lanayru Twilight Tear": 16,
    "Purple Rupee Links House": 1, "Green Rupee": 2, "Orange Rupee": 50, "Silver Rupee": 2,
}

STANDARD_ITEM_POOL = {
    "Bomb Bag": 2, "Progressive Bow": 2, "Magic Armor": 1, "Hawkeye": 1, "Giant Bomb Bag": 1,
    "Horse Call": 1, "Progressive Hidden Skill": 6, "Heart Container": 8, "Piece of Heart": 45,
}

PLENTIFUL_ITEM_POOL = {
    "Shadow Crystal": 1, "Slingshot": 1, "Lantern": 1, "Gale Boomerang": 1, "Iron Boots": 1,
    "Bomb Bag": 1, "Spinner": 1, "Ball and Chain": 1,
    "Progressive Fishing Rod": 1, "Progressive Sword": 4, "Progressive Bow": 1,
    "Progressive Clawshot": 1, "Progressive Dominion Rod": 1, "Progressive Wallet": 1,
    "Progressive Sky Book": 1,
    "Aurus Memo": 1, "Asheis Sketch": 1, "Zora Armor": 1, "Magic Armor": 1, "Hylian Shield": 1,
    "Empty Bottle": 1, "Progressive Hidden Skill": 1,
    "Gate Keys": 1, "Forest Temple Small Key": 1, "Goron Mines Small Key": 1,
    "Lakebed Temple Small Key": 1, "Arbiters Grounds Small Key": 1, "Snowpeak Ruins Small Key": 1,
    "Ordon Pumpkin": 1, "Ordon Cheese": 1, "Temple of Time Small Key": 1,
    "City in the Sky Small Key": 1, "Palace of Twilight Small Key": 1, "Hyrule Castle Small Key": 1,
    "Forest Temple Big Key": 1, "Goron Mines Key Shard": 1, "Lakebed Temple Big Key": 1,
    "Arbiters Grounds Big Key": 1, "Snowpeak Ruins Bedroom Key": 1, "Temple of Time Big Key": 1,
    "City in the Sky Big Key": 1, "Palace of Twilight Big Key": 1, "Hyrule Castle Big Key": 1,
}

INITIAL_JUNK_POOL = {
    "Bombs 5": 8, "Bombs 10": 2, "Bombs 20": 1, "Bombs 30": 1, "Arrows 10": 13, "Arrows 20": 6,
    "Arrows 30": 2, "Seeds 50": 2, "Water Bombs 5": 3, "Water Bombs 10": 5, "Water Bombs 15": 3,
    "Bomblings 5": 2, "Bomblings 10": 2, "Blue Rupee": 1, "Yellow Rupee": 6, "Red Rupee": 6,
    "Purple Rupee": 12,
}

SMALL_KEYSY_ITEMS = [
    "Gate Keys", "Gerudo Desert Bulblin Camp Key", "Faron Woods Coro Key", "Forest Temple Small Key",
    "Goron Mines Small Key", "Lakebed Temple Small Key", "Arbiters Grounds Small Key",
    "Snowpeak Ruins Small Key", "Ordon Pumpkin", "Ordon Cheese", "Temple of Time Small Key",
    "City in the Sky Small Key", "Palace of Twilight Small Key", "Hyrule Castle Small Key",
]

BIG_KEYSY_ITEMS = [
    "Forest Temple Big Key", "Goron Mines Key Shard", "Lakebed Temple Big Key",
    "Arbiters Grounds Big Key", "Snowpeak Ruins Bedroom Key", "Temple of Time Big Key",
    "City in the Sky Big Key", "Palace of Twilight Big Key",
]

MAPS_AND_COMPASSES = [
    "Forest Temple Compass", "Goron Mines Compass", "Lakebed Temple Compass",
    "Arbiters Grounds Compass", "Snowpeak Ruins Compass", "Temple of Time Compass",
    "City in the Sky Compass", "Palace of Twilight Compass", "Hyrule Castle Compass",
    "Forest Temple Dungeon Map", "Goron Mines Dungeon Map", "Lakebed Temple Dungeon Map",
    "Arbiters Grounds Dungeon Map", "Snowpeak Ruins Dungeon Map", "Temple of Time Dungeon Map",
    "City in the Sky Dungeon Map", "Palace of Twilight Dungeon Map", "Hyrule Castle Dungeon Map",
]

# Always placed vanilla by World::PlaceVanillaItems ("for the time being")
ALWAYS_VANILLA_SUBSTRINGS = ("Renados Letter", "Telma Invoice", "Wooden Statue", "Ilia Charm",
                             "Defeat Ganondorf", "Twilit Insect", "Twilit Bloat")

# Small-key-like items that fill.cpp treats as small keys
SMALL_KEY_LIKE = ("Ordon Pumpkin", "Ordon Cheese", "North Faron Woods Gate Key", "Faron Woods Coro Key",
                  "Gate Keys", "Gerudo Desert Bulblin Camp Key")


def valid_starting_inventory_items() -> dict[str, int]:
    """item_pool::GetValidStartingInventoryItems"""
    valid = Counter(MINIMAL_ITEM_POOL)
    for name, count in STANDARD_ITEM_POOL.items():
        valid[name] += count
    for junk in ("Purple Rupee Links House", "Green Rupee", "Orange Rupee", "Silver Rupee"):
        valid.pop(junk, None)
    return dict(valid)


def item_pool(lw: LogicWorld) -> Counter:
    """item_pool::GenerateItemPool"""
    s = lw.settings
    pool = Counter(MINIMAL_ITEM_POOL)
    if s["Item Scarcity"] in ("Vanilla", "Plentiful"):
        for name, count in STANDARD_ITEM_POOL.items():
            pool[name] += count
    if s["Item Scarcity"] == "Plentiful":
        for name, count in PLENTIFUL_ITEM_POOL.items():
            pool[name] += count

    for region in ("Faron", "Eldin", "Lanayru"):
        if s[f"{region} Twilight Cleared"] == "On":
            pool.pop(f"{region} Twilight Tear", None)

    quest = lw.setting_index("Ilia Memory Quest")
    if quest > lw.option_index("Ilia Memory Quest", "Letter"):
        pool.pop("Renados Letter", None)
    if quest > lw.option_index("Ilia Memory Quest", "Invoice"):
        pool.pop("Invoice", None)
    if quest > lw.option_index("Ilia Memory Quest", "Statue"):
        pool.pop("Wooden Statue", None)

    if s["Skip Prologue"] == "On":
        pool.pop("North Faron Woods Gate Key", None)
    if s["Arbiters Does Not Require Bulblin Camp"] == "On":
        pool.pop("Gerudo Desert Bulblin Camp Key", None)
    if s["City Does Not Require Filled Skybook"] == "On":
        pool["Progressive Sky Book"] = 1
    if s["Small Keys"] == "Keysy":
        for key in SMALL_KEYSY_ITEMS:
            pool.pop(key, None)
    if s["Big Keys"] == "Keysy":
        keys = list(BIG_KEYSY_ITEMS)
        if s["Hyrule Castle Big Key Requirements"] == "None":
            keys.append("Hyrule Castle Big Key")
        for key in keys:
            pool.pop(key, None)
    return +pool


def starting_items(lw: LogicWorld, pool: Counter, starting_inventory: dict[str, int]) -> Counter:
    """item_pool::GenerateStartingItemPool. Moves starting items out of the pool."""
    s = lw.settings
    start = Counter(starting_inventory)
    if s["Maps and Compasses"] == "Start With":
        for name in MAPS_AND_COMPASSES:
            start[name] = 1
    start["Ordon Spring Portal"] = 1
    if s["Faron Twilight Cleared"] == "On":
        start["South Faron Portal"] = 1
        start["North Faron Portal"] = 1
    if s["Eldin Twilight Cleared"] == "On":
        start["Kakariko Gorge Portal"] = 1
        start["Kakariko Village Portal"] = 1
        start["Death Mountain Portal"] = 1
    if s["Lanayru Twilight Cleared"] == "On":
        start["Zoras Domain Portal"] = 1
        start["Lake Hylia Portal"] = 1
        start["Castle Town Portal"] = 1
    if (s["Mirror Chamber Access"] == "Closed" and
            not (s["Randomize Dungeon Entrances"] == "On" and s["Decouple Entrances"] == "On")):
        start["Mirror Chamber Portal"] = 1
    for name, count in start.items():
        pool[name] -= min(pool[name], count)
    return +start


def is_vanilla_location(lw: LogicWorld, loc: Location) -> bool:
    """The condition of World::PlaceVanillaItems for one location."""
    s = lw.settings
    original = loc.original_item
    original_name = original.name if original is not None else "Nothing"
    name = loc.name
    poe = s["Poe Souls"]
    return bool(
        (s["Small Keys"] == "Vanilla" and original is not None and
         (original.is_dungeon_small_key or any(k in original_name for k in ("Ordon Pumpkin", "Ordon Cheese")))) or
        (s["Big Keys"] == "Vanilla" and original is not None and original.is_big_key and
         (original_name != "Hyrule Castle Big Key" or s["Hyrule Castle Big Key Requirements"] == "None")) or
        (s["Maps and Compasses"] == "Vanilla" and original is not None and
         (original.is_dungeon_map or original.is_compass)) or
        (original_name == "Hyrule Castle Big Key" and s["Hyrule Castle Big Key Requirements"] != "None") or
        (original_name == "Poe Soul" and
         (poe == "Vanilla" or (poe == "Dungeon" and loc.has("Overworld")) or
          (poe == "Overworld" and loc.has("Dungeon")))) or
        (s["Golden Bugs"] == "Off" and loc.has("Golden Bug")) or
        (s["Sky Characters"] == "Off" and loc.has("Sky Character")) or
        (s["Gifts From NPCs"] == "Off" and loc.has("Npc")) or
        (s["Shop Items"] == "Off" and loc.has("Shop")) or
        (s["Hidden Skills"] == "Off" and loc.has("Golden Wolf")) or
        (s["Hidden Rupees"] == "Off" and loc.has("Rupee - Hidden")) or
        (s["Freestanding Rupees"] == "Off" and loc.has("Rupee - Freestanding")) or
        loc.has("Warp Portal") or
        any(sub in name for sub in ALWAYS_VANILLA_SUBSTRINGS)
    )


def vanilla_placements(lw: LogicWorld, pool: Counter) -> dict[str, str]:
    """World::PlaceVanillaItems. Returns {location: item} and removes those items from pool."""
    placed: dict[str, str] = {}
    for loc in lw.locations:
        if not is_vanilla_location(lw, loc):
            continue
        item = loc.original_item
        if item is None:
            # Location without an original item ("Nothing"): stays empty, still vanilla
            placed[loc.name] = "Nothing"
            continue
        name = item.name
        if item.bottle:
            name = "Empty Bottle"
        if name.startswith("Stamp"):
            name = "Purple Rupee"
        placed[loc.name] = name
        if pool[name] > 0:
            pool[name] -= 1
    return placed


def nonprogress_conflicts(lw: LogicWorld, vanilla: dict[str, str], starting: Counter) -> list[str]:
    """The second half of World::SetNonProgressLocations: vanilla locations whose item cannot
    exist under the settings. They lose their item and become randomized, non-progress spots."""
    s = lw.settings
    conflicts = []
    for loc in lw.locations:
        original = loc.original_item
        if original is None:
            continue
        if ((s["Gifts From NPCs"] == "Off" and loc.has("Npc") and
             ((s["Small Keys"] == "Keysy" and original.is_dungeon_small_key) or
              (s["Big Keys"] == "Keysy" and original.is_big_key) or
              (s["Maps and Compasses"] == "Start With" and (original.is_dungeon_map or original.is_compass)))) or
                (s["Shop Items"] == "Off" and loc.has("Shop") and starting[original.name] > 0)):
            conflicts.append(loc.name)
            vanilla.pop(loc.name, None)
    return conflicts


def junk_pool(lw: LogicWorld) -> Counter:
    """The junk distribution used by World::SanitizeItemPool."""
    junk = Counter(INITIAL_JUNK_POOL)
    frequency = lw.settings["Trap Item Frequency"]
    if frequency == "Few":
        junk["Foolish Item"] = 6
    elif frequency == "Many":
        junk["Foolish Item"] = 27
    elif frequency == "Mayhem":
        junk["Foolish Item"] = 64
    elif frequency == "Nightmare":
        junk = Counter({"Foolish Item": 1})
    return junk


def junk_items(lw: LogicWorld, count: int, rng: random.Random) -> list[str]:
    """Draw `count` junk items: each item of the junk pool once first, then randomly."""
    base = list(junk_pool(lw).elements())
    remaining = base[:]
    out = []
    for _ in range(count):
        if remaining:
            out.append(remaining.pop(rng.randrange(len(remaining))))
        else:
            out.append(rng.choice(base))
    return out


def dungeon_items(lw: LogicWorld) -> dict[str, list[str]]:
    """Per dungeon: the items fill.cpp moves as that dungeon's small keys / big keys / maps."""
    out: dict[str, list[str]] = {}
    for dungeon_name, dungeon in lw.dungeons.items():
        small = [dungeon["small_key"].name] if "small_key" in dungeon else []
        if dungeon_name == "Snowpeak Ruins":
            small += ["Ordon Pumpkin", "Ordon Cheese"]
        out[dungeon_name] = small
    return out


def boss_locations(lw: LogicWorld) -> Iterable[Location]:
    """Locations World::SetForbiddenItems keeps small keys away from."""
    return [loc for loc in lw.locations
            if not loc.has("Non-Item Location") and
            ("Heart Container" in loc.name or "Dungeon Reward" in loc.name)]


def unreachable_with_everything(lw: LogicWorld, pool: Counter, starting: Counter,
                                internal: dict[int, int]) -> list[str]:
    """search::VerifyLogic with the complete item pool ("All Locations Reachable"). The
    randomizer refuses settings for which this is not empty; so does the APWorld."""
    from .search import SearchState
    counts = [0] * len(lw.items)
    for item, count in (pool + starting).items():
        counts[lw.get_item(item).index] += count
    search = SearchState(lw, internal, counts)
    search.run()
    return [loc.name for loc in lw.locations if not search.reached[loc.index]]
