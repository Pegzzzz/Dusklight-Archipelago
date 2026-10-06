"""The APWorld, its manifest and the mod must agree on their version: the mod refuses slot data
from another slot data version and reports the APWorld version when a seed fails to build."""

import json
import re
import unittest
from pathlib import Path

from .. import APWORLD_VERSION, SLOT_DATA_VERSION

WORLD_DIR = Path(__file__).resolve().parents[1]


class TestVersions(unittest.TestCase):
    def test_manifest(self) -> None:
        manifest = json.loads((WORLD_DIR / "archipelago.json").read_text())
        self.assertEqual(manifest["world_version"], APWORLD_VERSION)
        self.assertEqual(manifest["game"], "Twilight Princess Dusklight")

    def test_mod(self) -> None:
        # Only in the repository (a packaged .apworld has no mod sources next to it)
        repository = WORLD_DIR.parents[1]
        cmake = repository / "CMakeLists.txt"
        seed_header = repository / "src" / "archipelago" / "ap_seed.hpp"
        if not cmake.is_file() or not seed_header.is_file():
            self.skipTest("not in the mod's repository")
        mod_version = re.search(r'set\(ARCHIPELAGO_MOD_VERSION "([^"]+)"\)', cmake.read_text()).group(1)
        self.assertEqual(mod_version, APWORLD_VERSION)
        slot_version = re.search(r"kSlotDataVersion = (\d+);", seed_header.read_text()).group(1)
        self.assertEqual(int(slot_version), SLOT_DATA_VERSION)
