#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build and publish CHOPPAH's site: front page, web installer, browser emulator, manual, firmware download.
#   scripts/publish_site.sh VERSION DEST
#     VERSION  X.Y[.Z]: the release (identity FM-1_86XXYYZZ)
#     DEST     where the site goes: a local folder (/var/www/choppa) or user@host:/path (rsync over SSH)
# Needs scripts/bootstrap.sh done (fm1-base, JieLi toolchain, SDK files) and Emscripten (emcc on PATH, or
# EMSDK=~/emsdk) for the emulator. The public build never includes app/bank_data.h: recorded breaks stay private.
set -e
cd "$(dirname "$0")/.."
VER="${1:?usage: scripts/publish_site.sh VERSION DEST}"
DEST="${2:?usage: scripts/publish_site.sh VERSION DEST}"
[ -n "$EMSDK" ] && [ -f "$EMSDK/emsdk_env.sh" ] && . "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
command -v emcc >/dev/null || { echo "publish_site: no emcc (install emsdk, or set EMSDK)"; exit 1; }
PY="${PYTHON:-python3}"

HELD=""
if [ -f app/bank_data.h ]; then            # the private Amen bank out of the way for the public build
    HELD="$(mktemp -d)"
    mv app/bank_data.h "$HELD/"
fi
restore() { [ -n "$HELD" ] && mv "$HELD/bank_data.h" app/bank_data.h && rmdir "$HELD"; HELD=""; }
trap restore EXIT

./build.sh --release "$VER"
./build.sh --web
rm -rf build/site
"$PY" base/tools/make_site.py app "app/build/choppah-$VER.fwsc" "$VER" build/site
restore

case "$DEST" in
    *:*) rsync -az --delete build/site/ "$DEST/" ;;
    *)   mkdir -p "$DEST" && rsync -a --delete build/site/ "$DEST/" ;;
esac
echo "published CHOPPAH $VER to $DEST"
