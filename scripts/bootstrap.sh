#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# One-time setup for building CHOPPAH (macOS or Linux):
#   1. fm1-base (the platform) as a git submodule in base/, pinned to a tested commit
#   2. JieLi's Linux toolchain (clang for pi32v2) in ~/.jieli/toolchain
#   3. the three files of JieLi's AC79 SDK the package needs, in ~/fw-AC79_AIoT_SDK (checked by SHA-256)
#   4. Python packages: Pillow (the build's fonts), mido + python-rtmidi (installing over USB-MIDI)
# Re-running is safe: each step is skipped when it is already done.
#   scripts/bootstrap.sh            everything
#   FM1_BASE_REF=<commit> scripts/bootstrap.sh    another fm1-base commit
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
FM1_BASE_URL="https://github.com/charlesvestal/fm1-base.git"
FM1_BASE_REF="${FM1_BASE_REF:-d8ff8f41aa511e03d70a7c565c9fa0f1dd18ae2f}"   # 2026-10-29, felucca-bsp 305c372
JIELI="${JIELI_HOME:-$HOME/.jieli}"
SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
SDK_TAG="AC79NN_SDK_V1.2.1_2023-12-13"
SDK_RAW="https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/$SDK_TAG/cpu/wl82/tools"

say() { printf '\n== %s\n' "$*"; }

say "1/4 fm1-base"
[ -d .git ] || git init -q -b main
if [ ! -f base/tools/build.py ]; then
    git submodule add "$FM1_BASE_URL" base 2>/dev/null || true
    git -C base fetch -q origin "$FM1_BASE_REF"
    git -C base checkout -q "$FM1_BASE_REF"
    git submodule update --init --recursive base
fi
echo "base/ at $(git -C base rev-parse --short HEAD)"

say "2/4 JieLi toolchain"
if [ ! -d "$JIELI/toolchain" ]; then
    mkdir -p "$JIELI"
    curl -fL --retry 3 https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C "$JIELI"
    TC="$(ls -d "$JIELI"/jieli-linux-toolchains-* | sort | tail -1)"
    ln -sfn "$(basename "$TC")" "$JIELI/toolchain"
fi
echo "toolchain: $(readlink "$JIELI/toolchain" || echo "$JIELI/toolchain")"

say "3/4 AC79 SDK files"
mkdir -p "$SDK/cpu/wl82/tools/cfg"
check() {   # file sha256
    f="$SDK/cpu/wl82/tools/$1"
    [ -f "$f" ] && [ "$( (sha256sum "$f" 2>/dev/null || shasum -a 256 "$f") | cut -d' ' -f1)" = "$2" ]
}
fetch() {   # file sha256: gitee drops connections now and then, so a few tries
    check "$1" "$2" && { echo "ok   $1"; return; }
    for i in 1 2 3 4 5; do
        curl -fsSL -o "$SDK/cpu/wl82/tools/$1" "$SDK_RAW/$1" && check "$1" "$2" && { echo "got  $1"; return; }
        sleep 2
    done
    echo "could not get $1 from gitee (try again, or clone the SDK at tag $SDK_TAG into $SDK)"; exit 1
}
fetch uboot.boot 4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4
fetch cfg_tool.bin 276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b
fetch cfg/eq_cfg_hw.bin 41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d

say "4/4 Python packages"
PY="${PYTHON:-python3}"
"$PY" -c 'import PIL, mido, rtmidi' 2>/dev/null && echo "ok   Pillow, mido, python-rtmidi" ||
    "$PY" -m pip install --user Pillow mido python-rtmidi || "$PY" -m pip install --break-system-packages Pillow mido python-rtmidi

if [ "$(uname -s)" = "Darwin" ]; then
    say "macOS: the JieLi toolchain is Linux x86-64; fm1-base's build runs it in a linux/amd64 container"
    command -v docker >/dev/null && echo "docker: $(docker --version)" ||
        echo "Install Docker Desktop or OrbStack (or podman with a 'docker' shim) before ./build.sh"
    case "$ROOT" in /Users/*) ;; *) echo "Keep this folder under /Users so the container can see it." ;; esac
fi

say "done: ./build.sh   (then: ./build.sh flash)"
