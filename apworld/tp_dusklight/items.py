"""Item table, derived from the randomizer's items.yaml."""

from __future__ import annotations

from BaseClasses import Item, ItemClassification

from .logic.world_graph import load_data, load_ids

GAME = "Twilight Princess Dusklight"


class TPDusklightItem(Item):
    game = GAME


ITEM_ID_BASE: int = load_ids()["item_id_base"]

# Items that are only ever logic bookkeeping inside the randomizer (never shuffled)
NON_POOL_ITEMS = {"Game Beatable", "Hint"}

item_data: dict[str, dict] = {raw["name"]: raw for raw in load_data()["items"]}
item_name_to_id: dict[str, int] = {name: ITEM_ID_BASE + raw["id"] for name, raw in item_data.items()
                                   if name not in NON_POOL_ITEMS}

BULK_ITEMS = {"Poe Soul", "Piece of Heart", "Heart Container"}


def _group(predicate) -> set[str]:
    return {name for name, raw in item_data.items() if name in item_name_to_id and predicate(name, raw)}


item_name_groups: dict[str, set[str]] = {
    "Small Keys": _group(lambda n, r: bool(r["small_key_of"])) | {
        "Gate Keys", "Gerudo Desert Bulblin Camp Key", "North Faron Woods Gate Key",
        "Faron Woods Coro Key", "Ordon Pumpkin", "Ordon Cheese"},
    "Big Keys": _group(lambda n, r: bool(r["big_key_of"])),
    "Compasses": _group(lambda n, r: n.endswith("Compass")),
    "Dungeon Maps": _group(lambda n, r: n.endswith("Dungeon Map")),
    "Golden Bugs": _group(lambda n, r: n.startswith("Male ") or n.startswith("Female ")),
    "Warp Portals": _group(lambda n, r: n.endswith(" Portal")),
    "Rupees": _group(lambda n, r: "Rupee" in n),
    "Bottles": _group(lambda n, r: n.startswith("Bottle") or n == "Empty Bottle"),
    "Dungeon Rewards": {"Progressive Fused Shadow", "Progressive Mirror Shard"},
    "Hearts": {"Piece of Heart", "Heart Container"},
    "Swords": {"Progressive Sword"},
    "Ilia Quest Items": {"Renados Letter", "Invoice", "Wooden Statue", "Ilias Charm"},
    "Twilight Tears": _group(lambda n, r: n.endswith("Twilight Tear")),
}


def classification_for(name: str, logic_items: set[str], importance: str) -> ItemClassification:
    if name == "Foolish Item":
        return ItemClassification.trap
    if name in logic_items:
        if name in BULK_ITEMS:
            return ItemClassification.progression_skip_balancing
        return ItemClassification.progression
    if importance in ("Major", "Minor") or name in ("Heart Container", "Piece of Heart"):
        return ItemClassification.useful
    return ItemClassification.filler
