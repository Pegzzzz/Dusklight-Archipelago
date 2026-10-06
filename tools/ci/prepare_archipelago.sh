#!/usr/bin/env bash
# Prepares an Archipelago checkout for testing the Twilight Princess Dusklight APWorld:
# drops the other games' worlds (keeping the ones Archipelago's general tests need) and
# links this repository's world in.
#
#   tools/ci/prepare_archipelago.sh <archipelago checkout> <repository root>
set -euo pipefail

archipelago=$(cd "$1" && pwd)
repository=$(cd "$2" && pwd)

for world in "$archipelago"/worlds/*/; do
    name=$(basename "$world")
    case "$name" in
        generic | apquest | _* | __pycache__) ;;
        *) rm -rf "$world" ;;
    esac
done
ln -sfn "$repository/apworld/tp_dusklight" "$archipelago/worlds/tp_dusklight"
ls "$archipelago/worlds"
