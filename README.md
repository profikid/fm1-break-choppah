# CHOPPAH: an Amen chopper for the M-VAVE FM-1

CHOPPAH turns the [M-VAVE FM-1](https://www.m-vave.com/) into a breakbeat chopper: a break cut into sixteen slices
on the white keys, momentary effect pads on the black keys, a generator that rearranges the break as it plays, and
ring modulator, hi-pass, flanger, degrader and a DJ filter on the way out. Built on
[fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0).

![CHOPPAH's screens: the splash, the start screen, the slicer view, playing, a slice held, a roll](docs/img/screens.png)

- **Try it in your browser** (no FM-1 needed): <https://profikid.github.io/fm1-break-choppah/emu/>
- **Install it** (Chrome or Edge, with the FM-1 plugged in): <https://profikid.github.io/fm1-break-choppah/install/>
- **Read the manual**: <https://profikid.github.io/fm1-break-choppah/manual/> (source: [app/docs/MANUAL.md](app/docs/MANUAL.md))
- **Download the firmware**: [releases](https://github.com/profikid/fm1-break-choppah/releases), or from the site

Installing is at your own risk; the installer can put M-VAVE's own firmware back, and an FM-1 that fails to start
twice comes up in a safe mode the installer can still reach. If your FM-1 runs M-VAVE's FM1 Open firmware, restore
M-VAVE's original firmware first ([docs/FLASHING.md](docs/FLASHING.md)).

**Status: 0.1, early.** The engine, the slicer, the pads, the generator and the effects work on the device. Saved
patterns, the step sequencer, the looper, MIDI sync and loading your own breaks are next: see the
[PRD](docs/PRD.md) for the whole plan.

## Panel

| Control | Does |
| --- | --- |
| White keys 1–16 | Slices: at the next 16th while playing, at once while stopped; hold to loop |
| Black keys 1–11 | Pads, while held: ROLL 1/8 · 1/16 · 1/32 (with 1/8 or 1/16: triplets) · REVERSE · FREEZE · HALF · TAPE STOP · GATE · RANDOM · FILTER THROW · DUB (coming) |
| PLAY | Play / stop · HOME + PLAY: back to bar 1 |
| SELECT | Tempo (SEL held: 0.1 BPM) · tap SEL four times: tap tempo |
| OCT− / OCT+ | Pitch in semitones (hold: faster) · both: 0 |
| ALGORITHM | The next break |
| HOME | PERFORM: K1 DJ filter · K2 swing · K3 complexity · K4 crunch |
| ARP | GENERATOR: complexity · anchor · roll · fill · SEL + ARP: random pattern |
| FX | FX: ALGORITHM picks the effect, K1–K4 its settings, FX again turns it on or off |
| SAVE | Keep the settings |

## Breaks

The public build plays a synthesised Amen-style break, made by a small drum synth at boot: no recording ships with
it. To build your own firmware with recorded breaks (the bank goes into the image, up to 8):

```
python3 tools/prep_breaks.py amen.wav:136.9:2:"AMEN" think.wav::2 --slices 8 --transients --header app/bank_data.h
./build.sh
```

`app/bank_data.h` is git-ignored and left out of every published build: recorded breaks belong to whoever made them.
Uploading breaks over USB without rebuilding is milestone M5.

## Building

```
scripts/bootstrap.sh    # once: fm1-base (submodule), JieLi toolchain, AC79 SDK files, Python packages
./build.sh test         # the engine's tests, plain cc, no toolchain needed
./build.sh              # app/build/choppah.fwsc (macOS: the toolchain runs in Docker / OrbStack)
./build.sh --host       # the host simulator: app/build/host/choppah_host tests/scenarios/ui.txt OUTDIR
./build.sh --web        # the browser emulator (Emscripten)
./build.sh flash        # install over USB-MIDI (docs/FLASHING.md)
```

Clone with `--recursive` (fm1-base is a submodule in `base/`).

| Path | What |
| --- | --- |
| `app/` | The firmware: `app.c` (screens, controls, glue), `app.h`, `fm1.toml` (build and site settings) |
| `app/dsp/` | The engine, platform-free: `brk` (sequencer, generator, pads, voices), `brk_fx`, `adpcm`, `brk_synth`, `brk_bank.h` |
| `app/docs/`, `app/site/` | The manual and the site's front page |
| `tests/engine/` | Engine tests (plain cc), with WAV renders |
| `tests/scenarios/` | Host-simulator scripts: keys in, audio and screenshots out |
| `tools/prep_breaks.py` | WAV → slices → IMA ADPCM bank |
| `docs/` | [PRD](docs/PRD.md), [flashing and recovery](docs/FLASHING.md) |

## Publishing

Every push to `main` builds the firmware, the emulator and the site in GitHub Actions
([.github/workflows/pages.yml](.github/workflows/pages.yml)) and publishes it to GitHub Pages. A tag `vX.Y`
(matching `VERSION`) also attaches the firmware to a release. `scripts/publish_site.sh VERSION DEST` does the same
to any folder or server.

## Licence and credits

MPL-2.0 ([LICENSE](LICENSE)). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (Hügelton Instruments). The generator's behaviour follows the ideas of the Amen Break Generator app and BB
Gen; its code is new.

M-VAVE and FM-1 are trademarks of their owners. CHOPPAH is not affiliated with M-VAVE.
