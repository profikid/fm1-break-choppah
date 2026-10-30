# CHOPPAH: an Amen chopper for the M-VAVE FM-1

Dedicated FM-1 firmware for chopping and mangling breakbeats: slices on the white keys, momentary FX pads on the
black keys, a generator that rearranges the break as it plays, and ring mod, hi-pass, flanger and degrader on the
way out. Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0).

**Status:** M1 engine in progress. The engine runs and is tested on a computer; the firmware hasn't been built
or flashed yet. See [docs/PRD.md](docs/PRD.md) for the full feature set and milestones.

## Panel (M1)

| Control | Does |
| --- | --- |
| White keys 1–16 | Slices: quantised to the next 16th while playing, immediate while stopped; hold to loop |
| Black keys 1–11 | Pads, while held: ROLL 1/8 · 1/16 · 1/32 (with 1/8 or 1/16: triplets) · REVERSE · FREEZE · HALF · TAPE STOP · GATE · RANDOM · FILTER THROW · DUB (M4) |
| PLAY | Play / stop · HOME + PLAY: back to bar 1 |
| SELECT | Tempo (SEL held: 0.1 BPM) · tap SEL 4× on PERFORM: tap tempo |
| OCT− / OCT+ | Pitch in semitones (hold: faster) · both: 0 |
| HOME | PERFORM page: K1 DJ filter · K2 swing · K3 complexity · K4 crunch |
| ARP | GENERATOR page: complexity · anchor · roll · fill · SEL + ARP: randomise the pattern |
| FX | FX page: ALGORITHM picks the effect, K1–K4 its settings, FX again turns it on/off |
| SAVE | Keep the settings |

## Layout

```
app/            the firmware: app.c (UI, glue), app.h, fm1.toml (build settings)
app/dsp/        the engine, platform-free: brk (sequencer, generator, pads, voices), brk_fx, adpcm, brk_synth, brk_bank.h
tools/          prep_breaks.py: WAV -> slices -> IMA ADPCM bank image
tests/engine/   engine tests with plain cc; WAV renders in build/engine/
scripts/        bootstrap.sh: fm1-base, JieLi toolchain, SDK files, Python packages
docs/           PRD.md, FLASHING.md
base/           fm1-base (git submodule, added by bootstrap.sh)
```

## Getting started

```
scripts/bootstrap.sh    # once: fm1-base, toolchain, SDK files, Python packages (macOS needs Docker or OrbStack)
./build.sh test         # the engine's tests, no toolchain needed
./build.sh              # app/build/choppah.fwsc
./build.sh info         # what the FM-1 is running now
./build.sh flash        # install it: read docs/FLASHING.md first
```

Your own breaks (upload arrives in M5):

```
python3 tools/prep_breaks.py amen.wav:136.9:4 think.wav --bars 2 --transients --verify -o build/bank.bin
```

## Licence

MPL-2.0 (PRD Q3). Recorded breaks belong to whoever made them; the firmware ships only a synthesised break.
M-VAVE and FM-1 are trademarks of their owners; this project isn't affiliated with M-VAVE.
