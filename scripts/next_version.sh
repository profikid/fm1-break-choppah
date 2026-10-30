#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The version the next release gets: the last release tag plus one minor (v0.4 -> 0.5; v0.99 -> 1.0, the
# identity has two digits each), never below ./VERSION. ./VERSION is the floor: set it to 1.0 to start 1.x.
# The first release (no tags yet) is ./VERSION itself.
#   scripts/next_version.sh            the tags from origin (CI)
#   scripts/next_version.sh v0.1 v0.2  these tags instead (tests)
set -e
cd "$(dirname "$0")/.."
FLOOR="$(tr -d '[:space:]' < VERSION)"
echo "$FLOOR" | grep -Eq '^[0-9]{1,2}\.[0-9]{1,2}$' || { echo "next_version: VERSION must be X.Y, not '$FLOOR'" >&2; exit 1; }

if [ $# -gt 0 ]; then
    TAGS="$(printf '%s\n' "$@")"
else
    TAGS="$(git ls-remote --tags --refs origin 'v*' | sed 's|.*refs/tags/||')"
fi
LAST="$(printf '%s\n' "$TAGS" | grep -E '^v[0-9]+\.[0-9]+$' | sed 's/^v//' | sort -t. -k1,1n -k2,2n | tail -1)"

if [ -z "$LAST" ]; then
    echo "$FLOOR"
    exit 0
fi
MAJ="${LAST%.*}"
MIN="${LAST#*.}"
MIN=$((MIN + 1))
if [ "$MIN" -gt 99 ]; then
    MAJ=$((MAJ + 1))
    MIN=0
fi
NEXT="$MAJ.$MIN"
printf '%s\n%s\n' "$NEXT" "$FLOOR" | sort -t. -k1,1n -k2,2n | tail -1
