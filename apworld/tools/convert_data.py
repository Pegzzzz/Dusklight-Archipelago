#!/usr/bin/env python3
"""Convert the Dusklight randomizer's generator data into the APWorld's data files.

The APWorld never reads the randomizer's YAML at generation time. Instead this script
flattens everything the world needs (items, locations, macros, the world graph, settings)
into one JSON file, and keeps a frozen table of Archipelago IDs so that IDs never change
between releases, even when the randomizer adds, removes or reorders locations.

All YAML is loaded with BaseLoader so every scalar stays the exact text the C++ generator
sees (yaml-cpp's as<std::string>()). PyYAML's default loader would turn "On"/"Off" into
booleans, which would silently corrupt the logic.

Usage:
    python apworld/tools/convert_data.py [--data generator/data] [--out apworld/tp_dusklight/data]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[2]

# Same file order as World::LoadWorldGraph() in generator/logic/world.cpp. Event and area
# indices depend on it, so keep it identical.
WORLD_FILES = [
    "world/Root.yaml",
    "world/overworld/Ordona Province.yaml",
    "world/overworld/Faron Province.yaml",
    "world/overworld/Eldin Province.yaml",
    "world/overworld/Lanayru Province.yaml",
    "world/overworld/Gerudo Desert.yaml",
    "world/overworld/Snowpeak Province.yaml",
    "world/dungeons/Forest Temple.yaml",
    "world/dungeons/Goron Mines.yaml",
    "world/dungeons/Lakebed Temple.yaml",
    "world/dungeons/Arbiters Grounds.yaml",
    "world/dungeons/Snowpeak Ruins.yaml",
    "world/dungeons/Temple of Time.yaml",
    "world/dungeons/City in the Sky.yaml",
    "world/dungeons/Palace of Twilight.yaml",
    "world/dungeons/Hyrule Castle.yaml",
]

# Archipelago ID bases. Item IDs are derived from the game's own item IDs, which are
# stable. Location IDs come from the frozen table in ids.json.
ITEM_ID_BASE = 0x7470_0000
LOCATION_ID_BASE = 0x7470_1000


def load(path: Path):
    with path.open(encoding="utf-8") as f:
        return yaml.load(f, Loader=yaml.BaseLoader)


def as_bool(value, default=False) -> bool:
    if value is None:
        return default
    return str(value).strip().lower() in ("true", "yes", "on", "y", "1")


def convert_settings(data_dir: Path) -> list[dict]:
    settings = []
    for node in load(data_dir / "settings_list.yaml"):
        options = []
        for option_node in node["Options"]:
            for option in option_node:
                if "-" in option:
                    low, high = option.split("-")
                    options.extend(str(i) for i in range(int(low), int(high) + 1))
                else:
                    options.append(option)
        default = node["Default Option"]
        if default not in options:
            raise ValueError(f"default {default!r} not an option of {node['Name']!r}")
        settings.append({
            "name": node["Name"],
            "options": options,
            "default": default,
            "need_in_game": as_bool(node.get("Need In Game")),
            "tracker_important": as_bool(node.get("Tracker Important")),
        })
    return settings


def convert_items(data_dir: Path) -> list[dict]:
    items = []
    for node in load(data_dir / "items.yaml"):
        items.append({
            "name": node["Name"],
            "id": int(node["Id"], 0),
            "importance": node["Importance"],
            "game_winning": as_bool(node.get("Game Winning Item")),
            "small_key_of": node.get("Dungeon Small Key", ""),
            "big_key_of": node.get("Dungeon Big Key", ""),
            "compass_of": node.get("Dungeon Compass", ""),
            "map_of": node.get("Dungeon Map", ""),
        })
    return items


def convert_locations(data_dir: Path) -> list[dict]:
    locations = []
    for node in load(data_dir / "locations.yaml"):
        categories = list(node["Categories"])
        metadata = node.get("Metadata")
        if isinstance(metadata, dict):
            # World::BuildLocationTable adds metadata field names to the categories
            for key in metadata:
                if key not in categories:
                    categories.append(key)
        else:
            metadata = {}
        locations.append({
            "name": node["Name"],
            "original_item": node.get("Original Item", "Nothing"),
            "categories": categories,
            "goal_name": node.get("Goal Name"),
            "hint_priority": node.get("Hint Priority", "Never"),
            "metadata": metadata,
        })
    return locations


def convert_macros(data_dir: Path) -> list[list[str]]:
    macros = load(data_dir / "macros.yaml")
    return [[name, req] for name, req in macros.items()]


def convert_world(data_dir: Path) -> list[dict]:
    areas = []
    for rel in WORLD_FILES:
        for node in load(data_dir / rel):
            # Only the exact keys World::LoadWorldGraph reads. Misspelled keys in the data
            # ("Can_Warp", "Map Selector") are ignored by the C++ and must be ignored here.
            areas.append({
                "name": node["Name"],
                "file": rel,
                "map_sector": node.get("Map Sector", ""),
                "region": node.get("Region", ""),
                "twilight": node.get("Twilight", ""),
                "dungeon_start": as_bool(node.get("Dungeon Start Area")),
                "can_warp": as_bool(node.get("Can Warp")),
                "can_change_time": as_bool(node.get("Can Change Time")),
                "can_transform": node.get("Can Transform", "Always"),
                "events": [[k, v] for k, v in (node.get("Events") or {}).items()],
                "locations": [[k, v] for k, v in (node.get("Locations") or {}).items()],
                "exits": [[k, v] for k, v in (node.get("Exits") or {}).items()],
            })
    return areas


def git_describe(path: Path) -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=path,
                                       text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


def update_location_ids(ids_path: Path, location_names: list[str]) -> dict[str, int]:
    """Assign stable location IDs. Existing names keep their ID forever; new names get the next
    free ID. Removed names keep their reservation so an ID is never reused."""
    table = {}
    if ids_path.exists():
        table = json.loads(ids_path.read_text(encoding="utf-8"))
    locations: dict[str, int] = table.get("locations", {})
    next_id = max(locations.values(), default=LOCATION_ID_BASE - 1) + 1
    for name in location_names:
        if name not in locations:
            locations[name] = next_id
            next_id += 1
    table["locations"] = locations
    table["item_id_base"] = ITEM_ID_BASE
    ids_path.write_text(json.dumps(table, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    return locations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=REPO_ROOT / "generator" / "data")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "apworld" / "tp_dusklight" / "data")
    args = parser.parse_args()

    data_dir: Path = args.data
    out_dir: Path = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    locations = convert_locations(data_dir)
    logic = {
        "source": {
            "randomizer_commit": git_describe(data_dir),
        },
        "settings": convert_settings(data_dir),
        "items": convert_items(data_dir),
        "locations": locations,
        "macros": convert_macros(data_dir),
        "areas": convert_world(data_dir),
    }
    (out_dir / "logic.json").write_text(json.dumps(logic, ensure_ascii=False, separators=(",", ":")),
                                        encoding="utf-8")

    ids = update_location_ids(out_dir / "ids.json",
                              [loc["name"] for loc in locations])
    print(f"wrote {len(logic['items'])} items, {len(locations)} locations "
          f"({len(ids)} ids), {len(logic['macros'])} macros, {len(logic['areas'])} areas "
          f"to {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
