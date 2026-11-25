# CHOPPAH: notes for Claude sessions

Firmware for the M-VAVE FM-1 (JieLi AC791N): an Amen break chopper. Spec: `docs/PRD.md` (feature IDs A-*, B-*,
E-*, N-*; milestones M0–M8). Keep the PRD in step when scope changes.

## Layout

`app/app.c` is the core (settings, patterns, MIDI, the render); it ends by including `app/ui/*.c` in order (common,
fx, pads, keys, bta, global, arp, seq, mc, mixer, screen, demo, main): one unity build, so a page file sees what the
files before it define. `app/dsp/*.c` are separate `o2` units (brk, brk_fx, adpcm, brk_math).

**MATHCORE** (v1.5.5, docs/PRD_MATHCORE.md §14–19; EDIT: LIVE, LANE, SCENE in `app/ui/mc.c`): the Slice Map is heard
when a break loads (`mc_auto_roles`, brk.c), shown as K S H P on the slice blocks; 16 scenes (`MC_PRESET` in app.c,
`OBJ_MC` 0xD6000, magic MCS5: change the presets, bump it) of four lanes each, played from the black keys
(`brk_mc_jump`: a groove on the bar, a ONCE scene on the beat), the chain and the keys' scenes in `OBJ_MQ` 0xD8000;
LIVE's macros DENSITY / MATH / SPEED / CHAOS (`brk_mc_macro`, settings `mc_macro`). Every hit is a slice on the
break's own voice (`chop_hit`: no voices of their own, nothing laid over the loop); SEQ's drum lanes put their role's
slice in the loop's place (`drum_slice`). Nothing is synthesised: the kick and snare/hat engines are gone (their
settings bytes reserved).

## Hard rules

- `app/dsp/` stays platform-free: no `plat_*`, no fm1-base headers, no libc beyond `<stdint.h>`. It must build with
  plain `cc` (tests/engine) and as fm1-base `o2` units.
- **No double precision** anywhere in `app/` (the device build rejects it): `f` suffix on every float literal,
  no `math.h` in dsp (use `brk_math.h`). `tests/engine/run.sh` builds with `-Wdouble-promotion -Werror`.
- No 64-bit division on the device (no libgcc `__udivdi3`): keep sample maths in 32 bits.
- The render (`app_render`, `brk_render`) runs in the audio interrupt: no flash writes, no blocking, no
  allocation. Main loop → render through `brk_*` setters (single words) and the key queue.
- Pool RAM budget (PRD 8.2): canvas 240×60 (28 KB) + the PCM buffer + the delay lines; the build wants 8 KB of the
  344 KB spare (21 Nov 2026: 312,872 B used).
- Flash writes stall audio: only on SAVE or while stopped.
- Settings (`settings_t`, magic CHP7) only grow at the end: append new fields after the last one (`fxx`, each FX slot's extras) and
  give them defaults in `settings_defaults()`; a shorter (older) save loads into the front (`SET_SIZE_MIN`: the
  offset of the first appended field, `rsv2`, the dropped fills' bytes; keep it there).
  Anything else needs a new magic (saves lost). Patterns are their own object (`OBJ_PAT`, 0xDA000, magic PAT1).
- Bluetooth MIDI is on (`ble = true`, `ble_stack = "own"`: fm1-base's own stack, nothing of JieLi's); the GLOBAL
  page switches it, `APP_BLE_AT_BOOT` follows the saved setting. It costs ~40 KB of flash. Bluetooth audio out
  (A2DP) was removed 21 Nov 2026 (v1.5, Laurens's call) to make room for the split (`bt_audio = false`; its
  settings byte `bta_on` stays as reserved). GLOBAL has three parts: MIDI, PITCH, CHANNEL (`GPARTS`).
- Flash: the app slot is 581,564 B (`APP_SLOT` 0x8DFBC in base/tools/fm1pkg_make.py: not 581 KiB); 549,084 B used on
  21 Nov 2026 (the split, Bluetooth audio out): **~32 KB left**. RAM .data+.bss 90,128 of 98,304 B. fm1-base's resource region (`resources`, APP_RES_BASE) could
  take some of the break bank out of the app slot (it only fits the data area's ~268 KB, not the whole bank). The break bank (`CH_BANK`, ~369 KB) is most of it. Watch the image line: when it fills again, anything new needs something taken out first (ask Laurens what).
  Block-rate engine code is `BRK_COLD` (built for size) and calls the shared maths (`brk_exp2f_c` and the like,
  brk_math.c; the UI too) instead of an inline copy; the FX chain's per-sample loop (`fx_samples`) is `BRK_RAM`:
  .ram_text on the FM-1 (fast, packed in flash), so nothing it reaches may be a call (`BRK_INL` in brk_fx.c forces
  inlining; the build fails on "ram_text contains calls"; flash code reaches it through a pointer). CPU: the render
  must stay under its 5.8 ms block, or audio glitches and the watchdog resets the FM-1; `APP_OVERLOAD` sheds FX slots
  as a last resort (PRD E-7). Compare an engine change's cost on the host against the last release before shipping (`APP_SYS_TEXT` in app.h draws the platform's screens in CHOPPAH's faces,
  saving fm1-base's fonts): check `./build.sh`'s image line;
  the canvas is 240×60 (`APP_CANVAS_PX`), so draw in strips of ≤ 60 rows (draw_fx / draw_key draw two bands).
- **The split** (PRD S-030 to S-032): `split_load` (app.c) builds one 2-bar break from a bar of break A and a bar of
  break B (repitched to A's bar) in `pcm_a`; `brk_break_t.split` is its first half's slice count, which the engine
  uses for the half latch (`brk_half`) and the layer (a second key lane, `kl[1]`, with voices `lv[2]`). ALGORITHM's break turn (`break_turn`, ui/main.c) moves break B while bar 2 is latched. Without a
  split (`split` 0) the engine behaves as before. Its page is HOME's third part (`PG_SPLIT`: HOME cycles PERFORM,
  GENERATOR, SPLIT; `split_value` / `split_turn` / `draw_split` in ui/global.c), since v1.5.1 (it was GLOBAL's).
- Breaks: `breaks/breaks.txt` (paths relative to `breaks/`) marks with `*` the WAVs built into the firmware;
  `./build.sh` regenerates `app/bank_data.h` (never commit it, never edit it). The bank has 400 KB (`IMAGE_CAPACITY`
  in `tools/prep_breaks.py`) and 16 slots, a break at most `CH_PCM_MAX` 106,496 samples (pool ~326 of 344 KB). The breaks are the bank's only (no bank: silence; brk_synth is test material only,
  `tests/engine/`). The breaks stay 2 bars (32 steps). The player's **pack** (PRD S-003, v1.6; 1.4-1.5 had one
  break): up to 16 breaks in two banks in the built-in bank's format, bank 1 the data area 0x97000-0xD5FFF (63
  sectors, where the custom break was) and bank 2 XDATA1 0xE5000-0xFBFFF (23 sectors; "AT" never at a sector's 0xF06:
  `pk_at` refuses it, the page and `tools/cu_frames.py` nudge it out) (`PK_BASE`, `PK_SECT`, `pk_scan`: each bank
  checked whole), written over USB SysEx by the site's break manager (`app/site/breaks.html` -> site `breaks/`;
  `app_sysex` in ui/main.c: F0 7D 43 48 CMD 7-bit-packed DATA F7, the bank in CMD's bits 4-5; INFO version 2, 24
  bytes, the first 12 as version 1; BEGIN may also carry the pack's bytes, those before this bank and its name (16),
  for the upload screen: `up_*` in ui/demo.c, PRD S-034, tests/pack_screen.sh); the pack comes first (`n_user`, `slot_of`: bank 1's, bank 2's, the built-in
  ones), its first always the break at power-on; a pack change sets the split's B back to 0. fm1-base's resource
  region can't hold it (it only takes the build's own payload). Tests: `tests/pack.sh` (both banks: order, play,
  reboot, read back, "AT" refused, delete), `tests/custom_break.sh` (a break of one, the 1.5 page's way),
  `tests/page_parity.sh` (the page's conversion and a bank of three = prep_breaks.py, byte for byte; the AT nudge =
  cu_frames.py's), `tests/page_warp.sh` (the page's groove warp: hits onto the grid sample for sample), `tests/page_send.sh` (Send
  in Chrome over a bad link: frames and answers lost, late, a restart; the pack must still arrive byte for byte). The
  send never trusts the link: every command is sent again when its answer is lost (`must`, 8 tries; all are safe twice),
  DATA is 448 bytes a frame (521 on the wire, inside the 594 felucca-bsp always takes) and answers with its offset
  (1.6), and a bank that doesn't check at COMMIT, or an FM-1 that restarted, is sent again from BEGIN (`sendBank`). The page's library is Cyberworm's Amens from breaks/
  (tools/site_style.py converts them at site build); a page-only change goes live by running pages.yml (version: the
  latest release, package-from: release), no firmware release needed.
- **Packs v2 and the hub** (docs/PRD_PACKS.md): the pack file `.chpk` is `app/site/chpk.js` (MPL-2.0, no DOM: META,
  BANK, SCNE, OVLY, THMB chunks; `tests/chpk.test.mjs`, its bank byte for byte = breaks.html's `buildBank`). The site
  build copies it next to the builder (`breaks/chpk.js`, tools/site_style.py); the page's section 4 edits the pack's
  name, author, tags, theme and picture, each break's loop settings (`e.slot`), exports / imports `.chpk` (a `.chb`
  still reads), uploads to the hub and opens `#hub=<id>`. The hub is **choppah.app**, a private repo
  (`profikid/choppah-hub`: a Cloudflare Worker, D1, R2, Email Service), which vendors chpk.js pinned to a commit
  (`npm run vendor`) and proxies this site from GitHub Pages byte for byte. Change chpk.js here, never there.
  The FM-1 link is `app/site/fm1link.js` (MPL-2.0, no DOM: `FM1Link` open / status / install, breaks.html's must and
  sendBank rules; `tests/fm1link.test.mjs` against `tests/page_send/fake_fm1.js`, which plays a 1.6 or a 1.7): the
  hub vendors it with chpk.js and installs a pack straight onto the FM-1 (PRD_PACKS §10.7; `installImages` in chpk.js;
  a pack's `.chpk` downloads only for its owner or when the owner allows it).
  The builder's cut is `app/site/breakcut.js` (MPL-2.0, no DOM: level, slicePoints, warp copied from breaks.html
  unchanged, `tests/breakcut.test.mjs` holds them equal; `cut`, `makeBreak`, `autoEdit`): the hub's library slicer
  (PRD_PACKS §10.8) makes breaks from your samples with it. Change those functions in both places, or the test fails.
  It also cuts a break from the drums alone (an edit's `drums`: beats.js `stems` on the bars and half a second around
  them) and makes a pack's overlay (`makeOverlay`: bars on the edit's grid, the rest or the mix, `seamless`, `level`, the
  best of 22.05 / 16 / 11.025 kHz that fits): the hub's pack maker (library and MCP) uses them. A slow break (under
  ~99.4 BPM) keeps the rate that fits CH_PCM_MAX (`breakRate`, e.g. 22013 Hz): chpk.js reads any break rate of
  8000–22050 Hz (the FM-1 plays a slot at any rate); the overlay keeps to 22050 / 16000 / 11025.
  MATHCORE in the browser is `app/site/mathcore.js` (MPL-2.0, no DOM: SCNE and CHAN to and from bytes, the factory
  scenes (a copy of app.c's MC_PRESET: `tests/mathcore.test.mjs` reads app.c and brk.c and holds them equal, and
  tests/pack_v2.sh holds the factory scenes and chain to the host's GETSET byte for byte), `autoRoles` (brk.c
  mc_auto_roles) and `Player` (brk.c mc_tick: the same hash and choices)): the hub's MATHCORE editor (PRD_PACKS
  §10.9). Change MC_PRESET, mc_tick or mc_auto_roles, change mathcore.js with them. A pack's chain and black keys are
  its CHAN chunk (1.7: `pk_chan`, applied in `mc_pack`; without it the factory's). Live from the editor (INFO version
  4, PRD_PACKS §10.10): SysEx PUTSET (8: GETSET's scenes and chain written into RAM, playing or not), STATUS (9:
  `mc_status`, 32 bytes) and MCCTL (10: `mc_remote`, what PLAY on LANE / LIVE, a black key, LIVE's knobs and SEL + K
  do); `tests/mc_live.sh`. fm1link.js `mathcore`, `putScene`, `putChain`, `mcStatus`, `playScene` ... speak them.
  The builder's ears are `app/site/beats.js` (no DOM: onsets, tempo, bars, `findLoops`, `stems` (harmonic /
  percussive), `seamless`; `tests/beats.test.mjs` on the classics) in a module Worker (`beats_worker.js`); on
  choppah.app the page also reads your hub library and saves to it (script 3 of breaks.html). `tests/page_pack.sh`:
  the builder in Chrome, a track to a pack with an overlay and back.
  **The firmware reads v2 (1.7)**: bank version 2 (`brk_bank.h`: chunks after the slot table, PACK SSET SCNE THEM
  OVLY; `pk_chunks`), INFO version 3, GETSET (7: the scenes, the chain and the LOOP settings as in RAM). A pack
  break's settings (`slot_apply`: gain, tempo, the loop's window tiled over the two bars by `slot_window`, pitch and
  swing on top of the global ones; `slot_roles` through the tiling) come from `mq.ss[]` (OBJ_MQ, 1.6's 88-byte saves
  load into its front), which `mc_pack` fills from the pack's SSET when the pack changes (its id hashed into
  `mq.rsv`; a v1 pack's tag is its bank's CRC; a 1.6 save with a v1 pack keeps its scenes); HOME's fourth part LOOP
  (`PG_LOOP`, page 7, ui/global.c) edits them, SEL + a knob: the pack's own. The theme (`theme_apply`): gfx.c's
  `pal[]` and CHOPPAH's ground (`C_BLACK` is `th_bg` in the app) and blocks; a built-in break: the factory's. The
  overlay (`brk_overlay`, brk.c): ADPCM read from the flash as it plays, marks every 4096 samples, on the bars;
  `brk_overlay_on` under its pack's breaks; MIXER K2 OVERLAY (`set.lane_level`), GROOVE K2 OVL ROUTE
  (`set.ov_route`: the chain's `thru` or `post`). The builder sends v2 to INFO 3 (`chpk.deviceBanks`) and pulls the
  FM-1's changes back first. Tests: `tests/pack_v2.sh`, `test_overlay`.
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
come from the host scenarios: `tools/doc_shots.py` (after `./build.sh --host`) runs them all and refreshes every
picture in `app/docs/img`, the README's composites and the Hub card (`docs/img`) from its `MAP`; run it after any
change to what the screen draws (`--check` lists stale ones). A new picture in the docs gets a line in `MAP`.

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

- `ci.yml` — every push to main: the engine's tests, the firmware build, the emulator's build. Releases nothing, but
  uploads the package as an artifact (`choppah-main-<sha>`, `choppah-main-<sha7>.fwsc`, 30 days): the bleeding edge to
  test on a device. Fetch it: `gh run download <run id> -R profikid/fm1-break-choppah -n choppah-main-<sha>`.
  Once it has passed, it also sends that package to choppah.app's beta (`choppah.app/beta`: the installer with the
  newest test build), signed in by the run's GitHub OIDC token (`id-token: write`; choppah-hub's src/beta.js checks
  it's this repository, main and ci.yml). So `/beta` is always the latest change on main that builds and passes.
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
