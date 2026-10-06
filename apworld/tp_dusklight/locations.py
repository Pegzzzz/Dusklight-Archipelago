"""Location table, derived from the randomizer's locations.yaml."""

from __future__ import annotations

from BaseClasses import Location

from .items import GAME
from .logic.world_graph import load_data, load_ids


class TPDusklightLocation(Location):
    game = GAME


VICTORY_LOCATION = "Ganondorf Defeated"

_raw_locations = load_data()["locations"]
_ids = load_ids()["locations"]

# Hint signs are not item locations; everything else can hold an item under some settings.
location_data: dict[str, dict] = {raw["name"]: raw for raw in _raw_locations
                                  if "Non-Item Location" not in raw["categories"]
                                  and raw["name"] != "Defeat Ganondorf"}
location_name_to_id: dict[str, int] = {name: _ids[name] for name in location_data}

# Categories that describe how a check is detected rather than what it is
_TECHNICAL_CATEGORIES = {
    "ARC", "DZX", "REL", "ObjectARC", "Placeholder", "Name Lookup", "Event Flag", "Switch Flag",
    "Item Flag", "Freestanding Item", "Bug Reward", "Metadata", "Event", "Cutscene", "None",
    "Hint Sign", "Non-Item Location",
}

_RENAMED_GROUPS = {
    "Npc": "NPC Gifts",
    "Poe": "Poes",
    "Golden Bug": "Golden Bugs",
    "Sky Character": "Sky Characters",
    "Golden Wolf": "Hidden Skills",
    "Shop": "Shops",
    "Chest": "Chests",
    "Rupee - Hidden": "Hidden Rupees",
    "Rupee - Freestanding": "Freestanding Rupees",
    "Heart Container": "Heart Containers",
    "Dungeon Reward": "Dungeon Rewards",
    "Boss": "Bosses",
    "Warp Portal": "Warp Portals",
    "Twilit Insect": "Twilit Insects",
}

location_name_groups: dict[str, set[str]] = {}
for _name, _raw in location_data.items():
    for _category in _raw["categories"]:
        if not _category or _category in _TECHNICAL_CATEGORIES:
            continue
        location_name_groups.setdefault(_RENAMED_GROUPS.get(_category, _category), set()).add(_name)
