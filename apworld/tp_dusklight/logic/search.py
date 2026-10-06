"""Port of generator/logic/search.cpp (accessible-locations search).

The search tracks, for every area, which combinations of Link's form (human/wolf) and time of
day (day/night) can reach it, plus the two twilight forms. It is a monotone fixpoint over the
items owned, so it can be resumed after more items are added instead of restarting; that is
what makes it fast enough to drive Archipelago's fill.

One deliberate difference from C++: in Search::ExpandFormTimes, an area that can change time
but not transform expands *both* forms it already holds (C++ expands only the first one it
finds because of an `else if`). The C++ result depends on the order forms arrive in, which
would make the logic non-monotone; time can be changed in either form in game, so expanding
both is the faithful reading. Everything else follows the C++ step by step.
"""

from __future__ import annotations

from typing import Iterable, Optional

from .requirement import FormTime
from .world_graph import LogicWorld

NONE, PARTIAL, COMPLETE = 0, 1, 2

HUMAN = FormTime.HUMAN
WOLF = FormTime.WOLF
DAY = FormTime.DAY
NIGHT = FormTime.NIGHT
ALL = FormTime.ALL
TW = FormTime.TWILIGHT_WOLF
TH = FormTime.TWILIGHT_HUMAN
FT_NORMAL = FormTime.ALL_FORM_TIMES
FT_WITH_TWILIGHT = FormTime.ALL_FORM_TIMES_AND_TWILIGHT


class SearchState:
    """Resumable accessible-locations search for one LogicWorld.

    internal_items maps a location index to the item index the search itself collects when it
    reaches that location (vanilla, unshuffled placements that are not Archipelago locations).
    """

    __slots__ = ("w", "internal", "counts", "events", "ft", "visited", "exits_try", "exit_done",
                 "events_try", "locs_try", "loc_done", "reached", "disabled", "dirty")

    def __init__(self, world: LogicWorld, internal_items: Optional[dict[int, int]] = None,
                 counts: Optional[list[int]] = None, disabled_exits: Iterable[int] = ()) -> None:
        self.w = world
        self.internal = internal_items or {}
        self.counts = list(counts) if counts is not None else [0] * len(world.items)
        self.events = bytearray(len(world.event_names))
        self.ft = [0] * len(world.areas)
        self.visited = bytearray(len(world.areas))
        self.exit_done = bytearray(len(world.exits))
        self.loc_done = bytearray(len(world.location_accesses))
        self.reached = bytearray(len(world.locations))
        self.disabled = frozenset(disabled_exits)
        self.exits_try: list[int] = []
        self.events_try: list[tuple[int, object, int]] = []  # (event, fn, area index)
        self.locs_try: list[int] = []

        root = world.root
        self.visited[root.index] = 1
        self.ft[root.index] = ALL  # World::SetSearchStartingProperties
        for ex_index in root.exits:
            if ex_index not in self.disabled:
                self.exits_try.append(ex_index)
        # The root area's own locations are visible because the root counts as visited.
        self.locs_try.extend(root.locations)
        self.dirty = True

    # -- copying / item changes --------------------------------------------------------------

    def copy(self) -> "SearchState":
        new = SearchState.__new__(SearchState)
        new.w = self.w
        new.internal = self.internal
        new.counts = self.counts[:]
        new.events = self.events[:]
        new.ft = self.ft[:]
        new.visited = self.visited[:]
        new.exit_done = self.exit_done[:]
        new.loc_done = self.loc_done[:]
        new.reached = self.reached[:]
        new.disabled = self.disabled
        new.exits_try = self.exits_try[:]
        new.events_try = self.events_try[:]
        new.locs_try = self.locs_try[:]
        new.dirty = self.dirty
        return new

    def add_item(self, item_index: int, count: int = 1) -> None:
        had_crystal = self.counts[self.w.shadow_crystal.index] > 0
        self.counts[item_index] += count
        if item_index == self.w.shadow_crystal.index and not had_crystal:
            self._expand_all_visited()
        self.dirty = True

    # -- evaluation helpers ------------------------------------------------------------------

    def twilight_cleared(self, area) -> bool:
        index = area.twilight_macro
        if index == -1:
            return True
        return self.w.twilight_fn[index](self.counts, self.events, ALL)

    def expand_form_times(self, area) -> None:
        ft = self.ft[area.index]
        cleared = self.twilight_cleared(area)
        crystal = self.counts[self.w.shadow_crystal.index] > 0
        if area.can_change_time and area.can_transform and crystal and cleared:
            ft |= ALL
        elif area.can_change_time and cleared:
            if ft & WOLF:
                ft |= WOLF
            if ft & HUMAN:
                ft |= HUMAN
        elif area.can_transform and crystal and cleared:
            if ft & NIGHT:
                ft |= NIGHT
            if ft & DAY:
                ft |= DAY
        elif not cleared and area.can_transform and crystal:
            if ft & (TH | TW):
                ft |= TH | TW
        self.ft[area.index] = ft

    def _expand_all_visited(self) -> None:
        areas = self.w.areas
        for index, seen in enumerate(self.visited):
            if seen:
                self.expand_form_times(areas[index])

    def eval_exit(self, ex) -> int:
        if ex.index in self.disabled:
            return NONE
        parent = ex.parent
        target = ex.target
        ft = self.ft
        parent_ft = ft[parent.index]
        target_ft = ft[target.index]
        potential = ex.potential

        target_cleared = self.twilight_cleared(target)
        if not target_cleared and (self.twilight_cleared(parent) or
                                   parent.twilight_macro == target.twilight_macro):
            if parent_ft & HUMAN:
                parent_ft |= TH
            if parent_ft & WOLF:
                parent_ft |= TW
            potential |= TH | TW

        spread = ~target_ft & (parent_ft & potential)
        if not spread:
            return NONE

        success = NONE
        fn = ex.fn
        counts = self.counts
        events = self.events
        for form_time in (FT_NORMAL if target_cleared else FT_WITH_TWILIGHT):
            if form_time & spread and fn(counts, events, form_time):
                if not target_cleared:
                    if (not target_ft & TW and
                            (form_time & WOLF or form_time == TW or ex.twilight_gate)):
                        target_ft |= TW
                        success = PARTIAL
                    elif not target_ft & TH and (form_time & HUMAN or form_time == TH):
                        target_ft |= TH
                        success = PARTIAL
                else:
                    target_ft |= form_time
                    success = PARTIAL
        ft[target.index] = target_ft

        if success != NONE:
            self.expand_form_times(target)
        if target_cleared and (ft[target.index] & potential) == potential:
            success = COMPLETE
        return success

    # -- the search loop ---------------------------------------------------------------------

    def _explore(self, area, sink: list[int]) -> None:
        """Search::Explore: queue the area's events and locations and try its exits now.
        Exits that still need retrying are appended to sink."""
        w = self.w
        for event, fn in area.events:
            self.events_try.append((event, fn, area.index))
        self.locs_try.extend(area.locations)
        for ex_index in area.exits:
            ex = w.exits[ex_index]
            result = self.eval_exit(ex)
            if result == COMPLETE:
                self.exit_done[ex_index] = 1
            if result != NONE:
                target = ex.target
                if not self.visited[target.index]:
                    self.visited[target.index] = 1
                    self._explore(target, sink)
            if result != COMPLETE:
                sink.append(ex_index)

    def run(self) -> None:
        """Resume the search until nothing new can be reached."""
        if not self.dirty:
            return
        w = self.w
        exits = w.exits
        accesses = w.location_accesses
        found = True
        while found:
            found = False

            # ProcessEvents
            events = self.events
            ft = self.ft
            remaining = []
            for entry in self.events_try:
                event, fn, area_index = entry
                if events[event]:
                    continue
                if fn(self.counts, events, ft[area_index]):
                    events[event] = 1
                    found = True
                else:
                    remaining.append(entry)
            self.events_try = remaining

            # ProcessExits. Like the C++ list, exits discovered by Explore during this pass are
            # appended to the list being walked and get tried in the same pass.
            pending = self.exits_try
            retry: list[int] = []
            i = 0
            while i < len(pending):
                ex_index = pending[i]
                i += 1
                if self.exit_done[ex_index]:
                    continue
                ex = exits[ex_index]
                result = self.eval_exit(ex)
                if result != NONE:
                    found = True
                    target = ex.target
                    if not self.visited[target.index]:
                        self.visited[target.index] = 1
                        self._explore(target, pending)
                if result == COMPLETE:
                    self.exit_done[ex_index] = 1
                else:
                    retry.append(ex_index)
            self.exits_try = [x for x in dict.fromkeys(retry) if not self.exit_done[x]]

            # ProcessLocations
            remaining_locs = []
            for la_index in self.locs_try:
                if self.loc_done[la_index]:
                    continue
                la = accesses[la_index]
                if la.fn(self.counts, events, ft[la.area.index]):
                    self.loc_done[la_index] = 1
                    found = True
                    loc = la.location
                    if not self.reached[loc.index]:
                        self.reached[loc.index] = 1
                        item = self.internal.get(loc.index)
                        if item is not None:
                            self.add_item(item)
                else:
                    remaining_locs.append(la_index)
            self.locs_try = remaining_locs
        self.dirty = False

    # -- results -----------------------------------------------------------------------------

    def can_reach(self, location_index: int) -> bool:
        if self.dirty:
            self.run()
        return bool(self.reached[location_index])

    def reached_names(self) -> set[str]:
        if self.dirty:
            self.run()
        return {self.w.locations[i].name for i, r in enumerate(self.reached) if r}


def compute_exit_cache(world: LogicWorld, counts: list[int], internal: dict[int, int]) -> None:
    """Port of fill::CacheExitTimeForms: which form/times could ever pass each exit."""
    for ex in world.exits:
        ex.potential = ALL
    search = SearchState(world, internal, counts)
    search.run()
    for ex in world.exits:
        area_ft = search.ft[ex.parent.index]
        mask = 0
        for form_time in FT_NORMAL:
            if form_time & area_ft and ex.fn(search.counts, search.events, form_time):
                mask |= form_time
        ex.potential = mask
