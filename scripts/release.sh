#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Release CHOPPAH: tag the release and let .github/workflows/release.yml build, attest and publish it.
#
# A release is a tag, not a merge: the Firmware Hub only shows a firmware as "Built from source on GitHub" when its
# package was built by the shared builder (zvenson/fm1-build@v1), and that builder releases on a tag. So merging to
# main builds and tests (.github/workflows/ci.yml) but releases nothing; a tag releases.
#
#   scripts/release.sh              the next minor (the last tag plus one minor, floor ./VERSION)
#   scripts/release.sh patch        the next patch (1.2 -> 1.2.1); minor and major as well (scripts/bump_version.sh)
#   scripts/release.sh 0.97         this version
#   scripts/release.sh 0.97-rc1    a prerelease (a tag with a suffix)
#
# The tag is pushed, and the workflow does the rest: build, attestation, the release, the site.
set -e
cd "$(dirname "$0")/.."
REMOTE="${REMOTE:-origin}"

V="${1:-}"
case "$V" in
    ''|patch|minor|major) V="$(sh scripts/bump_version.sh "${V:-minor}")" ;;
esac
if ! printf '%s' "$V" | grep -Eq '^[0-9]{1,2}\.[0-9]{1,2}(\.[0-9]{1,2})?(-[A-Za-z0-9]+)?$'; then
    echo "release.sh: '$V' is not a version (X.Y, X.Y.Z or X.Y-suffix)" >&2
    exit 1
fi

# the notes, when they are written: docs/hub/notes_v096.md for v0.96, notes_v102_1.md for v1.2.1
NOTES="$(sh scripts/notes_path.sh "$V")"
if printf '%s' "$V" | grep -q -- '-'; then
    echo "release.sh: $V is a prerelease: it keeps GitHub's changelog, not $NOTES"
elif [ -f "$NOTES" ]; then
    echo "release.sh: notes: $NOTES"
else
    echo "release.sh: no $NOTES: the release keeps GitHub's changelog"
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "release.sh: the working tree is not clean; commit or stash first" >&2
    git status --short
    exit 1
fi
if [ "$(git rev-parse --abbrev-ref HEAD)" != main ]; then
    echo "release.sh: not on main (on $(git rev-parse --abbrev-ref HEAD)); the release is made from main" >&2
    exit 1
fi

git fetch -q --tags "$REMOTE"
if git rev-parse -q --verify "refs/tags/v$V" >/dev/null; then
    echo "release.sh: v$V exists already" >&2
    exit 1
fi

echo "release.sh: tagging v$V at $(git rev-parse --short HEAD)"
git tag "v$V"
git push "$REMOTE" "v$V"

cat <<EOF

Tagged v$V. The workflow builds it, signs the provenance, publishes the release and rebuilds the site:
    gh run watch --repo profikid/fm1-break-choppah
The Firmware Hub picks it up within three hours and shows the "Built from source" key.
EOF
