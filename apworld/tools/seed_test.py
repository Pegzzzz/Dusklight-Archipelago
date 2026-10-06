#!/usr/bin/env python3
"""End-to-end check of Archipelago seeds -> in-game randomizer seeds, without the game.

Generates multiworlds with Archipelago (random Twilight Princess Dusklight options plus other
games), takes each Dusklight slot's slot_data from the multidata, and runs the C++ ap_tool on it,
which drives the randomizer generator exactly as the mod does and checks every placement.

Usage (from an Archipelago checkout that has the world installed):
    python <repo>/apworld/tools/seed_test.py --ap-tool <build>/ap_tool [--count 10] [--others "Timespinner"]
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import logging
import os
import random
import subprocess
import sys
import tempfile
import zipfile
import zlib


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ap-tool", required=True)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--others", default="")
    parser.add_argument("--players", type=int, default=2)
    parser.add_argument("--keep", help="directory to keep slot data JSON files in")
    args = parser.parse_args()

    sys.path.insert(0, os.getcwd())
    import ModuleUpdate
    ModuleUpdate.update_ran = True
    logging.disable(logging.CRITICAL)
    import Generate
    import Utils
    from Main import main as generate_main
    from worlds.AutoWorld import AutoWorldRegister

    option_names = [name for name in AutoWorldRegister.world_types["Twilight Princess Dusklight"]
                    .options_dataclass.type_hints
                    if name not in {"start_inventory", "start_inventory_from_pool", "local_items", "non_local_items",
                                    "start_hints", "start_location_hints", "exclude_locations",
                                    "priority_locations", "item_links", "plando_items", "progression_balancing"}]
    rng = random.Random(args.seed)
    others = [g for g in args.others.split(",") if g]
    failures = checked = 0
    for n in range(args.count):
        with tempfile.TemporaryDirectory() as tmp:
            players_dir = os.path.join(tmp, "players")
            os.makedirs(players_dir)
            for p in range(args.players):
                options = "\n".join(f"  {name}: random" for name in option_names)
                with open(os.path.join(players_dir, f"tp{p}.yaml"), "w") as f:
                    f.write(f"name: TP{p}\ngame: Twilight Princess Dusklight\n"
                            f"Twilight Princess Dusklight:\n{options}\n")
            for i, game in enumerate(others):
                with open(os.path.join(players_dir, f"other{i}.yaml"), "w") as f:
                    f.write(f"name: Other{i}\ngame: {game}\n{game}: {{}}\n")
            out = os.path.join(tmp, "out")
            sys.argv = ["gen", "--player_files_path", players_dir, "--outputpath", out,
                        "--seed", str(args.seed * 1000 + n)]
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    erargs, seed = Generate.main()
                    generate_main(erargs, seed)
            except Exception as error:  # option combinations the randomizer refuses
                if "OptionError" in type(error).__name__ or "OptionError" in repr(error):
                    print(f"[{n}] options rejected, skipping")
                    continue
                raise
            archive = next(f for f in os.listdir(out) if f.endswith(".zip"))
            with zipfile.ZipFile(os.path.join(out, archive)) as z:
                name = next(f for f in z.namelist() if f.endswith(".archipelago"))
                raw = z.read(name)
            multidata = Utils.restricted_loads(zlib.decompress(raw[1:]))
            for slot, slot_info in multidata["slot_info"].items():
                if slot_info.game != "Twilight Princess Dusklight":
                    continue
                slot_data = multidata["slot_data"][slot]
                slot_file = os.path.join(args.keep or tmp, f"slot_{n}_{slot}.json")
                with open(slot_file, "w") as f:
                    json.dump(slot_data, f)
                work = os.path.join(tmp, f"work{slot}")
                proc = subprocess.run([args.ap_tool, "generate", slot_file, work], capture_output=True, text=True,
                                      timeout=600)
                result = [line for line in proc.stdout.splitlines()
                          if line.split("\t", 1)[0] in ("OK", "FAIL", "ERROR", "MISMATCH")]
                checked += 1
                if proc.returncode != 0:
                    failures += 1
                    print(f"[{n}] slot {slot} FAILED:")
                    for line in result[:15]:
                        print("   ", line)
                else:
                    print(f"[{n}] slot {slot}: {result[-1] if result else proc.stdout[-200:]}")
    print(f"checked {checked} slots, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
