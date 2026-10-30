#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# CHOPPAH's engine tests: app/dsp/ built with the computer's own C compiler (no FM-1 toolchain, no fm1-base).
#   tests/engine/run.sh [OUTDIR]     WAV renders go to OUTDIR (default build/engine)
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-build/engine}"
mkdir -p "$OUT"
CC="${CC:-cc}"
# the device build rejects double precision: warn here on any float that gets promoted
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion -Wno-unused-parameter \
    -Iapp/dsp -c app/dsp/brk.c -o "$OUT/brk.o"
for f in brk_fx brk_synth adpcm; do
    "$CC" -std=c99 -O2 -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion -Iapp/dsp -c app/dsp/$f.c -o "$OUT/$f.o"
done
"$CC" -std=c99 -O2 -Wall -Iapp/dsp tests/engine/test_engine.c "$OUT"/brk.o "$OUT"/brk_fx.o "$OUT"/brk_synth.o \
    "$OUT"/adpcm.o -lm -o "$OUT/test_engine"
"$OUT/test_engine" "$OUT"
