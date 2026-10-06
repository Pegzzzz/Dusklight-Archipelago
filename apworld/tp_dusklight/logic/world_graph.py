"""The randomizer's world graph, built the same way World::Build() builds it in C++.

This module does not import Archipelago, so it can be driven by stand-alone tests that compare
it against the C++ generator.
"""

from __future__ import annotations

import json
import pkgutil
from dataclasses import dataclass, field
from functools import lru_cache
from typing import Callable, Optional

from .requirement import (
    CodeGen,
    LogicError,
    Req,
    items_in,
    parse_requirement,
    simplify,
)


@lru_cache(maxsize=1)
def load_data() -> dict:
    raw = pkgutil.get_data(__package__.rsplit(".", 1)[0], "data/logic.json")
    if raw is None:
        raise FileNotFoundError("data/logic.json is missing from the apworld")
    return json.loads(raw.decode("utf-8"))


@lru_cache(maxsize=1)
def load_ids() -> dict:
    raw = pkgutil.get_data(__package__.rsplit(".", 1)[0], "data/ids.json")
    if raw is None:
        raise FileNotFoundError("data/ids.json is missing from the apworld")
    return json.loads(raw.decode("utf-8"))


@dataclass(eq=False)
class Item:
    index: int
    name: str
    game_id: int
    importance: str
    game_winning: bool
    small_key_of: str
    big_key_of: str
    compass_of: str
    map_of: str

    @property
    def golden_bug(self) -> bool:
        return self.name.startswith("Male") or self.name.startswith("Female")

    @property
    def shadow_crystal(self) -> bool:
        return self.name == "Shadow Crystal"

    @property
    def bottle(self) -> bool:
        return self.name.startswith("Bottle") or self.name == "Empty Bottle"

    @property
    def is_dungeon_small_key(self) -> bool:
        return bool(self.small_key_of)

    @property
    def is_big_key(self) -> bool:
        return bool(self.big_key_of)

    @property
    def is_compass(self) -> bool:
        return bool(self.compass_of)

    @property
    def is_dungeon_map(self) -> bool:
        return bool(self.map_of)

    def __repr__(self) -> str:
        return f"Item({self.name})"


@dataclass(eq=False)
class Location:
    index: int
    name: str
    original_item: Optional[Item]
    categories: frozenset
    goal_name: Optional[str]
    hint_priority: str
    metadata: dict
    accesses: list = field(default_factory=list)  # LocationAccess indices

    def has(self, *categories: str) -> bool:
        return all(c in self.categories for c in categories)

    def __repr__(self) -> str:
        return f"Location({self.name})"


@dataclass(eq=False)
class Area:
    index: int
    name: str
    region: str = ""
    map_sector: str = ""
    twilight: str = ""
    dungeon_start: bool = False
    can_warp: bool = False
    can_change_time: bool = False
    can_transform: bool = True
    twilight_macro: int = -1  # macro index of "Can Complete X Twilight" while not cleared
    event_reqs: list = field(default_factory=list)  # (event index, Req)
    events: list = field(default_factory=list)      # (event index, compiled fn)
    locations: list = field(default_factory=list)   # LocationAccess indices
    exits: list = field(default_factory=list)       # Exit indices
    entrances: list = field(default_factory=list)   # Exit indices


@dataclass(eq=False)
class LocationAccess:
    index: int
    location: Location
    area: Area
    req: Req
    fn: Callable = None


@dataclass(eq=False)
class Exit:
    index: int
    parent: Area
    target: Area
    req: Req
    fn: Callable = None
    twilight_gate: bool = False
    disabled: bool = False
    potential: int = 0b1111  # exit form/time cache (CacheExitTimeForms)

    @property
    def name(self) -> str:
        return f"{self.parent.name} -> {self.target.name}"


class LogicWorld:
    """One player's logic graph for one set of randomizer settings."""

    def __init__(self, settings: dict[str, str], data: Optional[dict] = None) -> None:
        self.data = data or load_data()

        # Settings -------------------------------------------------------------------------
        self.setting_infos: dict[str, dict] = {s["name"]: s for s in self.data["settings"]}
        self.settings: dict[str, str] = {name: info["default"] for name, info in self.setting_infos.items()}
        for name, value in settings.items():
            if name not in self.setting_infos:
                raise LogicError(f'Setting "{name}" is not a known setting')
            if value not in self.setting_infos[name]["options"]:
                raise LogicError(f'"{value}" is not a valid option for setting "{name}"')
            self.settings[name] = value
        self.resolve_conflicting_settings(self.settings)

        # Items ----------------------------------------------------------------------------
        self.items: list[Item] = []
        self.item_table: dict[str, Item] = {}
        for raw in self.data["items"]:
            item = Item(len(self.items), raw["name"], raw["id"], raw["importance"], raw["game_winning"],
                        raw["small_key_of"], raw["big_key_of"], raw["compass_of"], raw["map_of"])
            self.items.append(item)
            self.item_table[item.name] = item
        self.golden_bugs = [i for i in self.items if i.golden_bug]
        self.shadow_crystal = self.item_table["Shadow Crystal"]

        # Dungeons (World::BuildItemTable creates them from the dungeon item fields) ----------
        self.dungeons: dict[str, dict] = {}
        for item in self.items:
            if item.small_key_of:
                self._dungeon(item.small_key_of)["small_key"] = item
            elif item.big_key_of:
                self._dungeon(item.big_key_of)["big_key"] = item
            elif item.compass_of:
                self._dungeon(item.compass_of)["compass"] = item
            elif item.map_of:
                self._dungeon(item.map_of)["map"] = item

        # Locations ------------------------------------------------------------------------
        self.locations: list[Location] = []
        self.location_table: dict[str, Location] = {}
        self.removed_locations: set[str] = set()
        for raw in self.data["locations"]:
            original = raw["original_item"]
            if self._should_remove_location(raw["name"], original):
                self.removed_locations.add(raw["name"])
                continue
            loc = Location(len(self.locations), raw["name"],
                           None if original == "Nothing" else self.get_item(original),
                           frozenset(raw["categories"]), raw["goal_name"], raw["hint_priority"],
                           raw["metadata"])
            self.locations.append(loc)
            self.location_table[loc.name] = loc

        # Macros (parsed in file order: a macro only sees the macros defined before it) ----
        self.events: dict[str, int] = {}
        self.event_names: list[str] = []
        self.macro_index: dict[str, int] = {}
        self.macros: list[Req] = []
        self._simplified_macros: dict[int, Req] = {}
        for name, req_str in self.data["macros"]:
            req = parse_requirement(req_str, self)
            self.macros.append(req)
            self.macro_index[name] = len(self.macros) - 1

        # World graph ----------------------------------------------------------------------
        self.areas: list[Area] = []
        self.area_table: dict[str, Area] = {}
        self.exits: list[Exit] = []
        self.location_accesses: list[LocationAccess] = []
        self._load_world_graph()

        # Code generation ------------------------------------------------------------------
        self.macro_functions: list[Callable] = [None] * len(self.macros)
        self.codegen = CodeGen(self)
        for index in range(len(self.macros)):
            self.macro_functions[index] = self.codegen.compile(self.simplified_macro(index),
                                                               f"<macro {index}>")
        for area in self.areas:
            area.events = [(event, self.codegen.compile(simplify(req, self), f"<event {event}>"))
                           for event, req in area.event_reqs]
        for la in self.location_accesses:
            la.fn = self.codegen.compile(simplify(la.req, self), la.location.name)
        for ex in self.exits:
            ex.fn = self.codegen.compile(simplify(ex.req, self), ex.name)

        self.twilight_fn: dict[int, Callable] = {}
        for area in self.areas:
            if area.twilight_macro != -1 and area.twilight_macro not in self.twilight_fn:
                self.twilight_fn[area.twilight_macro] = self.macro_functions[area.twilight_macro]

        self.root = self.area_table["Root"]

    @staticmethod
    def resolve_conflicting_settings(s: dict[str, str]) -> None:
        """World::ResolveConflictingSettings"""
        if (s["Bonks Do Damage"] == "On" and s["Logic Damage Multiplier"] == "OHKO" and
                (s["Eldin Twilight Cleared"] == "Off" or s["Lanayru Twilight Cleared"] == "Off")):
            s["Bonks Do Damage"] = "Off"
        if s["Starting Form"] == "Wolf" and s["Skip Prologue"] == "Off":
            s["Skip Prologue"] = "On"

    # -- lookups used by the parser ------------------------------------------------------------

    def _dungeon(self, name: str) -> dict:
        return self.dungeons.setdefault(name, {"name": name})

    def find_item(self, name: str) -> Optional[Item]:
        return self.item_table.get(name)

    def get_item(self, name: str) -> Item:
        try:
            return self.item_table[name]
        except KeyError:
            raise LogicError(f'Unknown item name "{name}"') from None

    def get_macro_index(self, name: str) -> int:
        return self.macro_index.get(name, -1)

    def get_event_index(self, name: str, add_if_missing: bool = True) -> int:
        index = self.events.get(name)
        if index is None:
            if not add_if_missing:
                return -1
            index = len(self.event_names)
            self.events[name] = index
            self.event_names.append(name)
        return index

    def has_setting(self, name: str) -> bool:
        return name in self.setting_infos

    def setting(self, name: str) -> str:
        if name not in self.settings:
            raise LogicError(f'Setting "{name}" is not a known setting')
        return self.settings[name]

    def setting_index(self, name: str) -> int:
        return self.option_index(name, self.setting(name))

    def option_index(self, name: str, option: str) -> int:
        if name not in self.setting_infos:
            raise LogicError(f'Setting "{name}" is not a known setting')
        options = self.setting_infos[name]["options"]
        if option not in options:
            raise LogicError(f'"{option}" is not a valid option for setting "{name}"')
        return options.index(option)

    def is_(self, name: str, option: str) -> bool:
        return self.setting(name) == option

    def simplified_macro(self, index: int) -> Req:
        if index not in self._simplified_macros:
            self._simplified_macros[index] = simplify(self.macros[index], self)
        return self._simplified_macros[index]

    # -- world construction ------------------------------------------------------------------

    def _should_remove_location(self, name: str, original: str) -> bool:
        s = self.settings
        for region in ("Faron", "Eldin", "Lanayru"):
            if original == f"{region} Twilight Tear" and s[f"{region} Twilight Cleared"] == "On":
                return True
        quest = self.option_index("Ilia Memory Quest", s["Ilia Memory Quest"])
        letter = self.option_index("Ilia Memory Quest", "Letter")
        invoice = self.option_index("Ilia Memory Quest", "Invoice")
        statue = self.option_index("Ilia Memory Quest", "Statue")
        charm = self.option_index("Ilia Memory Quest", "Charm")
        return ((quest >= letter and name == "Renados Letter") or
                (quest >= invoice and name == "Telma Invoice") or
                (quest >= statue and name == "Wooden Statue") or
                (quest >= charm and name == "Ilia Charm"))

    def get_area(self, name: str, create: bool = False) -> Area:
        area = self.area_table.get(name)
        if area is None:
            if not create:
                raise LogicError(f'Unknown area name "{name}"')
            area = Area(len(self.areas), name)
            self.areas.append(area)
            self.area_table[name] = area
        return area

    def _load_world_graph(self) -> None:
        defined_events: set[int] = set()
        defined_areas: set[str] = set()
        transform_anywhere = self.settings["Logic Transform Anywhere"] == "On"

        for node in self.data["areas"]:
            name = node["name"]
            area = self.get_area(name, create=True)
            defined_areas.add(name)
            area.region = node["region"]
            area.map_sector = node["map_sector"]
            area.twilight = node["twilight"]
            area.dungeon_start = node["dungeon_start"]
            area.can_warp = node["can_warp"]
            area.can_change_time = node["can_change_time"]

            can_transform = node["can_transform"]
            if can_transform not in ("Always", "If Transform Anywhere", "Never"):
                raise LogicError(f'Unknown Can Transform Status "{can_transform}" in area "{name}".')
            area.can_transform = (can_transform == "Always" or
                                  (transform_anywhere and can_transform == "If Transform Anywhere"))

            if area.twilight:
                macro = f"Can Complete {area.twilight} Twilight"
                index = self.get_macro_index(macro)
                if index == -1:
                    raise LogicError(f'"{macro}" is not a macro that exists')
                if self.settings[f"{area.twilight} Twilight Cleared"] == "Off":
                    area.twilight_macro = index

            # std::map semantics: sorted by name, explicit events win over generated ones
            event_nodes: dict[str, str] = {}
            for event_name, req_str in node["events"]:
                event_nodes.setdefault(event_name, req_str)
            event_nodes.setdefault(f"Can Access {name}", "Nothing")
            if area.can_warp:
                event_nodes.setdefault("Can Warp", "Nothing")
            if area.map_sector:
                event_nodes.setdefault(f"{area.map_sector} Map Sector", "Nothing")
            for event_name in sorted(event_nodes):
                req = parse_requirement(event_nodes[event_name], self)
                index = self.get_event_index(event_name)
                area.event_reqs.append((index, req))
                defined_events.add(index)

            for loc_name, req_str in node["locations"]:
                if loc_name in self.removed_locations:
                    continue
                location = self.location_table.get(loc_name)
                if location is None:
                    raise LogicError(f'Unknown location name "{loc_name}"')
                if area.twilight and not (location.original_item is not None and
                                          location.original_item.name.endswith("Twilight Tear")):
                    req_str = f"Not_Twilight and ({req_str})"
                la = LocationAccess(len(self.location_accesses), location, area,
                                    parse_requirement(req_str, self))
                self.location_accesses.append(la)
                area.locations.append(la.index)
                location.accesses.append(la.index)

            for target_name, req_str in node["exits"]:
                target = self.get_area(target_name, create=True)
                ex = Exit(len(self.exits), area, target, parse_requirement(req_str, self))
                self.exits.append(ex)
                area.exits.append(ex.index)

        for event_name, index in self.events.items():
            if index not in defined_events:
                raise LogicError(f'Event "{event_name}" is used but never defined.')
        for area_name in self.area_table:
            if area_name not in defined_areas:
                raise LogicError(f'Area "{area_name}" is used but never defined.')
        for ex in self.exits:
            ex.target.entrances.append(ex.index)

        for gate in ("Ordon Bridge -> South Faron Woods",
                     "Faron Field -> Kakariko Gorge",
                     "North Eldin Field -> Lanayru Field"):
            self.get_exit(gate).twilight_gate = True

    def get_exit(self, name: str) -> Exit:
        parent_name, _, target_name = name.partition(" -> ")
        parent = self.get_area(parent_name)
        target = self.get_area(target_name)
        for index in parent.exits:
            if self.exits[index].target is target:
                return self.exits[index]
        raise LogicError(f'"{name}" is not a known connection')

    # -- helpers for the Archipelago world -----------------------------------------------------

    def logic_items(self) -> set[Item]:
        """Every item any requirement in the graph can depend on (after settings folding)."""
        out: set[Item] = set()
        for la in self.location_accesses:
            out |= items_in(simplify(la.req, self), self)
        for ex in self.exits:
            out |= items_in(simplify(ex.req, self), self)
        for area in self.areas:
            for _, req in area.event_reqs:
                out |= items_in(simplify(req, self), self)
            if area.twilight_macro != -1:
                out |= items_in(self.simplified_macro(area.twilight_macro), self)
        out.add(self.shadow_crystal)  # form/time expansion depends on it directly
        return out

    def dungeon_locations(self) -> dict[str, list[Location]]:
        """Port of Area::AssignHintRegionsAndDungeonLocations for dungeon membership."""
        dungeon_names = set(self.dungeons)
        result: dict[str, list[Location]] = {name: [] for name in dungeon_names}
        seen: dict[str, set[int]] = {name: set() for name in dungeon_names}
        for area in self.areas:
            regions = self.hint_regions(area)
            dungeon_regions = {r for r in regions if r in dungeon_names}
            if len(dungeon_regions) < len(regions):
                regions = regions - dungeon_regions
            for region in regions:
                if region in dungeon_names:
                    for la_index in area.locations:
                        loc = self.location_accesses[la_index].location
                        if loc.index not in seen[region]:
                            seen[region].add(loc.index)
                            result[region].append(loc)
        return result

    def hint_regions(self, area: Area) -> set[str]:
        regions: set[str] = set()
        checked: set[int] = set()
        queue = [area]
        while queue:
            current = queue.pop()
            checked.add(current.index)
            if current.region:
                if current.region != "None":
                    regions.add(current.region)
                continue
            for ex_index in current.entrances:
                parent = self.exits[ex_index].parent
                if parent.index not in checked:
                    queue.append(parent)
        return regions
