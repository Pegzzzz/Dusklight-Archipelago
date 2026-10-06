"""Generation tests across the option space. Each class runs Archipelago's standard checks
(full-state reachability, an empty-state start, a complete fill) with its options."""

from . import TPDusklightTestBase


class TestDefault(TPDusklightTestBase):
    options = {}

    def test_slot_data(self) -> None:
        world = self.multiworld.worlds[self.player]
        data = world.fill_slot_data()
        names = {entry[0] for entry in data["locations"]}
        self.assertLessEqual(names, set(world.ap_location_names()))
        for name, address, owner, item, flags, own in data["locations"]:
            self.assertEqual(address, world.location_name_to_id[name])
            if own:
                self.assertIn(item, world.item_name_to_id)
        self.assertEqual(data["settings"]["Logic Rules"], "No Logic")
        self.assertEqual(data["settings"]["Skip Prologue"], "On")


class TestVanillaPrologue(TPDusklightTestBase):
    options = {"skip_prologue": False}


class TestEverythingShuffled(TPDusklightTestBase):
    options = {
        "golden_bugs": True, "sky_characters": True, "gifts_from_npcs": True, "shop_items": True,
        "hidden_skills": True, "hidden_rupees": True, "freestanding_rupees": True, "poe_souls": "all",
        "small_keys": "anywhere", "big_keys": "anywhere", "maps_and_compasses": "anywhere",
        "dungeon_rewards_can_be_anywhere": True, "ilia_memory_quest": "charm",
    }


class TestOwnDungeon(TPDusklightTestBase):
    options = {"small_keys": "own_dungeon", "big_keys": "own_dungeon", "maps_and_compasses": "own_dungeon",
               "gifts_from_npcs": True}

    def test_keys_in_own_dungeon(self) -> None:
        world = self.multiworld.worlds[self.player]
        dungeon_of = {loc.name: name for name, locs in world.dungeon_locations.items() for loc in locs}
        for location in self.multiworld.get_locations(self.player):
            item = location.item
            if item is None or item.player != self.player:
                continue
            for dungeon, names in world.dungeon_small.items():
                if item.name in names:
                    self.assertEqual(dungeon_of.get(location.name), dungeon, f"{item.name} at {location.name}")
            for dungeon, names in world.dungeon_big.items():
                if item.name in names:
                    self.assertEqual(dungeon_of.get(location.name), dungeon, f"{item.name} at {location.name}")


class TestAnyDungeonAndOverworld(TPDusklightTestBase):
    options = {"small_keys": "any_dungeon", "big_keys": "overworld", "maps_and_compasses": "any_dungeon"}


class TestKeysy(TPDusklightTestBase):
    options = {"small_keys": "keysy", "big_keys": "keysy", "maps_and_compasses": "start_with",
               "hyrule_castle_big_key_requirements": "mirror_shards"}


class TestOwnWorldKeys(TPDusklightTestBase):
    options = {"small_keys": "own_world", "big_keys": "own_world"}


class TestWolfStartNight(TPDusklightTestBase):
    options = {"starting_form": "wolf", "skip_prologue": False, "starting_time_of_day": "night"}


class TestOpenWorld(TPDusklightTestBase):
    options = {"faron_twilight_cleared": True, "eldin_twilight_cleared": True, "lanayru_twilight_cleared": True,
               "skip_midnas_desperate_hour": True, "unlock_map_regions": True,
               "hyrule_barrier_requirements": "open", "faron_woods_logic": "open",
               "goron_mines_entrance": "open", "lakebed_does_not_require_water_bombs": True,
               "arbiters_does_not_require_bulblin_camp": True, "snowpeak_does_not_require_reekfish_scent": True,
               "sacred_grove_does_not_require_skull_kid": True, "city_does_not_require_filled_skybook": True,
               "open_door_of_time": True}


class TestHeartsAndPoes(TPDusklightTestBase):
    options = {"hyrule_barrier_requirements": "hearts", "hyrule_barrier_hearts": 16,
               "hyrule_castle_big_key_requirements": "poe_souls", "hyrule_castle_big_key_poe_souls": 40,
               "poe_souls": "overworld"}


class TestAllDungeons(TPDusklightTestBase):
    options = {"hyrule_barrier_requirements": "dungeons", "hyrule_barrier_dungeons": 8,
               "palace_of_twilight_requirements": "fused_shadows", "mirror_chamber_access": "closed",
               "unrequired_dungeons_are_barren": True}

    def test_all_required(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(world.barren_dungeons, set())


class TestBarrenDungeons(TPDusklightTestBase):
    options = {"hyrule_barrier_requirements": "open", "palace_of_twilight_requirements": "open",
               "unrequired_dungeons_are_barren": True, "small_keys": "own_dungeon"}

    def test_barren_dungeons_have_no_progression(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertTrue(world.barren_dungeons)
        dungeon_keys = set()
        for dungeon in world.barren_dungeons:
            dungeon_keys |= world.dungeon_small[dungeon] | world.dungeon_big[dungeon]
        for dungeon in world.barren_dungeons:
            allowed = dungeon_keys
            for name in world.barren_locations[dungeon]:
                location = world._ap_location(name)
                if location is None or location.item is None:
                    continue
                if location.item.advancement:
                    self.assertTrue(location.item.player == self.player and location.item.name in allowed,
                                    f"{location.item.name} in barren {dungeon} at {name}")


class TestPlentifulTraps(TPDusklightTestBase):
    options = {"item_scarcity": "plentiful", "trap_item_frequency": "mayhem", "small_keys": "own_dungeon"}


class TestMinimal(TPDusklightTestBase):
    options = {"item_scarcity": "minimal", "trap_item_frequency": "nightmare", "hidden_skills": True,
               "temple_of_time_sword_requirement": "light_sword", "logic_damage_multiplier": "ohko",
               "bonks_do_damage": True}


class TestLogicAssumptions(TPDusklightTestBase):
    options = {"logic_transform_anywhere": False, "logic_increase_wallet_capacity": True,
               "back_slice_as_sword": True, "ball_and_chain_webs": True}


class TestDeathLink(TPDusklightTestBase):
    options = {"death_link": True}

    def test_slot_data_death_link(self) -> None:
        self.assertTrue(self.multiworld.worlds[self.player].fill_slot_data()["death_link"])
