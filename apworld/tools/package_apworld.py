#!/usr/bin/env python3
"""Packages the APWorld: apworld/tp_dusklight -> tp_dusklight.apworld

An .apworld is a zip whose only top-level entry is the world's package folder (with its
archipelago.json manifest inside). Install it by opening it with the Archipelago Launcher or by
copying it to Archipelago's custom_worlds folder.

Usage: python apworld/tools/package_apworld.py [-o dist/tp_dusklight.apworld]
"""

from __future__ import annotations

import argparse
import json
import zipfile
from pathlib import Path

WORLD = Path(__file__).resolve().parents[1] / "tp_dusklight"
SKIP_DIRS = {"__pycache__"}
SKIP_SUFFIXES = {".pyc", ".pyo"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output", type=Path, default=Path("dist") / "tp_dusklight.apworld")
    args = parser.parse_args()

    manifest = json.loads((WORLD / "archipelago.json").read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in WORLD.rglob("*")
                   if p.is_file() and not SKIP_DIRS & set(p.relative_to(WORLD).parts)
                   and p.suffix not in SKIP_SUFFIXES)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            archive.write(path, Path("tp_dusklight") / path.relative_to(WORLD))
    print(f"{args.output}: {manifest['game']} {manifest['world_version']}, {len(files)} files, "
          f"{args.output.stat().st_size // 1024} KiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
