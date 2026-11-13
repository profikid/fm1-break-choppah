#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Where a release's notes live: docs/hub/notes_vXYY.md for X.Y (0.96 -> notes_v096.md, 1.2 -> notes_v102.md),
# docs/hub/notes_vXYY_Z.md for a patch X.Y.Z (1.2.1 -> notes_v102_1.md). A suffix (1.3-rc1) is ignored.
#   scripts/notes_path.sh 1.2.1
set -e
V="${1#v}"
V="${V%%-*}"
dec() { v=$(printf '%s' "$1" | sed 's/^0*//'); if [ -n "$v" ]; then echo "$v"; else echo 0; fi; }
MAJOR="${V%%.*}"; REST="${V#*.}"; MINOR="${REST%%.*}"
P="docs/hub/notes_v$(printf '%03d' $(( $(dec "$MAJOR") * 100 + $(dec "$MINOR") )))"
case "$REST" in
    *.*) P="${P}_$(dec "${REST#*.}")" ;;
esac
echo "$P.md"
