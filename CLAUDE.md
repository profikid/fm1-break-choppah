# CHOPPAH: notes for Claude sessions

Firmware for the M-VAVE FM-1 (JieLi AC791N): an Amen break chopper. Spec: `docs/PRD.md` (feature IDs A-*, B-*,
E-*, N-*; milestones M0–M8). Keep the PRD in step when scope changes.

## Hard rules

- `app/dsp/` stays platform-free: no `plat_*`, no fm1-base headers, no libc beyond `<stdint.h>`. It must build with
  plain `cc` (tests/engine) and as fm1-base `o2` units.
- **No double precision** anywhere in `app/` (the device build rejects it): `f` suffix on every float literal,
  no `math.h` in dsp (use `brk_math.h`). `tests/engine/run.sh` builds with `-Wdouble-promotion -Werror`.
- No 64-bit division on the device (no libgcc `__udivdi3`): keep sample maths in 32 bits.
- The render (`app_render`, `brk_render`) runs in the audio interrupt: no flash writes, no blocking, no
  allocation. Main loop → render through `brk_*` setters (single words) and the key queue.
- Pool RAM budget (PRD 8.2): canvas 240×96 (45 KB) + PCM buffers; stay under 336 KB total.
- Flash writes stall audio: only on SAVE or while stopped.
- Settings (`settings_t`, magic CHP6) only grow at the end: append new fields after `kick` and give them defaults in
  `settings_defaults()`; a shorter (older) save loads into the front. Anything else needs a new magic (saves lost).
- Bluetooth MIDI is on (`ble = true`, `ble_stack = "own"`: fm1-base's own stack, nothing of JieLi's); the GLOBAL
  page switches it, `APP_BLE_AT_BOOT` follows the saved setting. It costs ~40 KB of flash.
- Breaks: `breaks/breaks.txt` (paths relative to `breaks/`) marks with `*` the WAVs built into the firmware;
  `./build.sh` regenerates `app/bank_data.h` (never commit it, never edit it). The bank has 400 KB (`IMAGE_CAPACITY`
  in `tools/prep_breaks.py`) and 16 slots, a break at most `CH_PCM_MAX` 106,496 samples (pool ~325 of 344 KB); the
  image is ~530 KB of the 581 KB app slot. The breaks are the bank's only: brk_synth is the no-bank fallback (and
  the tests').
- Releases are automatic: every push to main is the last `vX.Y` tag + 1 minor (`scripts/next_version.sh`, floor
  `VERSION`). Don't tag by hand or bump `VERSION` for a minor; raise `VERSION` only for a new major.

## Commands

```
./build.sh test        # engine tests (fast, run after every dsp change)
./build.sh             # firmware (needs scripts/bootstrap.sh, Docker/OrbStack on macOS)
./build.sh --host      # host simulator
./build.sh flash       # macOS Terminal only: the sandboxed shell has no USB access
```

## Platform facts (fm1-base, Oct 2026)

44.1 kHz stereo, 256-frame blocks; 581 KB app flash; 96 KB RAM + 336 KB pool; 240×240 screen; 27 keys
(16 white / 11 black, `PLAT_WHITE_KEYS` / `PLAT_BLACK_KEYS`); 14 buttons (`B_*`), 7 encoders (`EN_*`), MASTER pot.
Store objects ≤ 3840 B; data area 0x97000–0xDFFFF, XDATA0 0x93000–0x96FFF, XDATA1 0xE5000–0xFBFFF (keep bytes
0xF00–0xF07 of each XDATA sector erased).
