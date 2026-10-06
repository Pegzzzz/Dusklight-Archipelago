"""Archipelago world for Twilight Princess on Dusklight (the PC reimplementation), driven by the
Dusklight randomizer's own data and logic."""

from __future__ import annotations

import hashlib
import logging
from collections import Counter
from typing import Any, Callable, ClassVar, Optional

from BaseClasses import CollectionState, Item, ItemClassification, Location, LocationProgressType, MultiWorld, \
    Region, Tutorial
from Fill import fill_restrictive
from Options import OptionError
from worlds.AutoWorld import LogicMixin, WebWorld, World

from .items import GAME, ITEM_ID_BASE, TPDusklightItem, classification_for, item_data, item_name_groups, \
    item_name_to_id
from .locations import VICTORY_LOCATION, TPDusklightLocation, location_data, location_name_groups, \
    location_name_to_id
from .logic import rando_rules
from .logic.requirement import LogicError
from .logic.search import SearchState, compute_exit_cache
from .logic.world_graph import Area, LogicWorld, load_data
from .options import SETTING_MAP, TPDusklightOptions, option_groups, option_to_setting

logger = logging.getLogger("Twilight Princess Dusklight")

APWORLD_VERSION = "0.1.0"
SLOT_DATA_VERSION = 1

GOAL_ITEMS = ("Progressive Fused Shadow", "Progressive Mirror Shard")
RESTRICTED_MODES = ("own_dungeon", "any_dungeon", "overworld")

# Settings the in-game generator always gets in Archipelago mode. The randomizer's hints talk
# about the items at locations, which are mostly other players' items here, so its hint system
# is off; logic already ran in Archipelago.
FORCED_IN_GAME_SETTINGS = {
    "Logic Rules": "No Logic",
    "Number of Path Hints": "0",
    "Path Hints on Midna": "Off",
    "Path Hints on Hint Signs": "Off",
    "Number of Barren Hints": "0",
    "Barren Hints on Midna": "Off",
    "Barren Hints on Hint Signs": "Off",
    "Number of Item Hints": "0",
    "Item Hints on Midna": "Off",
    "Item Hints on Hint Signs": "Off",
    "Number of Location Hints": "0",
    "Location Hints on Midna": "Off",
    "Location Hints on Hint Signs": "Off",
    "Agitha Hints": "Off",
}


class TPDusklightLogicState(LogicMixin):
    """Each CollectionState carries a resumable logic search per Dusklight player."""

    tpd_searches: dict[int, SearchState]

    def init_mixin(self, multiworld: MultiWorld) -> None:
        self.tpd_searches = {}

    def copy_mixin(self, new_state: CollectionState) -> CollectionState:
        new_state.tpd_searches = {player: search.copy() for player, search in self.tpd_searches.items()}
        return new_state


class TPDusklightWeb(WebWorld):
    theme = "grassFlowers"
    option_groups = option_groups
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "How to set up Twilight Princess on Dusklight for Archipelago.",
        "English",
        "setup_en.md",
        "setup/en",
        ["Elliot"],
    )]


class TPDusklightWorld(World):
    """Twilight Princess, played on Dusklight, the PC reimplementation of the game. Link must save
    Hyrule from the Twilight Realm with Midna's help, as a hero and as a wolf. Every check the
    Dusklight randomizer supports can hold items from the multiworld."""

    game = GAME
    web = TPDusklightWeb()
    options_dataclass = TPDusklightOptions
    options: TPDusklightOptions
    topology_present = False
    required_client_version = (0, 6, 0)

    item_name_to_id: ClassVar[dict[str, int]] = item_name_to_id
    location_name_to_id: ClassVar[dict[str, int]] = location_name_to_id
    item_name_groups = item_name_groups
    location_name_groups = location_name_groups

    lw: LogicWorld
    settings_map: dict[str, str]
    pool: Counter
    starting: Counter
    vanilla: dict[str, str]
    internal: dict[int, int]
    base_counts: list[int]
    logic_item_names: set[str]

    def __init__(self, multiworld: MultiWorld, player: int) -> None:
        super().__init__(multiworld, player)
        self.pre_fill_pool: list[Item] = []
        self.goal_placements: dict[str, str] = {}
        self.required_dungeons: set[str] = set()
        self.barren_dungeons: set[str] = set()
        self.barren_locations: dict[str, set[str]] = {}
        self._base_search: Optional[SearchState] = None
        self._area_regions_cache: dict[int, set[str]] = {}

    # ----------------------------------------------------------------------------------------
    # Settings and the logic world

    def generate_early(self) -> None:
        settings = {name: option_to_setting(getattr(self.options, attr)) for attr, name in SETTING_MAP.items()}
        try:
            self.lw = LogicWorld(settings)
        except LogicError as error:
            raise OptionError(f"{self.player_name}: invalid Twilight Princess Dusklight options: {error}") from error
        lw = self.lw
        self.settings_map = dict(lw.settings)  # includes World::ResolveConflictingSettings changes

        self.pool = rando_rules.item_pool(lw)
        self.starting = rando_rules.starting_items(lw, self.pool, {})
        self.vanilla = rando_rules.vanilla_placements(lw, self.pool)
        self.conflict_locations = set(rando_rules.nonprogress_conflicts(lw, self.vanilla, self.starting))
        self.internal = {lw.location_table[loc].index: lw.get_item(item).index
                         for loc, item in self.vanilla.items() if item != "Nothing"}

        self.base_counts = [0] * len(lw.items)
        for name, count in self.starting.items():
            self.base_counts[lw.get_item(name).index] += count

        self.logic_item_names = {item.name for item in lw.logic_items()}
        self.item_index = {item.name: item.index for item in lw.items}
        self.dungeon_locations = lw.dungeon_locations()

        # Dungeon item names exactly as fill.cpp groups them (Dungeon::GetSmallKey, ...)
        self.dungeon_small: dict[str, set[str]] = {}
        self.dungeon_big: dict[str, set[str]] = {}
        self.dungeon_maps: dict[str, set[str]] = {}
        for name, dungeon in lw.dungeons.items():
            small = {dungeon["small_key"].name} if "small_key" in dungeon else set()
            if name == "Snowpeak Ruins":
                small |= {"Ordon Pumpkin", "Ordon Cheese"}
            self.dungeon_small[name] = small
            self.dungeon_big[name] = {dungeon["big_key"].name} if "big_key" in dungeon else set()
            self.dungeon_maps[name] = {dungeon[k].name for k in ("compass", "map") if k in dungeon}

        # Exit form/time cache (fill::CacheExitTimeForms), with everything this world could own
        compute_exit_cache(lw, self._full_counts(), self.internal)

        # search::VerifyLogic: the randomizer refuses settings with unreachable locations
        missing = rando_rules.unreachable_with_everything(lw, self.pool, self.starting, self.internal)
        if "Defeat Ganondorf" in missing:
            raise OptionError(f"{self.player_name}: these Twilight Princess Dusklight options make the game "
                              f"impossible to beat (for example more hearts or Poe Souls required than exist).")
        if missing and self.options.accessibility != "minimal":
            raise OptionError(
                f"{self.player_name}: with these options some Twilight Princess Dusklight locations can never "
                f"be reached ({', '.join(missing[:5])}). Change the options or use accessibility: minimal.")

        for attr, group in (("small_keys", "Small Keys"), ("big_keys", "Big Keys")):
            if getattr(self.options, attr).current_key == "own_world":
                self.options.local_items.value |= set(item_name_groups[group])
        if self.options.maps_and_compasses.current_key == "own_world":
            self.options.local_items.value |= item_name_groups["Compasses"] | item_name_groups["Dungeon Maps"]

    def _full_counts(self, minus: Counter = Counter()) -> list[int]:
        counts = list(self.base_counts)
        for name, count in self.pool.items():
            counts[self.lw.get_item(name).index] += count
        for name, count in minus.items():
            counts[self.lw.get_item(name).index] -= count
        return counts

    # ----------------------------------------------------------------------------------------
    # Regions, locations and items

    def ap_location_names(self) -> list[str]:
        """Locations that become Archipelago locations: everything the randomizer would fill."""
        return [loc.name for loc in self.lw.locations if loc.name in location_data and loc.name not in self.vanilla]

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        hyrule = Region("Hyrule", self.player, self.multiworld)
        self.multiworld.regions += [menu, hyrule]
        menu.connect(hyrule)

        for name in self.ap_location_names():
            location = TPDusklightLocation(self.player, name, location_name_to_id[name], hyrule)
            if name in self.conflict_locations:
                location.progress_type = LocationProgressType.EXCLUDED
            hyrule.locations.append(location)

        victory = TPDusklightLocation(self.player, VICTORY_LOCATION, None, hyrule)
        victory.place_locked_item(TPDusklightItem("Victory", ItemClassification.progression, None, self.player))
        hyrule.locations.append(victory)

    def create_item(self, name: str) -> TPDusklightItem:
        raw = item_data[name]
        # Item link group worlds never run generate_early; Archipelago merges in the linked
        # players' classifications afterwards.
        logic_items = getattr(self, "logic_item_names", None)
        if logic_items is None:
            classification = (ItemClassification.trap if name == "Foolish Item" else ItemClassification.filler)
        else:
            classification = classification_for(name, logic_items, raw["importance"])
        return TPDusklightItem(name, classification, item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return self.random.choice(sorted(rando_rules.INITIAL_JUNK_POOL))

    def _restricted_mode(self, name: str) -> Optional[str]:
        """The fill.cpp placement step an item belongs to, if it is placed before the main fill."""
        for dungeon in self.dungeon_small:
            if name in self.dungeon_small[dungeon]:
                mode = self.options.small_keys.current_key
                return mode if mode in RESTRICTED_MODES else None
            if name in self.dungeon_big[dungeon]:
                mode = self.options.big_keys.current_key
                return mode if mode in RESTRICTED_MODES else None
            if name in self.dungeon_maps[dungeon]:
                mode = self.options.maps_and_compasses.current_key
                return mode if mode in RESTRICTED_MODES else None
        return None

    def _held_for_pre_fill(self, name: str) -> bool:
        if name in GOAL_ITEMS and not self.options.dungeon_rewards_can_be_anywhere:
            return True
        return self._restricted_mode(name) is not None

    def create_items(self) -> None:
        location_count = len(self.ap_location_names())
        items: list[Item] = [self.create_item(name) for name, count in self.pool.items() for _ in range(count)]

        if len(items) < location_count:
            # World::SanitizeItemPool: junk (and traps) up to the number of locations
            items += [self.create_item(name)
                      for name in rando_rules.junk_items(self.lw, location_count - len(items), self.random)]
        elif len(items) > location_count:
            excess = len(items) - location_count
            for classification in (ItemClassification.filler, ItemClassification.trap, ItemClassification.useful):
                for item in [i for i in items if i.classification == classification]:
                    if not excess:
                        break
                    items.remove(item)
                    excess -= 1
            if excess:
                raise OptionError(f"{self.player_name}: not enough Twilight Princess Dusklight locations for "
                                  f"its progression items; shuffle more kinds of locations.")

        for item in items:
            if self._held_for_pre_fill(item.name):
                self.pre_fill_pool.append(item)
            else:
                self.multiworld.itempool.append(item)

    def get_pre_fill_items(self) -> list[Item]:
        return list(self.pre_fill_pool)

    # ----------------------------------------------------------------------------------------
    # Rules

    def _search(self, state: CollectionState) -> SearchState:
        search = state.tpd_searches.get(self.player)
        if search is None:
            if self._base_search is None:
                self._base_search = SearchState(self.lw, self.internal, self.base_counts)
                self._base_search.run()
            search = self._base_search.copy()
            index = self.item_index
            for name, count in state.prog_items[self.player].items():
                item_index = index.get(name)
                if item_index is not None and count > 0:
                    search.add_item(item_index, count)
            state.tpd_searches[self.player] = search
        return search

    def collect(self, state: CollectionState, item: Item) -> bool:
        changed = super().collect(state, item)
        if changed:
            search = state.tpd_searches.get(self.player)
            if search is not None:
                item_index = self.item_index.get(item.name)
                if item_index is not None:
                    search.add_item(item_index)
        return changed

    def remove(self, state: CollectionState, item: Item) -> bool:
        changed = super().remove(state, item)
        if changed:
            state.tpd_searches.pop(self.player, None)
        return changed

    def set_rules(self) -> None:
        lw = self.lw
        for location in self.get_region("Hyrule").locations:
            name = "Defeat Ganondorf" if location.name == VICTORY_LOCATION else location.name
            index = lw.location_table[name].index
            location.access_rule = (lambda state, i=index: self._search(state).can_reach(i))
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Victory", self.player)

        # World::SetForbiddenItems: keep this world's small keys off boss rewards
        if not self.options.small_keys_on_bosses:
            small_keys = set(rando_rules.SMALL_KEY_LIKE)
            for names in self.dungeon_small.values():
                small_keys |= names
            for loc in rando_rules.boss_locations(lw):
                location = self._ap_location(loc.name)
                if location is not None:
                    self._add_item_rule(location, lambda item, keys=small_keys:
                                        not (item.player == self.player and item.name in keys))

    def _ap_location(self, name: str) -> Optional[Location]:
        if name not in location_data or name in self.vanilla:
            return None
        return self.get_location(name)

    @staticmethod
    def _add_item_rule(location: Location, rule: Callable[[Item], bool]) -> None:
        old = location.item_rule
        location.item_rule = lambda item, old=old: old(item) and rule(item)

    # ----------------------------------------------------------------------------------------
    # Pre-fill (fill.cpp PlaceRestrictedItems): dungeon rewards, required/barren dungeons and
    # dungeon-restricted keys, maps and compasses.

    def _maximum_state(self) -> CollectionState:
        state = CollectionState(self.multiworld)
        for item in self.multiworld.itempool:
            state.collect(item, True)
        for world in self.multiworld.worlds.values():
            if world is not self:
                for item in world.get_pre_fill_items():
                    state.collect(item, True)
        for item in self.pre_fill_pool:
            state.collect(item, True)
        state.sweep_for_advancements()
        return state

    def _take(self, names: set[str]) -> list[Item]:
        taken = [item for item in self.pre_fill_pool if item.name in names]
        for item in taken:
            self.pre_fill_pool.remove(item)
        return taken

    def _place(self, items: list[Item], locations: list[Location], step: str) -> None:
        progression = [item for item in items if item.advancement]
        other = [item for item in items if not item.advancement]
        if progression:
            state = self._maximum_state()  # everything not being placed right now
            spots = [loc for loc in locations if loc.item is None]
            self.random.shuffle(spots)
            fill_restrictive(self.multiworld, state, spots, progression, single_player_placement=True, lock=True,
                             allow_excluded=False, name=f"Twilight Princess Dusklight {step}")
            if progression:
                raise OptionError(f"{self.player_name}: could not place {[i.name for i in progression]} ({step}).")
        if other:
            spots = [loc for loc in locations if loc.item is None]
            self.random.shuffle(spots)
            for item in other:
                spot = next((loc for loc in spots if loc.item is None and loc.can_fill(None, item, False)), None)
                if spot is None:
                    raise OptionError(f"{self.player_name}: no room left for {item.name} ({step}).")
                spot.place_locked_item(item)

    def _dungeon_spots(self, dungeon: str, include_nonprogress: bool) -> list[Location]:
        """Empty locations of a dungeon. fill.cpp skips non-progress ones in required dungeons."""
        spots = []
        for loc in self.dungeon_locations.get(dungeon, []):
            location = self._ap_location(loc.name)
            if location is None or location.item is not None:
                continue
            if not include_nonprogress and location.progress_type == LocationProgressType.EXCLUDED:
                continue
            spots.append(location)
        return spots

    def pre_fill(self) -> None:
        lw = self.lw

        # PlaceGoalLocationItems
        if not self.options.dungeon_rewards_can_be_anywhere:
            goal_items = self._take(set(GOAL_ITEMS))
            goal_locations = [location for location in (self._ap_location(loc.name) for loc in lw.locations
                                                        if loc.goal_name is not None)
                              if location is not None and location.item is None
                              and location.progress_type != LocationProgressType.EXCLUDED]
            if len(goal_items) > len(goal_locations):
                raise OptionError(f"{self.player_name}: not enough dungeon reward locations for the Fused "
                                  f"Shadows and Mirror Shards (are some of them excluded?).")
            self._place(goal_items, goal_locations, "Dungeon Rewards")
            for location in goal_locations:
                if location.item is not None and location.item.player == self.player and \
                        location.item.name in GOAL_ITEMS:
                    self.goal_placements[location.name] = location.item.name

        self._determine_required_dungeons()

        # Barren dungeons (and locations that depend on them) become non-progress: nothing anyone
        # needs, except the restricted dungeon keys fill.cpp still places in unrequired dungeons.
        allowed = self._barren_allowed_keys()
        dungeon_of: dict[str, set[str]] = {}
        for dungeon, locs in self.dungeon_locations.items():
            for loc in locs:
                dungeon_of.setdefault(loc.name, set()).add(dungeon)
        non_progress: dict[str, set[str]] = {}
        for dungeon in self.barren_dungeons:
            for name in self.barren_locations[dungeon]:
                keys = non_progress.setdefault(name, set())
                for owner in dungeon_of.get(name, ()):
                    keys |= allowed.get(owner, set())
        for name, keys in non_progress.items():
            location = self._ap_location(name)
            if location is not None:
                self._add_item_rule(location, lambda item, keys=frozenset(keys): not item.advancement or
                                    (item.player == self.player and item.name in keys))

        small_mode = self.options.small_keys.current_key
        big_mode = self.options.big_keys.current_key
        map_mode = self.options.maps_and_compasses.current_key

        # PlaceOwnDungeonItems
        for dungeon in lw.dungeons:
            include_nonprogress = dungeon not in self.required_dungeons
            if small_mode == "own_dungeon":
                self._place(self._take(self.dungeon_small[dungeon]),
                            self._dungeon_spots(dungeon, include_nonprogress), f"{dungeon} Small Keys")
            if big_mode == "own_dungeon":
                self._place(self._take(self.dungeon_big[dungeon]),
                            self._dungeon_spots(dungeon, include_nonprogress), f"{dungeon} Big Key")
            if map_mode == "own_dungeon":
                self._place(self._take(self.dungeon_maps[dungeon]),
                            self._dungeon_spots(dungeon, True), f"{dungeon} Map and Compass")

        # PlaceAnyDungeonItems: barren and non-barren dungeons are separate pools
        for barren in (False, True):
            dungeons = [d for d in lw.dungeons if (d in self.barren_dungeons) == barren]
            wanted: set[str] = set()
            for dungeon in dungeons:
                if small_mode == "any_dungeon":
                    wanted |= self.dungeon_small[dungeon]
                if big_mode == "any_dungeon":
                    wanted |= self.dungeon_big[dungeon]
                if map_mode == "any_dungeon":
                    wanted |= self.dungeon_maps[dungeon]
            items = self._take(wanted)
            if items:
                spots = [spot for dungeon in dungeons for spot in self._dungeon_spots(dungeon, barren)]
                self._place(items, spots, "Any Dungeon Items")

        # PlaceOverworldItems
        wanted = set()
        for dungeon in lw.dungeons:
            if small_mode == "overworld":
                wanted |= self.dungeon_small[dungeon]
            if big_mode == "overworld":
                wanted |= self.dungeon_big[dungeon]
            if map_mode == "overworld":
                wanted |= self.dungeon_maps[dungeon]
        items = self._take(wanted)
        if items:
            in_dungeons = {loc.name for locs in self.dungeon_locations.values() for loc in locs}
            spots = [location for location in (self._ap_location(name) for name in self.ap_location_names()
                                               if name not in in_dungeons)
                     if location is not None and location.item is None
                     and location.progress_type != LocationProgressType.EXCLUDED]
            self._place(items, spots, "Overworld Dungeon Items")

        if self.pre_fill_pool:
            logger.warning(f"{self.player_name}: returning {[i.name for i in self.pre_fill_pool]} to the item pool")
            self.multiworld.itempool += self.pre_fill_pool
            self.pre_fill_pool = []

    def _barren_allowed_keys(self) -> dict[str, set[str]]:
        small_mode = self.options.small_keys.current_key
        big_mode = self.options.big_keys.current_key
        any_dungeon: set[str] = set()
        for dungeon in self.barren_dungeons:
            if small_mode == "any_dungeon":
                any_dungeon |= self.dungeon_small[dungeon]
            if big_mode == "any_dungeon":
                any_dungeon |= self.dungeon_big[dungeon]
        out = {}
        for dungeon in self.barren_dungeons:
            own = set(any_dungeon)
            if small_mode == "own_dungeon":
                own |= self.dungeon_small[dungeon]
            if big_mode == "own_dungeon":
                own |= self.dungeon_big[dungeon]
            out[dungeon] = own
        return out

    def _area_regions(self, area: Area) -> set[str]:
        regions = self._area_regions_cache.get(area.index)
        if regions is None:
            regions = self.lw.hint_regions(area)
            dungeon_regions = {r for r in regions if r in self.lw.dungeons}
            if len(dungeon_regions) < len(regions):
                regions = regions - dungeon_regions
            self._area_regions_cache[area.index] = regions
        return regions

    def _starting_entrances(self, dungeon: str) -> list[int]:
        """World::AssignAreaProperties: exits that lead into the dungeon from outside it."""
        return [ex.index for ex in self.lw.exits
                if dungeon in self._area_regions(ex.target) and dungeon not in self._area_regions(ex.parent)]

    def _determine_required_dungeons(self) -> None:
        """World::DetermineDungeonDependentLocations + World::DetermineRequiredDungeons."""
        lw = self.lw
        ganon = lw.location_table["Defeat Ganondorf"].index
        internal_with_goals = dict(self.internal)
        for loc_name, item_name in self.goal_placements.items():
            internal_with_goals[lw.location_table[loc_name].index] = lw.get_item(item_name).index
        counts_after_goals = self._full_counts(Counter(self.goal_placements.values()))
        full_counts = self._full_counts()
        barren_setting = bool(self.options.unrequired_dungeons_are_barren)

        for dungeon in lw.dungeons:
            if dungeon == "Hyrule Castle":  # implicitly required
                self.required_dungeons.add(dungeon)
                continue
            disabled = self._starting_entrances(dungeon)
            beatable = SearchState(lw, internal_with_goals, counts_after_goals, disabled)
            beatable.run()
            if not beatable.reached[ganon]:
                self.required_dungeons.add(dungeon)
            elif barren_setting:
                dungeon_names = {loc.name for loc in self.dungeon_locations.get(dungeon, [])}
                dependent = SearchState(lw, self.internal, full_counts, disabled)
                dependent.run()
                outside = {loc.name for loc in lw.locations
                           if loc.name not in dungeon_names and not dependent.reached[loc.index]}
                self.barren_dungeons.add(dungeon)
                self.barren_locations[dungeon] = dungeon_names | outside

    # ----------------------------------------------------------------------------------------
    # Output

    def in_game_seed(self) -> str:
        digest = hashlib.sha256(f"{self.multiworld.seed_name}:{self.player}".encode()).hexdigest()
        return f"AP{digest[:14]}"

    def fill_slot_data(self) -> dict[str, Any]:
        settings = dict(self.settings_map)
        settings.update(FORCED_IN_GAME_SETTINGS)
        locations = []
        for location in self.get_region("Hyrule").locations:
            if location.address is None or location.item is None:
                continue
            item = location.item
            own = item.player == self.player and item.game == GAME
            flags = (1 if item.advancement else 0) | (2 if item.useful else 0) | (4 if item.trap else 0)
            locations.append([location.name, location.address, self.multiworld.get_player_name(item.player),
                              item.name, flags, 1 if own else 0])
        return {
            "slot_data_version": SLOT_DATA_VERSION,
            "apworld_version": APWORLD_VERSION,
            "randomizer_data": load_data()["source"]["randomizer_commit"],
            "seed": self.in_game_seed(),
            "settings": settings,
            "item_id_base": ITEM_ID_BASE,
            "locations": locations,
            "death_link": bool(self.options.death_link),
            "required_dungeons": sorted(self.required_dungeons),
        }

    def extend_hint_information(self, hint_data: dict[int, dict[int, str]]) -> None:
        dungeon_of = {loc.name: name for name, locs in self.dungeon_locations.items() for loc in locs}
        info = {location.address: dungeon_of[location.name] for location in self.get_region("Hyrule").locations
                if location.address is not None and location.name in dungeon_of}
        if info:
            hint_data[self.player] = info

    def write_spoiler(self, spoiler_handle) -> None:
        spoiler_handle.write(f"\n\nTwilight Princess Dusklight ({self.player_name}):\n")
        spoiler_handle.write(f"  Required dungeons: {', '.join(sorted(self.required_dungeons)) or 'none'}\n")
        spoiler_handle.write(f"  Barren dungeons: {', '.join(sorted(self.barren_dungeons)) or 'none'}\n")
