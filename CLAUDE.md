# CHOPPAH: notes for Claude sessions

Firmware for the M-VAVE FM-1 (JieLi AC791N): an Amen break chopper. Spec: `docs/PRD.md` (feature IDs A-*, B-*,
E-*, N-*; milestones M0–M8). Keep the PRD in step when scope changes.

## Layout

`app/app.c` is the core (settings, patterns, MIDI, the render); it ends by including `app/ui/*.c` in order (common,
fx, pads, keys, bta, global, arp, seq, kick, perc, mixer, screen, demo, main): one unity build, so a page file sees what the
files before it define. `app/dsp/*.c` are separate `o2` units (brk, brk_fx, kick, perc, …).

## Hard rules

- `app/dsp/` stays platform-free: no `plat_*`, no fm1-base headers, no libc beyond `<stdint.h>`. It must build with
  plain `cc` (tests/engine) and as fm1-base `o2` units.
- **No double precision** anywhere in `app/` (the device build rejects it): `f` suffix on every float literal,
  no `math.h` in dsp (use `brk_math.h`). `tests/engine/run.sh` builds with `-Wdouble-promotion -Werror`.
- No 64-bit division on the device (no libgcc `__udivdi3`): keep sample maths in 32 bits.
- The render (`app_render`, `brk_render`) runs in the audio interrupt: no flash writes, no blocking, no
  allocation. Main loop → render through `brk_*` setters (single words) and the key queue.
- Pool RAM budget (PRD 8.2): canvas 240×60 (28 KB) + PCM buffers + the Bluetooth audio queue; the build wants 8 KB
  of the 344 KB spare.
- Flash writes stall audio: only on SAVE or while stopped.
- Settings (`settings_t`, magic CHP7) only grow at the end: append new fields after the last one (`fxx`, each FX slot's extras) and
  give them defaults in `settings_defaults()`; a shorter (older) save loads into the front (`SET_SIZE_MIN`: the
  offset of the first appended field, `rsv2`, the dropped fills' bytes; keep it there).
  Anything else needs a new magic (saves lost). Patterns are their own object (`OBJ_PAT`, 0xDA000, magic PAT1).
- Bluetooth MIDI is on (`ble = true`, `ble_stack = "own"`: fm1-base's own stack, nothing of JieLi's); the GLOBAL
  page switches it, `APP_BLE_AT_BOOT` follows the saved setting. It costs ~40 KB of flash. Bluetooth audio out
  (`bt_audio = true`, A2DP) is on too: ~37 KB of flash, its packet queue in the pool (`BP_MQ_ATTR` in app.h).
- Flash: the app slot is 581,564 B (`APP_SLOT` 0x8DFBC in base/tools/fm1pkg_make.py: not 581 KiB); 579,460 B used on
  14 Nov 2026 (v1.3.2: fm1-base c8e0599 and the screen reader): **~2.1 KB left, the flash is full**. Bluetooth audio
  (~37 KB, A2DP + SBC) has no smaller build in fm1-base; fm1-base's resource region (`resources`, APP_RES_BASE) could
  take some of the break bank out of the app slot (it only fits the data area's ~268 KB, not the whole bank). The break bank (`CH_BANK`, ~369 KB) is most of it. Anything new needs something taken out first (ask Laurens what).
  Block-rate engine code is `BRK_COLD` (built for size) and calls the shared maths (`brk_exp2f_c` and the like,
  brk_math.c; the UI too) instead of an inline copy; the FX chain's per-sample loop (`fx_samples`) is `BRK_RAM`:
  .ram_text on the FM-1 (fast, packed in flash), so nothing it reaches may be a call (`BRK_INL` in brk_fx.c forces
  inlining; the build fails on "ram_text contains calls"; flash code reaches it through a pointer). CPU: the render
  must stay under its 5.8 ms block, or audio glitches and the watchdog resets the FM-1; `APP_OVERLOAD` sheds FX slots
  as a last resort (PRD E-7). Compare an engine change's cost on the host against the last release before shipping (`APP_SYS_TEXT` in app.h draws the platform's screens in CHOPPAH's faces,
  saving fm1-base's fonts): check `./build.sh`'s image line;
  the canvas is 240×60 (`APP_CANVAS_PX`), so draw in strips of ≤ 60 rows (draw_fx / draw_key draw two bands).
- Breaks: `breaks/breaks.txt` (paths relative to `breaks/`) marks with `*` the WAVs built into the firmware;
  `./build.sh` regenerates `app/bank_data.h` (never commit it, never edit it). The bank has 400 KB (`IMAGE_CAPACITY`
  in `tools/prep_breaks.py`) and 16 slots, a break at most `CH_PCM_MAX` 106,496 samples (pool ~326 of 344 KB). The breaks are the bank's only: brk_synth is the no-bank fallback (and
  the tests'). No loading of samples of your own (dropped from scope), and the breaks stay 2 bars (32 steps).
- **Releases are tags, not merges** (since 6 Nov 2026): `scripts/release.sh` tags vX.Y and pushes it; the shared
  builder (`zvenson/fm1-build@v1`, via `.github/workflows/release.yml`) builds it on GitHub, signs the build
  provenance, publishes the release and rebuilds the site. The Firmware Hub only shows "Built from source" for
  packages built that way. A push to main builds and tests (`.github/workflows/ci.yml`) but releases nothing.
  Releases come in three kinds (`scripts/bump_version.sh`, from the last `vX.Y[.Z]` tag, floor `VERSION`): patch
  1.2 → 1.2.1, minor 1.2 → 1.3, major 1.2 → 2.0. Tag with `scripts/release.sh [patch|minor|major|X.Y[.Z]]` (default
  minor) or the Actions workflow `bump.yml` ("Release: patch, minor or major", workflow_dispatch: it tags and runs
  release.yml on the tag, since a tag pushed with GITHUB_TOKEN starts nothing). A Claude session can't push tags
  through its proxy, but can run bump.yml: `gh api -X POST repos/profikid/fm1-break-choppah/actions/workflows/bump.yml/dispatches
  -f ref=main -f 'inputs[bump]=patch'` (`gh workflow run` needs GraphQL, which is blocked). Don't tag by hand or edit `VERSION` for a release.
  Release notes live in `docs/hub/notes_vXY.md` (v0.96 → `notes_v096.md`), a patch's in `notes_vXY_Z.md`
  (v1.2.1 → `notes_v102_1.md`; `scripts/notes_path.sh`); without that file the release keeps GitHub's
  auto-changelog. Write the notes **before** the tag, so the release has them.
- **The build is bit-reproduceerbaar**: the same commit gives the same `.fwsc` (v0.96 built locally = the published
  `85fabff9…`, 611,743 B). The site therefore serves the *attested* package of the release, not a second build.
- **Always push to GitHub after changes** (Laurens' standing rule, 4 Nov 2026): commit and push as soon as a change is
  done, before moving on. Finished, tested work goes to main; work in progress that doesn't build or fit
  yet goes to a branch of its own and is pushed there.

## The screen reader (fm1-reader)

fm1-base's `device/announce.c` sends the screen to a reader; CHOPPAH's side is in `app/ui/main.c` ("the screen
reader": `speak()`, `screen_text()`, `foot_knob()`) and `app_say` / `app_warn` (common.c). The words are
`app/words.tsv`; each build writes its words file into `app/web/words/` (committed, every one kept: devices point at
them; never edit or delete a published one). After changing any text on the screen run `./build.sh --host` and
`tools/sr_audit.sh` (fm1-reader cloned next to this repo, or FM1_READER=...): no errors, no warnings. The reader's
text buffer shares the demo's stars in the pool (`sp_buf`).

## The update screen

The screen the FM-1 shows while an update is written ("UPDATING", "DO NOT UNPLUG", the bar) is the update loader's,
built from felucca-bsp (`base/bsp/loader/`), which has no hook: `build.sh` copies `app/loader/ldr_screen.c` over
`base/bsp/loader/ldr_screen.c` for a device build and puts the original back when it ends (never commit the
submodule changed). Keep its function names, the panel's start and RAM-only buffers as felucca-bsp's: the loader runs
while the app area is rewritten. It ships in the package, so it shows when that package is installed.

## Site

`base/tools/make_site.py` builds the GitHub Pages site; `tools/site_style.py` then gives it CHOPPAH's look
(`app/site/site.css`; the installer and the emulator, fm1-base's pages, also `app/site/install.css` and
`app/site/emu.css`: only their style, never their markup, ids or script) and puts the interactive control map (`app/site/controlmap.html`) into the manual's section
"The control map". When a page's controls change, update that file's `PAGES` list with the manual. Manual screenshots
come from the host scenarios (`app/build/scenarios/*.png`, copied into `app/docs/img`).

## Commands

```
./build.sh test        # engine tests (fast, run after every dsp change)
./build.sh             # firmware (needs scripts/bootstrap.sh, Docker/OrbStack on macOS)
./build.sh --host      # host simulator
./build.sh flash       # macOS Terminal only: the sandboxed shell has no USB access
scripts/release.sh     # release: tag vX.Y (the next minor) and push it; the workflow builds and attests it
scripts/release.sh patch # the next patch (1.2 -> 1.2.1); or major
scripts/release.sh 0.97  # release this version
```

## The workflows

- `ci.yml` — every push to main: the engine's tests, the firmware build, the emulator's build. Releases nothing.
- `bump.yml` — run by hand (patch / minor / major): tags the next version and runs `release.yml` on that tag.
- `release.yml` — a tag vX.Y[.Z] (pushed, or bump.yml's dispatch on it): the shared builder (`zvenson/fm1-build@v1`) builds the package, signs the build
  provenance (an attestation), and the release is published with it; the site is rebuilt from the same package.
  This is what makes the Firmware Hub show "Built from source on GitHub" — a build in another workflow does not.
  The release's notes are `docs/hub/notes_vXY.md`; write them before the tag.
- `pages.yml` — the site, called by `release.yml` with the version. It takes the *attested* package from the
  release's artifact, so the site and the Hub serve the same bytes.

## Platform facts (fm1-base 2d8ed74, Nov 2026)

44.1 kHz stereo, 256-frame blocks; 581,564 B app flash; 96 KB RAM + 336 KB pool; 240×240 screen; 27 keys
(16 white / 11 black, `PLAT_WHITE_KEYS` / `PLAT_BLACK_KEYS`); 14 buttons (`B_*`), 7 encoders (`EN_*`), MASTER pot.
Store objects ≤ 3840 B; data area 0x97000–0xDFFFF, XDATA0 0x93000–0x96FFF, XDATA1 0xE5000–0xFBFFF (keep bytes
0xF00–0xF07 of each XDATA sector erased).
