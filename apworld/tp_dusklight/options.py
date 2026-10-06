"""Player options. Every option maps onto one Dusklight randomizer setting (see SETTING_MAP),
so the in-game generator runs with exactly the settings Archipelago's logic used."""

from __future__ import annotations

from dataclasses import dataclass

from Options import (
    Choice,
    DeathLink,
    DefaultOnToggle,
    OptionGroup,
    PerGameCommonOptions,
    Range,
    StartInventoryPool,
    Toggle,
)

# --------------------------------------------------------------------------------------------
# Access requirements


class HyruleBarrierRequirements(Choice):
    """What dispels the barrier around Hyrule Castle.

    - **Open:** dispelled from the start.
    - **Vanilla:** dispelled once Palace of Twilight is cleared.
    - **Fused Shadows / Mirror Shards / Dungeons / Poe Souls / Hearts:** dispelled once you have
      the number set by the matching "Hyrule Barrier ..." option."""
    display_name = "Hyrule Barrier Requirements"
    option_open = 0
    option_vanilla = 1
    option_fused_shadows = 2
    option_mirror_shards = 3
    option_dungeons = 4
    option_poe_souls = 5
    option_hearts = 6
    default = 1


class HyruleBarrierFusedShadows(Range):
    """Fused Shadows needed when the barrier requires Fused Shadows."""
    display_name = "Hyrule Barrier Fused Shadows"
    range_start = 1
    range_end = 3
    default = 3


class HyruleBarrierMirrorShards(Range):
    """Mirror Shards needed when the barrier requires Mirror Shards."""
    display_name = "Hyrule Barrier Mirror Shards"
    range_start = 1
    range_end = 4
    default = 4


class HyruleBarrierDungeons(Range):
    """Dungeons to clear when the barrier requires Dungeons."""
    display_name = "Hyrule Barrier Dungeons"
    range_start = 1
    range_end = 8
    default = 5


class HyruleBarrierPoeSouls(Range):
    """Poe Souls needed when the barrier requires Poe Souls."""
    display_name = "Hyrule Barrier Poe Souls"
    range_start = 1
    range_end = 60
    default = 20


class HyruleBarrierHearts(Range):
    """Hearts needed when the barrier requires Hearts."""
    display_name = "Hyrule Barrier Hearts"
    range_start = 4
    range_end = 20
    default = 10


class HyruleCastleBigKeyRequirements(Choice):
    """What opens the Hyrule Castle big key gate.

    - **None:** the gate is open and the Hyrule Castle Big Key is shuffled like other big keys.
    - **Fused Shadows / Mirror Shards / Dungeons / Poe Souls / Hearts:** the gate opens once you
      have the number set by the matching "Hyrule Castle Big Key ..." option."""
    display_name = "Hyrule Castle Big Key Requirements"
    option_none = 0
    option_fused_shadows = 1
    option_mirror_shards = 2
    option_dungeons = 3
    option_poe_souls = 4
    option_hearts = 5
    default = 0


class HyruleCastleBigKeyFusedShadows(Range):
    """Fused Shadows needed for the Hyrule Castle big key gate."""
    display_name = "Hyrule Castle Big Key Fused Shadows"
    range_start = 1
    range_end = 3
    default = 3


class HyruleCastleBigKeyMirrorShards(Range):
    """Mirror Shards needed for the Hyrule Castle big key gate."""
    display_name = "Hyrule Castle Big Key Mirror Shards"
    range_start = 1
    range_end = 4
    default = 4


class HyruleCastleBigKeyDungeons(Range):
    """Dungeons to clear for the Hyrule Castle big key gate."""
    display_name = "Hyrule Castle Big Key Dungeons"
    range_start = 1
    range_end = 8
    default = 5


class HyruleCastleBigKeyPoeSouls(Range):
    """Poe Souls needed for the Hyrule Castle big key gate."""
    display_name = "Hyrule Castle Big Key Poe Souls"
    range_start = 1
    range_end = 60
    default = 20


class HyruleCastleBigKeyHearts(Range):
    """Hearts needed for the Hyrule Castle big key gate."""
    display_name = "Hyrule Castle Big Key Hearts"
    range_start = 4
    range_end = 20
    default = 10


class PalaceOfTwilightRequirements(Choice):
    """What opens the Mirror of Twilight.

    - **Open:** open from the start.
    - **Fused Shadows:** collect all 3 Fused Shadows.
    - **Mirror Shards:** collect all 4 Mirror Shards.
    - **Vanilla:** complete City in the Sky."""
    display_name = "Palace of Twilight Requirements"
    option_open = 0
    option_fused_shadows = 1
    option_mirror_shards = 2
    option_vanilla = 3
    default = 3


class FaronWoodsLogic(Choice):
    """**Closed:** Midna blocks you from leaving Faron Woods until Forest Temple is done.
    **Open:** Midna lets you leave Faron Woods."""
    display_name = "Faron Woods Logic"
    option_closed = 0
    option_open = 1
    default = 0


class MirrorChamberAccess(Choice):
    """- **Open:** the Mirror Chamber entrance works normally.
    - **Barrier:** a barrier blocks it until Stallord is defeated.
    - **Closed:** the chamber can only be reached with its portal or from Palace of Twilight."""
    display_name = "Mirror Chamber Access"
    option_open = 0
    option_barrier = 1
    option_closed = 2
    default = 0


class GoronMinesEntrance(Choice):
    """- **Closed:** climb to the Sumo Hall and wrestle the Goron Elder.
    - **No Wrestling:** climb to the Sumo Hall, no wrestling needed.
    - **Open:** the elevator shortcut to the Sumo Hall is open."""
    display_name = "Goron Mines Entrance"
    option_closed = 0
    option_no_wrestling = 1
    option_open = 2
    default = 0


class TempleOfTimeSwordRequirement(Choice):
    """Sword you must strike into the Master Sword pedestal to open the door to the past."""
    display_name = "Temple of Time Sword Requirement"
    option_none = 0
    option_wooden_sword = 1
    option_ordon_sword = 2
    option_master_sword = 3
    option_light_sword = 4
    default = 0


class LakebedDoesNotRequireWaterBombs(Toggle):
    """The rock blocking Lakebed Temple's entrance is gone, so Water Bombs are not needed."""
    display_name = "Lakebed Does Not Require Water Bombs"


class ArbitersDoesNotRequireBulblinCamp(Toggle):
    """Bulblin Camp starts cleared, so Arbiter's Grounds does not need the camp key."""
    display_name = "Arbiters Does Not Require Bulblin Camp"


class SnowpeakDoesNotRequireReekfishScent(Toggle):
    """You start with the Reekfish scent, so the Snowpeak blizzard does not need the Coral Earring."""
    display_name = "Snowpeak Does Not Require Reekfish Scent"


class SacredGroveDoesNotRequireSkullKid(Toggle):
    """The Lost Woods Skull Kid chase starts completed."""
    display_name = "Sacred Grove Does Not Require Skull Kid"


class CityDoesNotRequireFilledSkybook(Toggle):
    """The cannon to City in the Sky is at Lake Hylia from the start."""
    display_name = "City Does Not Require Filled Skybook"


# --------------------------------------------------------------------------------------------
# What is shuffled


class GoldenBugs(Toggle):
    """Shuffle the 24 Golden Bug locations."""
    display_name = "Golden Bugs"


class SkyCharacters(Toggle):
    """Shuffle the Sky Character locations."""
    display_name = "Sky Characters"


class GiftsFromNPCs(Toggle):
    """Shuffle items NPCs give you."""
    display_name = "Gifts From NPCs"


class ShopItems(Toggle):
    """Shuffle shop items."""
    display_name = "Shop Items"


class HiddenSkills(Toggle):
    """Shuffle the Golden Wolf (Hidden Skill) locations."""
    display_name = "Hidden Skills"


class HiddenRupees(Toggle):
    """Shuffle rupees hidden in tricky spots."""
    display_name = "Hidden Rupees"


class FreestandingRupees(Toggle):
    """Shuffle rupees lying out in the open."""
    display_name = "Freestanding Rupees"


class PoeSouls(Choice):
    """Which Poes give shuffled items instead of Poe Souls."""
    display_name = "Poe Souls"
    option_vanilla = 0
    option_overworld = 1
    option_dungeon = 2
    option_all = 3
    default = 0


class IliaMemoryQuest(Choice):
    """How far into Ilia's memory quest you start. The chosen quest item is shuffled; the earlier
    steps of the quest are skipped."""
    display_name = "Ilia Memory Quest"
    option_vanilla = 0
    option_letter = 1
    option_invoice = 2
    option_statue = 3
    option_charm = 4
    default = 0


class ItemScarcity(Choice):
    """- **Vanilla:** no changes to the item pool.
    - **Minimal:** removes unneeded items (heart containers and pieces, Hawkeye...).
    - **Plentiful:** an extra copy of major items and keys."""
    display_name = "Item Scarcity"
    option_vanilla = 0
    option_minimal = 1
    option_plentiful = 2
    default = 0


class TrapItemFrequency(Choice):
    """How many of this world's filler items are replaced with Foolish Items (traps that look
    like other items)."""
    display_name = "Trap Item Frequency"
    option_none = 0
    option_few = 1
    option_many = 2
    option_mayhem = 3
    option_nightmare = 4
    default = 0


class DungeonItemOption(Choice):
    option_vanilla = 0
    option_own_dungeon = 1
    option_any_dungeon = 2
    option_overworld = 3
    option_own_world = 4
    option_anywhere = 5
    default = 0


class SmallKeys(DungeonItemOption):
    """Where small keys (and the Ordon Pumpkin/Cheese for Snowpeak Ruins) can be.

    - **Vanilla / Own Dungeon / Any Dungeon / Overworld:** as in the randomizer, in your world.
    - **Own World:** anywhere in your own world.
    - **Anywhere:** anywhere in the multiworld.
    - **Keysy:** no small keys; locked doors start open."""
    display_name = "Small Keys"
    option_keysy = 6


class BigKeys(DungeonItemOption):
    """Where big keys (including Goron Mines key shards and the Snowpeak Bedroom Key) can be.
    Same choices as Small Keys; **Keysy** opens boss doors."""
    display_name = "Big Keys"
    option_keysy = 6


class MapsAndCompasses(DungeonItemOption):
    """Where dungeon maps and compasses can be. Same choices as Small Keys;
    **Start With** gives them all at the start."""
    display_name = "Maps and Compasses"
    option_start_with = 6


class DungeonRewardsCanBeAnywhere(Toggle):
    """Off: Fused Shadows and Mirror Shards are at the end of dungeons.
    On: they can be anywhere in the multiworld."""
    display_name = "Dungeon Rewards Can Be Anywhere"


class SmallKeysOnBosses(Toggle):
    """Allow this world's small keys on boss heart containers and dungeon rewards."""
    display_name = "Small Keys on Bosses"


class UnrequiredDungeonsAreBarren(DefaultOnToggle):
    """Dungeons that are not needed to beat the game hold nothing that anyone needs (no
    progression items from any world)."""
    display_name = "Unrequired Dungeons Are Barren"


# --------------------------------------------------------------------------------------------
# Story and quality of life


class SkipPrologue(DefaultOnToggle):
    """Start with the prologue (everything up to the second goat herding) completed.
    Recommended in a multiworld, where your first swords may be in someone else's game."""
    display_name = "Skip Prologue"


class FaronTwilightCleared(Toggle):
    """Start with the Castle Sewers and Faron Twilight completed."""
    display_name = "Faron Twilight Cleared"


class EldinTwilightCleared(Toggle):
    """Start with Eldin Twilight completed."""
    display_name = "Eldin Twilight Cleared"


class LanayruTwilightCleared(Toggle):
    """Start with Lanayru Twilight completed."""
    display_name = "Lanayru Twilight Cleared"


class SkipMidnasDesperateHour(Toggle):
    """Start with Midna's Desperate Hour completed."""
    display_name = "Skip Midna's Desperate Hour"


class SkipMinorCutscenes(Toggle):
    """Skip area introductions and Midna explanations."""
    display_name = "Skip Minor Cutscenes"


class SkipMajorCutscenes(Toggle):
    """Automatically skip every skippable cutscene."""
    display_name = "Skip Major Cutscenes"


class UnlockMapRegions(Toggle):
    """Start with the map filled in as far as possible."""
    display_name = "Unlock Map Regions"


class OpenDoorOfTime(Toggle):
    """The Temple of Time statue starts in place and the big door is open."""
    display_name = "Open Door of Time"


class ActiveGoronMinesMagnets(Toggle):
    """Goron Mines magnet switches start on, except the highest one in the main room."""
    display_name = "Active Goron Mines Magnets"


class LowerHyruleCastleChandelier(Toggle):
    """One Hyrule Castle main hall chandelier starts lowered."""
    display_name = "Lower Hyrule Castle Chandelier"


class SkipBridgeDonation(Toggle):
    """The Eldin-Castle Town bridge builds itself once Eldin and Lanayru Twilight are done."""
    display_name = "Skip Bridge Donation"


class StartingForm(Choice):
    """Start as Human Link or Wolf Link. Wolf forces Skip Prologue on."""
    display_name = "Starting Form"
    option_human = 0
    option_wolf = 1
    default = 0


class StartingTimeOfDay(Choice):
    """Time of day when you start."""
    display_name = "Starting Time of Day"
    option_morning = 0
    option_noon = 1
    option_evening = 2
    option_night = 3
    default = 1


class BonksDoDamage(Toggle):
    """Bonking into walls hurts."""
    display_name = "Bonks Do Damage"


class LogicTransformAnywhere(DefaultOnToggle):
    """Logic may expect you to transform where the game normally won't let you. Match this to
    Dusklight's Transform Anywhere setting."""
    display_name = "Logic Transform Anywhere"


class LogicIncreaseWalletCapacity(Toggle):
    """Logic assumes Dusklight's Bigger Wallets setting is on."""
    display_name = "Logic Increase Wallet Capacity"


class LogicDamageMultiplier(Choice):
    """The damage multiplier logic assumes; match it to Dusklight's Damage Multiplier setting."""
    display_name = "Logic Damage Multiplier"
    option_vanilla = 0
    option_double = 1
    option_triple = 2
    option_quadruple = 3
    option_ohko = 4
    default = 0


class BackSliceAsSword(Toggle):
    """Logic may expect back slicing without a sword to deal damage."""
    display_name = "Back Slice as Sword"


class BallAndChainWebs(Toggle):
    """Logic may expect the Ball and Chain to break webs."""
    display_name = "Ball and Chain Webs"


# --------------------------------------------------------------------------------------------


@dataclass
class TPDusklightOptions(PerGameCommonOptions):
    start_inventory_from_pool: StartInventoryPool
    death_link: DeathLink

    hyrule_barrier_requirements: HyruleBarrierRequirements
    hyrule_barrier_fused_shadows: HyruleBarrierFusedShadows
    hyrule_barrier_mirror_shards: HyruleBarrierMirrorShards
    hyrule_barrier_dungeons: HyruleBarrierDungeons
    hyrule_barrier_poe_souls: HyruleBarrierPoeSouls
    hyrule_barrier_hearts: HyruleBarrierHearts
    hyrule_castle_big_key_requirements: HyruleCastleBigKeyRequirements
    hyrule_castle_big_key_fused_shadows: HyruleCastleBigKeyFusedShadows
    hyrule_castle_big_key_mirror_shards: HyruleCastleBigKeyMirrorShards
    hyrule_castle_big_key_dungeons: HyruleCastleBigKeyDungeons
    hyrule_castle_big_key_poe_souls: HyruleCastleBigKeyPoeSouls
    hyrule_castle_big_key_hearts: HyruleCastleBigKeyHearts
    palace_of_twilight_requirements: PalaceOfTwilightRequirements
    faron_woods_logic: FaronWoodsLogic
    mirror_chamber_access: MirrorChamberAccess
    goron_mines_entrance: GoronMinesEntrance
    temple_of_time_sword_requirement: TempleOfTimeSwordRequirement
    lakebed_does_not_require_water_bombs: LakebedDoesNotRequireWaterBombs
    arbiters_does_not_require_bulblin_camp: ArbitersDoesNotRequireBulblinCamp
    snowpeak_does_not_require_reekfish_scent: SnowpeakDoesNotRequireReekfishScent
    sacred_grove_does_not_require_skull_kid: SacredGroveDoesNotRequireSkullKid
    city_does_not_require_filled_skybook: CityDoesNotRequireFilledSkybook

    golden_bugs: GoldenBugs
    sky_characters: SkyCharacters
    gifts_from_npcs: GiftsFromNPCs
    shop_items: ShopItems
    hidden_skills: HiddenSkills
    hidden_rupees: HiddenRupees
    freestanding_rupees: FreestandingRupees
    poe_souls: PoeSouls
    ilia_memory_quest: IliaMemoryQuest
    item_scarcity: ItemScarcity
    trap_item_frequency: TrapItemFrequency
    small_keys: SmallKeys
    big_keys: BigKeys
    maps_and_compasses: MapsAndCompasses
    dungeon_rewards_can_be_anywhere: DungeonRewardsCanBeAnywhere
    small_keys_on_bosses: SmallKeysOnBosses
    unrequired_dungeons_are_barren: UnrequiredDungeonsAreBarren

    skip_prologue: SkipPrologue
    faron_twilight_cleared: FaronTwilightCleared
    eldin_twilight_cleared: EldinTwilightCleared
    lanayru_twilight_cleared: LanayruTwilightCleared
    skip_midnas_desperate_hour: SkipMidnasDesperateHour
    skip_minor_cutscenes: SkipMinorCutscenes
    skip_major_cutscenes: SkipMajorCutscenes
    unlock_map_regions: UnlockMapRegions
    open_door_of_time: OpenDoorOfTime
    active_goron_mines_magnets: ActiveGoronMinesMagnets
    lower_hyrule_castle_chandelier: LowerHyruleCastleChandelier
    skip_bridge_donation: SkipBridgeDonation
    starting_form: StartingForm
    starting_time_of_day: StartingTimeOfDay
    bonks_do_damage: BonksDoDamage
    logic_transform_anywhere: LogicTransformAnywhere
    logic_increase_wallet_capacity: LogicIncreaseWalletCapacity
    logic_damage_multiplier: LogicDamageMultiplier
    back_slice_as_sword: BackSliceAsSword
    ball_and_chain_webs: BallAndChainWebs


# attribute -> randomizer setting name. Choice values map by their option name (snake_case ->
# the randomizer's spelling via CHOICE_SPELLING), toggles map to On/Off, ranges to numbers.
SETTING_MAP: dict[str, str] = {
    "hyrule_barrier_requirements": "Hyrule Barrier Requirements",
    "hyrule_barrier_fused_shadows": "Hyrule Barrier Fused Shadows",
    "hyrule_barrier_mirror_shards": "Hyrule Barrier Mirror Shards",
    "hyrule_barrier_dungeons": "Hyrule Barrier Dungeons",
    "hyrule_barrier_poe_souls": "Hyrule Barrier Poe Souls",
    "hyrule_barrier_hearts": "Hyrule Barrier Hearts",
    "hyrule_castle_big_key_requirements": "Hyrule Castle Big Key Requirements",
    "hyrule_castle_big_key_fused_shadows": "Hyrule Castle Big Key Fused Shadows",
    "hyrule_castle_big_key_mirror_shards": "Hyrule Castle Big Key Mirror Shards",
    "hyrule_castle_big_key_dungeons": "Hyrule Castle Big Key Dungeons",
    "hyrule_castle_big_key_poe_souls": "Hyrule Castle Big Key Poe Souls",
    "hyrule_castle_big_key_hearts": "Hyrule Castle Big Key Hearts",
    "palace_of_twilight_requirements": "Palace of Twilight Requirements",
    "faron_woods_logic": "Faron Woods Logic",
    "mirror_chamber_access": "Mirror Chamber Access",
    "goron_mines_entrance": "Goron Mines Entrance",
    "temple_of_time_sword_requirement": "Temple of Time Sword Requirement",
    "lakebed_does_not_require_water_bombs": "Lakebed Does Not Require Water Bombs",
    "arbiters_does_not_require_bulblin_camp": "Arbiters Does Not Require Bulblin Camp",
    "snowpeak_does_not_require_reekfish_scent": "Snowpeak Does Not Require Reekfish Scent",
    "sacred_grove_does_not_require_skull_kid": "Sacred Grove Does Not Require Skull Kid",
    "city_does_not_require_filled_skybook": "City Does Not Require Filled Skybook",
    "golden_bugs": "Golden Bugs",
    "sky_characters": "Sky Characters",
    "gifts_from_npcs": "Gifts From NPCs",
    "shop_items": "Shop Items",
    "hidden_skills": "Hidden Skills",
    "hidden_rupees": "Hidden Rupees",
    "freestanding_rupees": "Freestanding Rupees",
    "poe_souls": "Poe Souls",
    "ilia_memory_quest": "Ilia Memory Quest",
    "item_scarcity": "Item Scarcity",
    "trap_item_frequency": "Trap Item Frequency",
    "small_keys": "Small Keys",
    "big_keys": "Big Keys",
    "maps_and_compasses": "Maps and Compasses",
    "dungeon_rewards_can_be_anywhere": "Dungeon Rewards Can Be Anywhere",
    "small_keys_on_bosses": "Small Keys on Bosses",
    "unrequired_dungeons_are_barren": "Unrequired Dungeons Are Barren",
    "skip_prologue": "Skip Prologue",
    "faron_twilight_cleared": "Faron Twilight Cleared",
    "eldin_twilight_cleared": "Eldin Twilight Cleared",
    "lanayru_twilight_cleared": "Lanayru Twilight Cleared",
    "skip_midnas_desperate_hour": "Skip Midna's Desperate Hour",
    "skip_minor_cutscenes": "Skip Minor Cutscenes",
    "skip_major_cutscenes": "Skip Major Cutscenes",
    "unlock_map_regions": "Unlock Map Regions",
    "open_door_of_time": "Open Door of Time",
    "active_goron_mines_magnets": "Active Goron Mines Magnets",
    "lower_hyrule_castle_chandelier": "Lower Hyrule Castle Chandelier",
    "skip_bridge_donation": "Skip Bridge Donation",
    "starting_form": "Starting Form",
    "starting_time_of_day": "Starting Time of Day",
    "bonks_do_damage": "Bonks Do Damage",
    "logic_transform_anywhere": "Logic Transform Anywhere",
    "logic_increase_wallet_capacity": "Logic Increase Wallet Capacity",
    "logic_damage_multiplier": "Logic Damage Multiplier",
    "back_slice_as_sword": "Back Slice as Sword",
    "ball_and_chain_webs": "Ball and Chain Webs",
}

# snake_case choice names whose randomizer spelling is not just Title Case
CHOICE_SPELLING: dict[str, str] = {
    "ohko": "OHKO",
    "own_world": "Anywhere",
    "anywhere": "Anywhere",
    "start_with": "Start With",
    "no_wrestling": "No Wrestling",
}


def option_to_setting(option) -> str:
    if isinstance(option, Toggle):
        return "On" if option.value else "Off"
    if isinstance(option, Range):
        return str(option.value)
    if isinstance(option, Choice):
        key = option.current_key
        if key in CHOICE_SPELLING:
            return CHOICE_SPELLING[key]
        return " ".join(word.capitalize() for word in key.split("_"))
    raise TypeError(f"cannot convert {option!r}")


option_groups = [
    OptionGroup("Access Requirements", [
        HyruleBarrierRequirements, HyruleBarrierFusedShadows, HyruleBarrierMirrorShards,
        HyruleBarrierDungeons, HyruleBarrierPoeSouls, HyruleBarrierHearts,
        HyruleCastleBigKeyRequirements, HyruleCastleBigKeyFusedShadows,
        HyruleCastleBigKeyMirrorShards, HyruleCastleBigKeyDungeons, HyruleCastleBigKeyPoeSouls,
        HyruleCastleBigKeyHearts, PalaceOfTwilightRequirements, FaronWoodsLogic,
        MirrorChamberAccess, GoronMinesEntrance, TempleOfTimeSwordRequirement,
        LakebedDoesNotRequireWaterBombs, ArbitersDoesNotRequireBulblinCamp,
        SnowpeakDoesNotRequireReekfishScent, SacredGroveDoesNotRequireSkullKid,
        CityDoesNotRequireFilledSkybook,
    ]),
    OptionGroup("Shuffled Locations and Items", [
        GoldenBugs, SkyCharacters, GiftsFromNPCs, ShopItems, HiddenSkills, HiddenRupees,
        FreestandingRupees, PoeSouls, IliaMemoryQuest, ItemScarcity, TrapItemFrequency,
    ]),
    OptionGroup("Dungeon Items", [
        SmallKeys, BigKeys, MapsAndCompasses, DungeonRewardsCanBeAnywhere, SmallKeysOnBosses,
        UnrequiredDungeonsAreBarren,
    ]),
    OptionGroup("Story and Quality of Life", [
        SkipPrologue, FaronTwilightCleared, EldinTwilightCleared, LanayruTwilightCleared,
        SkipMidnasDesperateHour, SkipMinorCutscenes, SkipMajorCutscenes, UnlockMapRegions,
        OpenDoorOfTime, ActiveGoronMinesMagnets, LowerHyruleCastleChandelier, SkipBridgeDonation,
        StartingForm, StartingTimeOfDay, BonksDoDamage,
    ]),
    OptionGroup("Logic Assumptions", [
        LogicTransformAnywhere, LogicIncreaseWalletCapacity, LogicDamageMultiplier,
        BackSliceAsSword, BallAndChainWebs,
    ]),
]
