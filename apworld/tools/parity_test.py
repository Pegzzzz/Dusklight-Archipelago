#!/usr/bin/env python3
"""Compare the APWorld's Python logic port against the C++ randomizer generator.

For each settings preset, the C++ harness (target ap_parity_harness) builds the world, and both
sides must agree on the item pool, the vanilla placements, and on which locations are
reachable for many random sets of owned items.

Usage: python apworld/tools/parity_test.py --harness build/ap_parity_harness [--samples 200]
"""

from __future__ import annotations

import argparse
import random
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tp_dusklight.logic import rando_rules  # noqa: E402
from tp_dusklight.logic.search import SearchState, compute_exit_cache  # noqa: E402
from tp_dusklight.logic.world_graph import LogicWorld  # noqa: E402

PRESETS: dict[str, dict[str, str]] = {
    "default": {},
    "skip prologue": {"Skip Prologue": "On"},
    "open": {"Skip Prologue": "On", "Faron Twilight Cleared": "On", "Eldin Twilight Cleared": "On",
             "Lanayru Twilight Cleared": "On", "Skip Midna's Desperate Hour": "On",
             "Unlock Map Regions": "On", "Hyrule Barrier Requirements": "Open"},
    "everything shuffled": {"Golden Bugs": "On", "Sky Characters": "On", "Gifts From NPCs": "On",
                            "Shop Items": "On", "Hidden Skills": "On", "Hidden Rupees": "On",
                            "Freestanding Rupees": "On", "Poe Souls": "All",
                            "Small Keys": "Anywhere", "Big Keys": "Anywhere",
                            "Maps and Compasses": "Anywhere"},
    "keysy": {"Small Keys": "Keysy", "Big Keys": "Keysy", "Maps and Compasses": "Start With",
              "Gifts From NPCs": "Off"},
    "wolf start": {"Starting Form": "Wolf", "Skip Prologue": "On", "Starting Time of Day": "Night"},
    "no transform anywhere": {"Logic Transform Anywhere": "Off", "Bonks Do Damage": "On",
                              "Logic Damage Multiplier": "OHKO"},
    "hearts barrier": {"Hyrule Barrier Requirements": "Hearts", "Hyrule Barrier Hearts": "15",
                       "Hyrule Castle Big Key Requirements": "Poe Souls",
                       "Hyrule Castle Big Key Poe Souls": "30"},
    "dungeons barrier": {"Hyrule Barrier Requirements": "Dungeons", "Hyrule Barrier Dungeons": "8",
                         "Palace of Twilight Requirements": "Fused Shadows",
                         "Mirror Chamber Access": "Closed"},
    "plentiful + scarce entrances": {"Item Scarcity": "Plentiful", "Goron Mines Entrance": "Open",
                                     "Lakebed Does Not Require Water Bombs": "On",
                                     "Arbiters Does Not Require Bulblin Camp": "On",
                                     "Snowpeak Does Not Require Reekfish Scent": "On",
                                     "Sacred Grove Does Not Require Skull Kid": "On",
                                     "City Does Not Require Filled Skybook": "On",
                                     "Open Door of Time": "On", "Faron Woods Logic": "Open"},
    "minimal ilia": {"Item Scarcity": "Minimal", "Ilia Memory Quest": "Charm",
                     "Temple of Time Sword Requirement": "Master Sword",
                     "Skip Bridge Donation": "On"},
}


def parse_counts(text: str) -> Counter:
    out = Counter()
    for entry in filter(None, text.split("|")):
        name, _, count = entry.rpartition("*")
        out[name] += int(count)
    return out


def run_preset(harness: Path, name: str, settings: dict[str, str], samples: int, rng: random.Random) -> bool:
    lw = LogicWorld(settings)
    pool = rando_rules.item_pool(lw)
    start = rando_rules.starting_items(lw, pool, {})
    vanilla = rando_rules.vanilla_placements(lw, pool)
    rando_rules.nonprogress_conflicts(lw, vanilla, start)
    internal = {lw.location_table[loc].index: lw.get_item(item).index
                for loc, item in vanilla.items() if item != "Nothing"}

    with tempfile.TemporaryDirectory() as tmp:
        settings_text = "Seed: PARITY\n" + "".join(f"{k}: \"{v}\"\n" for k, v in settings.items())
        (Path(tmp) / "settings.yaml").write_text(settings_text)
        queries = []
        relevant = [i for i in lw.logic_items()]
        full_counts = Counter({k: v for k, v in pool.items()})
        for _ in range(samples):
            fraction = rng.random()
            chosen = Counter()
            for item_name, count in full_counts.items():
                if lw.get_item(item_name) not in relevant:
                    continue
                for _ in range(count):
                    if rng.random() < fraction:
                        chosen[item_name] += 1
            queries.append(chosen)
        queries.append(Counter(full_counts))  # everything
        queries.append(Counter())             # nothing
        stdin = "\n".join("|".join(f"{k}*{v}" for k, v in q.items()) or "Green Rupee*0" for q in queries) + "\n"
        proc = subprocess.run([str(harness), tmp], input=stdin, capture_output=True, text=True, timeout=1200)
    lines = [l for l in proc.stdout.splitlines()
             if l.split("\t", 1)[0] in ("POOL", "START", "VANILLA", "REACH", "ERROR")]
    if lines and lines[0].startswith("ERROR") and "Not all locations reachable" in proc.stdout:
        lw2 = LogicWorld(settings)
        missing = rando_rules.unreachable_with_everything(lw2, pool, start, internal)
        print(f"[{name}] {'OK' if missing else 'FAIL'}: c++ rejects these settings; python "
              f"{'also rejects them' if missing else 'accepts them'} (missing e.g. {missing[:3]})")
        return bool(missing)
    if not lines or lines[0].startswith("ERROR"):
        errors = [l for l in proc.stdout.splitlines() if l.startswith("ERROR") or "xception" in l]
        print(f"[{name}] harness failed: {errors[-3:]} {proc.stderr[-300:]}\n  settings: {settings}")
        return False

    ok = True
    cpp_pool = parse_counts(lines[0].split("\t", 1)[1])
    cpp_start = parse_counts(lines[1].split("\t", 1)[1])
    cpp_vanilla = dict(e.split("=", 1) for e in filter(None, lines[2].split("\t", 1)[1].split("|")))

    junk_names = set(rando_rules.junk_pool(lw)) | set(rando_rules.INITIAL_JUNK_POOL)
    for item in set(cpp_pool) | set(pool):
        if item in junk_names:
            continue
        if cpp_pool[item] != pool[item]:
            print(f"[{name}] pool mismatch {item}: c++ {cpp_pool[item]} py {pool[item]}")
            ok = False
    if cpp_start != start:
        print(f"[{name}] starting items differ: c++ {dict(cpp_start)} py {dict(start)}")
        ok = False
    py_vanilla = {k: v for k, v in vanilla.items() if v != "Nothing"}
    if cpp_vanilla != py_vanilla:
        for k in sorted(set(cpp_vanilla) | set(py_vanilla)):
            if cpp_vanilla.get(k) != py_vanilla.get(k):
                print(f"[{name}] vanilla mismatch {k}: c++ {cpp_vanilla.get(k)} py {py_vanilla.get(k)}")
        ok = False

    base = [0] * len(lw.items)
    for item, count in (pool + start).items():
        base[lw.get_item(item).index] += count
    compute_exit_cache(lw, base, internal)

    mismatches = 0
    for query, line in zip(queries, lines[3:]):
        cpp_reach = set(filter(None, line.split("\t", 1)[1].split("|")))
        counts = [0] * len(lw.items)
        for item, count in (query + start).items():
            counts[lw.get_item(item).index] += count
        search = SearchState(lw, internal, counts)
        py_reach = search.reached_names()
        if cpp_reach != py_reach:
            mismatches += 1
            if mismatches <= 3:
                print(f"[{name}] reach mismatch: only c++ {sorted(cpp_reach - py_reach)[:8]} "
                      f"only py {sorted(py_reach - cpp_reach)[:8]}")
    if mismatches:
        print(f"[{name}] {mismatches}/{len(queries)} queries differ")
        ok = False
    print(f"[{name}] {'OK' if ok else 'FAIL'} ({len(queries)} queries)")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--harness", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=100)
    parser.add_argument("--preset", action="append")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--random-presets", type=int, default=0,
                        help="also test this many fully random settings combinations")
    args = parser.parse_args()
    rng = random.Random(args.seed)
    results = []
    for name, settings in PRESETS.items():
        if args.preset and name not in args.preset:
            continue
        results.append(run_preset(args.harness, name, settings, args.samples, rng))
    infos = LogicWorld({}).setting_infos
    skip = {"Logic Rules"} | {n for n in infos if n.startswith("Randomize ") or n.startswith("Decouple")}
    for i in range(args.random_presets):
        settings = {n: rng.choice(info["options"]) for n, info in infos.items() if n not in skip}
        results.append(run_preset(args.harness, f"random {i}", settings, args.samples, rng))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
