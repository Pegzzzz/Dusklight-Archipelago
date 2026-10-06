"""Spot checks that Archipelago's view of reachability matches the randomizer logic."""

from . import TPDusklightTestBase


class TestStartWithNothing(TPDusklightTestBase):
    options = {"skip_prologue": False}

    def test_ordon_only(self) -> None:
        state = self.multiworld.state
        world = self.multiworld.worlds[self.player]
        search = world._search(state)
        reach = lambda name: search.can_reach(world.lw.location_table[name].index)
        self.assertTrue(reach("Wooden Sword Chest"))
        self.assertTrue(reach("Sera Shop Slingshot"))
        self.assertFalse(reach("Forest Temple Central North Chest"))
        self.assertFalse(reach("Faron Woods Owl Statue Chest"))
        self.assertFalse(state.has("Victory", self.player))

    def test_everything_beats_the_game(self) -> None:
        self.collect_all_but([])
        self.assertBeatable(True)


class TestCollectAndRemove(TPDusklightTestBase):
    options = {}

    def test_remove_rebuilds_search(self) -> None:
        state = self.multiworld.state
        items = self.get_items_by_name("Progressive Clawshot")
        before = self.can_reach_location("Faron Woods Owl Statue Chest")
        for item in items:
            state.collect(item, True)
        state.collect(self.get_item_by_name("Shadow Crystal"), True)
        for item in items:
            state.remove(item)
        state.remove(self.get_item_by_name("Shadow Crystal"))
        self.assertEqual(before, self.can_reach_location("Faron Woods Owl Statue Chest"))
