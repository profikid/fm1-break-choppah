#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build CHOPPAH with fm1-base (base/: run scripts/bootstrap.sh once first).
#   ./build.sh                    app/build/choppah.fwsc (the firmware package)
#   ./build.sh --release X.Y      app/build/choppah-X.Y.fwsc
#   ./build.sh --host             the host simulator: app/build/host/choppah_host
#   ./build.sh --web              the browser emulator: app/build/emu/
#   ./build.sh test               the engine's tests (plain cc, no toolchain needed)
#   ./build.sh bank               the break bank again: app/bank_data.h from breaks/breaks.txt (every build makes it
#                                 when the list, a WAV or tools/prep_breaks.py is newer)
#   ./build.sh info               what the connected FM-1 is running
#   ./build.sh flash [FILE]       install over USB-MIDI (default app/build/choppah.fwsc)
# JIELI_TOOLCHAIN, AC79_SDK and PYTHON override the defaults (~/.jieli/toolchain, ~/fw-AC79_AIoT_SDK, python3).
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"

bank() {   # the breaks marked * in breaks/breaks.txt -> app/bank_data.h (built into the firmware and the emulator)
    [ -f breaks/breaks.txt ] || return 0
    if [ "$1" = force ] || [ ! -f app/bank_data.h ] ||
       [ -n "$(find breaks tools/prep_breaks.py -newer app/bank_data.h \( -name '*.wav' -o -name breaks.txt -o -name prep_breaks.py \) | head -1)" ]; then
        echo "build.sh: the break bank, from breaks/breaks.txt"
        "$PY" tools/prep_breaks.py --list breaks/breaks.txt --break-slices 16 --transients \
            --header app/bank_data.h -o app/build/bank.bin
    fi
}

case "$1" in
    bank)
        bank force; exit ;;
    test)
        exec sh tests/engine/run.sh ;;
    info)
        exec "$PY" base/tools/fm1_install.py --info ;;
    flash)
        PKG="${2:-app/build/choppah.fwsc}"
        [ -f "$PKG" ] || { echo "build.sh: no $PKG (./build.sh first)"; exit 1; }
        echo "Installing $PKG. Keep the FM-1 plugged in and charged until it restarts."
        exec "$PY" base/tools/fm1_install.py "$PKG" ;;
esac
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (scripts/bootstrap.sh)"; exit 1; }
bank
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (scripts/bootstrap.sh)"; exit 1; }
exec "$PY" base/tools/build.py app "$@"
