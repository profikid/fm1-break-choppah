#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The version the next release gets, by kind of release, from the last release tag (vX.Y or vX.Y.Z; prereleases
# such as v1.3-rc1 are skipped), never below ./VERSION:
#   patch  1.2 -> 1.2.1, 1.2.1 -> 1.2.2     (a fix, nothing new to learn)
#   minor  1.2 -> 1.3,   1.2.1 -> 1.3       (new features; 0.99 -> 1.0)
#   major  1.2 -> 2.0,   1.2.1 -> 2.0
# Each part has at most two digits (the package identity is the id prefix + XXYYZZ, base/tools/build.py).
#   scripts/bump_version.sh patch              the tags from origin (CI)
#   scripts/bump_version.sh minor v0.1 v0.2    these tags instead (tests)
set -e
cd "$(dirname "$0")/.."
KIND="${1:-}"
case "$KIND" in
    patch|minor|major) shift ;;
    *) echo "usage: bump_version.sh patch|minor|major [tags...]" >&2; exit 1 ;;
esac
FLOOR="$(tr -d '[:space:]' < VERSION)"
echo "$FLOOR" | grep -Eq '^[0-9]{1,2}\.[0-9]{1,2}$' || { echo "bump_version: VERSION must be X.Y, not '$FLOOR'" >&2; exit 1; }

if [ $# -gt 0 ]; then
    TAGS="$(printf '%s\n' "$@")"
else
    TAGS="$(git ls-remote --tags --refs origin 'v*' | sed 's|.*refs/tags/||')"
fi
# the last release: X.Y as X.Y.0, so 1.2.1 sorts after 1.2
LAST="$(printf '%s\n' "$TAGS" | grep -E '^v[0-9]+\.[0-9]+(\.[0-9]+)?$' | sed 's/^v//' |
        awk -F. '{ printf "%d %d %d\n", $1, $2, (NF > 2 ? $3 : 0) }' | sort -k1,1n -k2,2n -k3,3n | tail -1)"

if [ -z "$LAST" ]; then
    echo "$FLOOR"
    exit 0
fi
set -- $LAST
MAJ=$1 MIN=$2 PAT=$3
case "$KIND" in
    patch)
        PAT=$((PAT + 1))
        [ "$PAT" -le 99 ] || { echo "bump_version: $MAJ.$MIN has 99 patches already: release a minor" >&2; exit 1; }
        NEXT="$MAJ.$MIN.$PAT" ;;
    minor)
        MIN=$((MIN + 1))
        if [ "$MIN" -gt 99 ]; then MAJ=$((MAJ + 1)); MIN=0; fi
        NEXT="$MAJ.$MIN" ;;
    major)
        NEXT="$((MAJ + 1)).0" ;;
esac
[ "${NEXT%%.*}" -le 99 ] || { echo "bump_version: $NEXT is past 99" >&2; exit 1; }

# never below the floor (./VERSION, X.Y): a floor above the last release starts there
F_MAJ="${FLOOR%.*}" F_MIN="${FLOOR#*.}"
N_MAJ="${NEXT%%.*}" N_REST="${NEXT#*.}" N_MIN="${N_REST%%.*}"
if [ "$F_MAJ" -gt "$N_MAJ" ] || { [ "$F_MAJ" -eq "$N_MAJ" ] && [ "$F_MIN" -gt "$N_MIN" ]; }; then
    NEXT="$FLOOR"
fi
echo "$NEXT"
