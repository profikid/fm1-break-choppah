# CHOPPAH — Amen Chopper firmware for the M-VAVE FM-1

**Product requirements document** · v0.1 · 30 Oct 2026 · Owner: Laurens (Sylverline)
Status: draft for review · Working name: CHOPPAH (repo `fm1-break-choppah`) · Repo docs: `docs/PRD.md`

---

## 1. Summary

CHOPPAH turns the M-VAVE FM-1 into a dedicated breakbeat chopper: load the Amen (or any break), cut it
into slices, and play, sequence, randomise and mangle it live with remix effects and DSP. It has to match
everything the **Amen Break Generator** app does (§4), then go further with a **Performance mode** (§5)
built for jungle / drum & bass / breakcore sets: momentary FX on the black keys, rolls and stutters,
tape stops, scratch scrub, per-step locks and pattern chaining.

The firmware is built on **fm1-base** (MPL-2.0) and **felucca-bsp**: the same platform as AMB-1, LOOM-8
and CTL-1. It has a host simulator and a browser emulator, and installs through the USB update path with
a safe mode and a route back to stock firmware.

### 1.1 Goals

1. **Parity:** every feature of Amen Break Generator (Revived), mapped onto the FM-1 panel.
2. **Performance:** a hands-on chop-and-FX mode that's playable live without looking at the screen.
3. **Sound:** a crunchy, 90s-sampler character (repitch, not time-stretch, by default) with clean
   headroom: no clicks at slice edges and no dropouts.
4. **Your own breaks:** a desktop tool and web uploader that slice, encode and send up to 8 breaks.
5. **Safe to install:** never brick the device; always a way back to stock.

### 1.2 Non-goals (v1)

- A general-purpose sampler or multitrack groovebox (X0X, LOOM-8 and zp12 already do that).
- Synth engines (FM, 303, reese). A bass layer is listed under Future (§12).
- Bluetooth or Wi-Fi. The radio stays off, which frees RAM and flash for audio.
- Recording audio from a microphone. The FM-1 has no audio input.

### 1.3 Target users

- **Laurens as DJ / live performer:** jungle and DnB sets, B2B with CDJs, needs MIDI clock sync.
- **Beat-makers:** want a pocket Amen chopper to sketch patterns and record them into a DAW over USB.
- **The FM-1 community:** installs from a web page; reads the manual.

---

## 2. Hardware and platform constraints

| Item | Value | Source / implication |
| --- | --- | --- |
| SoC | JieLi AC791N (WL82), pi32v2 core, single-precision FPU, **second core available** | fm1-base `plat_cpu2_*`; FX chain can move to core 2 |
| Audio | 44.1 kHz, stereo, 24-bit render, 256-frame blocks (5.8 ms) | `app_render(lr, n, master_q12)` |
| App flash slot | 581 KB | code + built-in assets |
| RAM | 96 KB `.data`/`.bss` + **336 KB pool** for big buffers | PCM decode buffers live in the pool (§8.2) |
| Sample flash | Data area 0x97000–0xDFFFF (292 KB, shared with store objects), XDATA0 16 KB, XDATA1 92 KB → **≈ 356 KB for the bank** (0x97000–0xD9FFF + XDATA1, §8.2) | Sector bytes 0xF00–0xF07 must stay erased in XDATA windows |
| Screen | 240 × 240 colour TFT, ~60 fps UI frame | |
| Keys | 27 silicone keys F3–G5: **16 white, 11 black**, each with an LED; velocity and pressure via `APP_TOUCH` (experimental) | 16 white = 16 slices/steps; 11 black = FX pads |
| Buttons (14) | FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY (red/green), REC, OCT−, OCT+ | |
| Encoders (7) | SELECT, ALGORITHM, PRESETS, K1–K4 (detented, endless) | |
| Pot | MASTER volume | |
| MIDI | USB-MIDI in/out, TRS MIDI **in only** | clock out over USB only |
| USB audio | FM-1 as a USB audio input (the master output, recordable in any DAW) | covers "record to WAV" (§4.6) |
| Store | Torn-write-safe objects of ≤ 3840 bytes each (A/B sectors) | patterns split across objects |
| Update | USB-MIDI SysEx OTA; single bank; fm1-base safe mode after two failed boots; stock restore from the web installer | §10 |

---

## 3. Panel map (overview)

The panel works in two layers. **Keys** are always the instrument; **buttons** pick what K1–K4 edit.

| Control | Default role |
| --- | --- |
| **White keys 1–16** | Slices 1–16 (live trigger, quantised to the step grid while playing; immediate while stopped); in SEQ, the 16 steps |
| **Black keys 1–11** | Performance FX pads (momentary; §5.1) |
| **PLAY** | Play / pause (green playing, red when the looper records) |
| **HOME** | Main screen; **hold HOME + PLAY** = reset to bar 1; **HOME + SELECT** = tempo |
| **REC** | Looper: record → loop → overdub → loop (§4.3); hold = clear |
| **SEQ** | Step sequencer edit |
| **ARP** | Generator (the randomising algorithm, §4.2) |
| **EDIT** | Break / slice editor |
| **FX** | DSP effects page (§4.5) |
| **ENV** | Slice envelope and gate |
| **LFO** | Modulation (filter / flanger / ring-mod rate, tempo-synced) |
| **GLO** | Global: MIDI, clock, swing, master bus, settings |
| **SEL** | Shift: second page of the current screen; with a key, alternative functions |
| **OCT− / OCT+** | Pitch/speed down / up (hold to accelerate); both = reset to 0 |
| **SAVE** | Save pattern and kit; hold = save as / revert |
| **SELECT enc** | Scroll / value; with HOME held, tempo |
| **ALGORITHM enc** | Break select (1–8 slots + built-ins) |
| **PRESETS enc** | Pattern select (queued to the next bar) |
| **K1–K4** | Parameters of the current page (labels on screen) |
| **MASTER** | Volume |

**Hold any button for 1 s** for a help card (fm1-base pattern), so the panel explains itself.

---

## 4. Feature set A: Amen Break Generator parity

Each requirement has an ID, priority (**P0** must ship in v1.0, **P1** should, **P2** nice to have) and
acceptance criteria.

### 4.1 Break playback and slice triggering

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-1 | Play a break as a tempo-locked loop. Default repitch mode: playback rate = target BPM / break's native BPM | P0 | Amen at 136.9 native plays in time at 60–220 BPM; ±1 sample drift per 64 bars max |
| A-2 | Break library: **8 slots** (user-loaded) + at least 1 built-in break that ships legally (§11 Q1) | P0 | ALGORITHM switches breaks, quantised to the next bar |
| A-3 | **16 slice trigger keys** (white keys), synced to tempo: a press plays that slice at the next step (1/16 grid) | P0 | Max quantise latency = 1 step; trigger jitter < 1 ms |
| A-4 | **Free-play mode:** while stopped, keys play slices immediately (unquantised) | P0 | Key-to-sound < 10 ms |
| A-5 | Slice count per break: 8, 16 (default) or 32; equal grid or transient-detected markers from the prep tool | P0 | Markers survive reboot |
| A-6 | Click-free slice edges: 1–3 ms fades at slice start/stop and on every cut | P0 | No audible clicks on rapid retriggers at 1/32 |
| A-7 | Reload the last break, pattern, tempo and settings at power-on | P0 | State identical after power cycle |
| A-8 | Velocity from keys scales slice volume (`APP_TOUCH`) | P2 | Off by default; GLO toggle |

### 4.2 Randomising algorithm (generator)

Based on the behaviour of BB Gen / X0X's break part and ABG's "randomizing algorithm".

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-10 | **Generator** (ARP page) rearranges slices as the loop plays. Parameters: **COMPLEXITY** (chance of jumping instead of playing the next slice), **ANCHOR** (keep kick/snare slices on 1 and 3), **ROLL** (repeat or neighbour-slice chance), **FILL** (rule-breaking in the last bar of a phrase) | P0 | COMPLEXITY 0 = straight loop, bit-exact with plain playback |
| A-11 | **RANDOMIZE** one-shot: SEL+ARP writes a new random pattern into the current sequence | P0 | Undo with a second SEL+ARP within 4 s |
| A-12 | Phrase length 2/4/8/16 bars (fills land on the phrase's last bar) | P1 | |
| A-13 | Deterministic seed per pattern (same seed → same variations), and a "lock" so a pattern you like can be recalled | P1 | |
| A-14 | Retrigger chances per bar: 2×, 3×, 4×, 8× | P1 | |

### 4.3 Looper

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-20 | **Looper repeats the last 16 steps** of what was played (keys, generator and FX pad moves, as events, not audio) | P0 | REC tap: the next 16 steps loop exactly |
| A-21 | **Overdub:** REC again adds new key presses on top of the loop | P0 | |
| A-22 | Loop length 4, 8, 16, 32 or 64 steps (GLO) | P1 | |
| A-23 | Commit loop to the current pattern (SAVE while looping) | P1 | Pattern replays the loop identically |
| A-24 | Undo last overdub layer | P2 | |

### 4.4 Live remix effects (transport-locked)

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-30 | **FREEZE:** hold the current slice (or 1/8, 1/16 loop of it) while transport keeps counting; release rejoins where the loop would be | P0 | Rejoin lands on the correct step |
| A-31 | **Step randomisation:** each step plays a random slice while held | P0 | |
| A-32 | **Auto-retrigger:** repeat the current slice at a rate (1/8, 1/16, 1/32, triplets) while held | P0 | |
| A-33 | **Slice re-trigger:** re-fire the slice playing now from its start | P0 | |
| A-34 | **RMX mode:** generator on/off as a latch; the playhead follows the loop's own position, so dropping out returns in phase | P0 | |
| A-35 | **REVERSE as a gate:** reverse playback while held (not a toggle) | P0 | |
| A-36 | **Pitch / speed ±** on OCT−/OCT+: semitone steps; holding accelerates; both buttons reset | P0 | ±24 semitones; fine mode with SEL (±100 cents) |
| A-37 | **Pause** (PLAY) and **reset** (HOME+PLAY) | P0 | |
| A-38 | **BPM display doubles as tap tempo** (tap SEL four times on HOME, or a dedicated black key in GLO) | P0 | Tempo within ±0.5 BPM after 4 taps |

### 4.5 DSP effects

ABG's four, plus the master section a live unit needs.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-40 | **Ring modulator:** frequency (tempo-synced or free, 20 Hz–2 kHz), mix | P0 | |
| A-41 | **Stereo hi-pass filter:** cutoff, resonance; stereo spread offset between L and R | P0 | No zipper noise on knob turns |
| A-42 | **Flanger:** rate (sync/free), depth, feedback (±), mix | P0 | |
| A-43 | **Degrader:** bit depth (16→2), sample-rate reduction (44.1k→2k), drive | P0 | |
| A-44 | FX page: one FX focused at a time on K1–K4; each FX on/off by SEL+K1..K4 press; fixed order ring → HPF → flanger → degrader | P0 | Total DSP < 60 % of the render budget, all FX on |
| A-45 | **Master bus:** DJ filter (one knob LP↔HP), compressor with drive, brickwall limiter | P1 | No output clipping at max settings |
| A-46 | Send FX: tape/dub delay (tempo-synced, feedback, filter in loop) and small reverb | P1 | |

### 4.6 Recording and export

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-50 | Record the performance: the FM-1 is a **USB audio input**, so any DAW or recorder captures the master output | P0 | Works class-compliant on macOS, Windows, iOS |
| A-51 | Recording starts in sync with the loop: a USB MIDI Start goes out on PLAY (DAWs can arm on Start) | P1 | |
| A-52 | **Resample:** bounce 1–2 bars of the master output into a new break slot (flash) | P2 | Bounce encodes to ADPCM in < 3 s |

### 4.7 Custom loops

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-60 | Import up to **8 custom loops** | P0 | |
| A-61 | **Prep tool** (`tools/prep_breaks.py`, Python stdlib only): WAV in → trim to whole bars (by BPM or detected) → slice markers (grid or transient) → resample → IMA ADPCM → bank image + manifest | P0 | Amen 2 bars fits in < 45 KB |
| A-62 | **Upload** over USB: CLI (`tools/fm1_breaks.py upload`) and a web page (Chrome/Edge, Web MIDI SysEx) | P0 | A full bank uploads in < 60 s; a failed upload leaves the old bank intact (A/B or verify-then-commit) |
| A-63 | On-device slice editor (EDIT): move markers, nudge in 1 ms, set slice count, set native BPM, rename | P1 | |
| A-64 | Bank capacity shown before upload ("what fits"), including `--dry-run` without the FM-1 | P0 | |

### 4.8 MIDI control

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-70 | Transport: Start/Stop/Continue in; **MIDI clock in** (USB and TRS) with tempo follow; clock out over USB | P0 | Locks to CDJ/DAW clock in < 2 beats; follows tempo changes |
| A-71 | Slice triggering by notes (default C2 = slice 1, chromatic up) | P0 | |
| A-72 | Break select by notes (separate octave) and Program Change; pattern select by Program Change | P1 | |
| A-73 | CC map for K1–K4 and all FX parameters; CC out on knob turns | P1 | Map documented in the manual |
| A-74 | MIDI channel selectable (GLO) | P0 | |

### 4.9 Help and interface

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| A-80 | Hold-for-help cards on every button | P1 | |
| A-81 | Manual (docs/MANUAL.md → site) | P1 | |
| A-82 | Screen-reader words (fm1-reader) | P2 | |

---

## 5. Feature set B: Performance mode

Performance mode is the default **HOME** screen with the keys set for playing live. The aim: two hands
on the keys, eyes on the crowd.

### 5.1 Black-key FX pads (momentary, combinable)

| Key | Pad | What it does while held | Pri |
| --- | --- | --- | --- |
| B1 | **ROLL 1/8** | Beat-repeat the current position at 1/8 | P0 |
| B2 | **ROLL 1/16** | ... at 1/16 | P0 |
| B3 | **ROLL 1/32** | ... at 1/32 (with B1/B2: 1/8T, 1/16T) | P0 |
| B4 | **REVERSE** | Reverse gate (A-35) | P0 |
| B5 | **FREEZE** | A-30 | P0 |
| B6 | **HALF** | Half-speed (one octave down, tape style) | P0 |
| B7 | **TAPE STOP** | Pitch-and-speed ramp down over 1/4–2 beats; release spins back up | P0 |
| B8 | **STUTTER GATE** | Trance gate at 1/16 with swing | P1 |
| B9 | **RANDOM** | Step randomisation (A-31) | P0 |
| B10 | **FILTER THROW** | Sweeps the DJ filter to LP (SEL: HP) and back on release | P1 |
| B11 | **DUB THROW** | Sends the current step into the dub delay at 100 % | P1 |

- Pads combine (ROLL + REVERSE = reversed roll).
- Pad assignments are configurable in GLO (P2).
- **SEL + black key** latches a pad until pressed again (P1).

### 5.2 Live chopping

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| B-10 | **Hold a slice key = loop it** at the step grid until released; release rejoins in phase | P0 | |
| B-11 | **Two slice keys held = ping-pong** between them each step | P1 | |
| B-12 | **Scratch scrub:** turning SELECT on HOME scrubs the playhead (vinyl-style), with momentum; release returns to the loop | P1 | Scrub audio is continuous (no gaps) |
| B-13 | **Choke / mute group:** slices cut each other (mono) or overlap (poly, 4 voices) | P0 | Default mono, like a classic sampler |
| B-14 | **Swing** 50–75 % on the 1/16 grid | P0 | |
| B-15 | **Double-time / half-time** of the sequencer (not the pitch) | P1 | |
| B-16 | **Slice pitch on the fly:** hold a slice key and turn K1 to repitch that slice ±12 | P1 | |

### 5.3 Step sequencer with locks

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| B-20 | **16 patterns**, each 16–64 steps (pages of 16, SEL+white key to pick a page) | P0 | |
| B-21 | Per step: slice (or "follow loop"), on/off, reverse, pitch (±12), ratchet (1–4), gate length, probability (0–100 %), FX lock (one FX on/off mask) | P0 | |
| B-22 | **Record live:** in SEQ with PLAY + REC, key presses record into steps (quantised) | P0 | |
| B-23 | Pattern change quantised to the bar; chain patterns (song mode, up to 32 entries) | P1 | |
| B-24 | Copy/paste step, page, pattern (SEL+SAVE / SEL+key) | P1 | |
| B-25 | Knob motion recording (one lane per FX param, P-lock style) | P2 | |

### 5.4 Performance screen (HOME)

- Top bar: break name · BPM (tap to set) · bar.beat · pattern · clock source icon.
- Middle: waveform of the break with slice markers, the playing slice highlighted, playhead.
- Pad strip: 11 small boxes lit when a black-key pad is active.
- Bottom: K1–K4 labels and values: default **FILTER · PITCH · COMPLEXITY · FX MIX**.
- Key LEDs: white key of the playing slice lit; held pads lit; steps lit in SEQ.

---

## 6. Modes and pages

| Page (button) | K1 | K2 | K3 | K4 | SEL page |
| --- | --- | --- | --- | --- | --- |
| HOME (perform) | Filter | Pitch | Complexity | FX mix | Swing · Volume · Tempo · Generator on/off |
| SEQ | Step slice | Step pitch | Ratchet | Probability | Gate · Reverse · FX lock · Page length |
| ARP (generator) | Complexity | Anchor | Roll | Fill | Retrig 2× · 3× · 4× · 8× |
| EDIT (break) | Slice select | Start nudge | Slice count | Native BPM | Gain · Trim · Name · Mode (repitch/stretch) |
| FX | Focused FX param 1 | param 2 | param 3 | Mix | Next FX · on/off |
| ENV | Attack | Decay | Gate length | Choke (mono/poly) | Fade length |
| LFO | Rate (sync) | Depth | Target | Shape | Retrigger on bar |
| GLO | MIDI channel | Clock source | Clock out | Looper length | Pad map · Velocity · Brightness |

---

## 7. Sound engine requirements

| ID | Requirement | Pri |
| --- | --- | --- |
| E-1 | Playback interpolation: 4-point Hermite (P0); linear as a "crunch" option (P1) | P0 |
| E-2 | Voices: 1 mono (default) or up to 4 poly with choke groups | P0 |
| E-3 | Sample-accurate step scheduling inside the 256-frame block (no 1 ms tick jitter) | P0 |
| E-4 | Fades on every start/stop/cut (A-6) | P0 |
| E-5 | Time-stretch mode (granular, tempo without pitch change) per break | P2 |
| E-6 | Character: optional 12-bit / 26 kHz "SP-1200" output stage, separate from the Degrader | P2 |
| E-7 | Overload guard: if the render exceeds 85 %, drop poly voices first, then FX quality, never audio (`APP_OVERLOAD`) | P0 |
| E-8 | FX chain can run on the second core (`plat_cpu2_run`) with fallback to one core | P1 |

---

## 8. Architecture

### 8.1 Code layout

```
app/
  app.h, app.c      platform contract (init, render, frame, panic, store map)
  ui.c              screens, pages, help cards
  project.c         patterns, settings, persistence
  dsp/
    brk_bank.[ch]   sample bank format: header, slots, slice tables, IMA ADPCM data in flash
    adpcm.[ch]      IMA ADPCM decoder (into pool RAM)
    brk_voice.[ch]  slice voices: Hermite resampler, fades, reverse, pitch
    brk_seq.[ch]    clock, steps, quantise, swing, looper, pattern chain
    brk_gen.[ch]    the generator (complexity, anchor, roll, fill, retrig)
    brk_fx.[ch]     ring mod, stereo HPF, flanger, degrader, DJ filter, comp/limiter, delay
tools/
  prep_breaks.py    WAV → slices → ADPCM → bank image
  fm1_breaks.py     upload / dump / dry-run over USB-MIDI
tests/
  engine/           host unit tests for dsp/ (plain gcc, no hardware)
  scenarios/        fm1-base host-simulator scripts (keys in → audio + screenshots out)
```

The engine in `app/dsp/` has **no platform dependencies**, so it builds and is tested with plain `gcc`
on any computer, and in the browser emulator.

### 8.2 Memory plan

| Region | Use | Budget |
| --- | --- | --- |
| Flash: app slot (581 KB) | code, UI assets, 1 built-in break | ≤ 450 KB (headroom) |
| Flash: 0x97000–0xD9FFF + XDATA1 0xE5000–0xFBFFF | break bank (8 slots) | ≤ 356 KB |
| Flash: 0xDA000, 0xDC000, 0xFC000 | store objects: patterns, panel calibration, settings | 2 sectors each |
| Pool RAM (336 KB) | screen canvas 240 × 96 (45 KB, drawn in strips); **active break decoded to int16 PCM**: 2 × 112 KB (current + next/B-loop, so switching is instant); FX delay line 40 KB; scratch 8 KB | ≤ 320 KB |
| `.data/.bss` (96 KB) | engine state, patterns in RAM, UI | ≤ 64 KB |

**Why decode to RAM:** ADPCM can't be read backwards or from any sample, while scrub, reverse, roll and
freeze all need random access. A 112 KB buffer holds 2.6 s of 22.05 kHz mono (1.5 bars of the Amen at
its native tempo) or 1.3 s at 44.1 kHz; a 2-bar break at 22.05 kHz needs 154 KB, so long breaks take both
buffers (no instant switching for those). M1 uses one 128 KB buffer. The prep tool chooses the rate per break to fit; 22.05 kHz is the
default (it's also the jungle sound).

**Flash budget example:** IMA ADPCM at 22.05 kHz = 11 KB/s → 360 KB ≈ 33 s of breaks ≈ 8 × 2-bar breaks.

### 8.3 Bank format (v1)

Defined in `app/dsp/brk_bank.h` and written by `tools/prep_breaks.py`: a 16-byte header (`CHB1`, version,
slot count, data size, CRC-32 of everything after it), a slot table of 168-byte entries (name, BPM ×100,
rate, length, data offset and size, bars, slice count, 33 slice offsets), then each slot's IMA ADPCM. A break
decodes whole into RAM from its start, so no per-slice decoder state is stored. Written verify-then-commit:
data first, header last, so a cut upload leaves the old header valid or nothing (A-62).

### 8.4 Persistence (store objects)

| Object | Content | Size |
| --- | --- | --- |
| OBJ_SETTINGS (0xFC000) | globals, MIDI, last break/pattern/tempo, FX state | < 1 KB |
| OBJ_PAT0..3 | 16 patterns × 64 steps × 4 bytes, split over 4 objects | 4 × ~1 KB (fits 3840 B max) |
| OBJ_SONG | chain | < 256 B |
| OBJ_PANEL (0xDC000) | panel calibration (platform's) | |

### 8.5 Timing

- Clock: internal BPM or MIDI clock (24 ppqn), smoothed; position in samples (double-free: 32.32 fixed point).
- Each render block computes the step boundaries that fall inside it and starts voices at the exact frame.
- UI runs in `app_frame` (~60 Hz); UI → engine through a lock-free event queue; engine → UI through a
  snapshot struct (playhead, slice, levels).

---

## 9. Non-functional requirements

| ID | Requirement | Target |
| --- | --- | --- |
| N-1 | No audio dropouts | 0 xruns in a 1-hour soak with all FX and 4 voices (`plat_xruns`) |
| N-2 | Render load | < 70 % worst case on one core (`plat_cpu_pct`) |
| N-3 | Latency key → sound | < 10 ms free-play; quantised: next step |
| N-4 | Boot to playable | < 2 s |
| N-5 | Flash writes never interrupt playback unexpectedly | saves only on SAVE / stop; show a "saving" note (writes stall audio briefly) |
| N-6 | Tests | engine unit tests + host scenarios run on every change (CI on GitHub Actions) |
| N-7 | Licence | MPL-2.0 (pending §11 Q3) |
| N-8 | Radio | never enabled (`ble = false`) |

---

## 10. Install, update and recovery

- **Install:** `python3 base/tools/fm1_install.py app/build/choppah.fwsc` over USB-MIDI, or the generated
  web installer page (Chrome/Edge). Update mode on the FM-1: hold **OCT− + OCT+ for 5 s**.
- **Rollback:** the web installer can put M-VAVE's own firmware back. Keep a stock firmware file locally
  before the first flash.
- **Safe mode:** fm1-base starts in safe mode after two failed boots, and the installer can still reach it.
- **Last resort:** if the chip's own update mode shows up (USB 4C4A:8057), `base/bsp/tools/fm1_rescue.sh`
  restores stock firmware from a Mac.
- The single-bank flash has no proven recovery for an interrupted write: never unplug during an update,
  and flash with a charged battery.

---

## 11. Open questions

| # | Question | Default if not decided |
| --- | --- | --- |
| Q1 | **Built-in breaks.** The Amen and other recorded breaks are copyrighted recordings; X0X ships none. Ship user-loaded only, plus a synthesised "Amen-like" break played by an internal kit? | Ship a synthesised break; users load their own recordings |
| Q2 | Name: CHOPPAH / BREAK CHOPPAH / AMEN-1? | CHOPPAH |
| Q3 | Licence: MPL-2.0 (like fm1-base) or GPL-3.0 (needed if we port GPL code such as X0X's BB Gen port)? | MPL-2.0, write the generator from scratch |
| Q4 | Default sample rate 22.05 kHz (more breaks, crunchier) or 32 kHz (cleaner)? | 22.05 kHz, per-break override |
| Q5 | Package identity `id_prefix`: needs a unique FM-1_ number | `FM-1_86` (check the community list before release) |
| Q6 | Theme and splash (e.g. Disco in Paradise branding)? | Black / amber |

---

## 12. Future (post v1)

- Bass layer: sub / reese with 16-step note lane, sidechained to the kick slices.
- Time-stretch mode (E-5) and slice-level timestretch.
- Two-deck mode: two breaks crossfaded on K4, DJ-style.
- Ableton Link (not possible without Wi-Fi: out unless radio policy changes).
- Kit mode: one-shots on the black keys (stabs, hoovers) alongside the break.

---

## 13. Milestones

| Milestone | Scope | Exit criteria |
| --- | --- | --- |
| **M0 Pipeline** | fm1-base bootstrapped, app builds, flashes, restores to stock | App boots on the FM-1 showing CHOPPAH; stock restore tested once |
| **M1 Break player** | Bank format, ADPCM decode to pool, 1 built-in break, 16 slices on white keys, play/stop, tempo, repitch, fades | A-1, A-3, A-4, A-6, A-37 pass on device. *Built and simulator-tested 30 Oct 2026 (image 352 KB, pool 210 KB): engine, all pads, 4 FX + DJ filter + limiter, plus an 8-break Amen bank built into the image (Cyberworm's royalty-free cw_amen loops from archive.org, 22.05 kHz ADPCM, 16 slices each) chosen on ALGORITHM. Flashed to the FM-1 30 Oct 2026 (FM-1_8600); on-device acceptance checks still to run.* |
| **M2 Sequencer + generator** | Steps, swing, generator, randomize, tap tempo, patterns, persistence | A-7, A-10, A-11, A-38, B-14, B-20, B-21 |
| **M3 Remix FX + looper** | Freeze, reverse gate, retrigger, RMX, step random, pitch ±, looper with overdub | §4.3, §4.4 P0 rows |
| **M4 DSP** | Ring mod, stereo HPF, flanger, degrader, master DJ filter + limiter; overload guard | §4.5 P0 rows, N-1, N-2 |
| **M5 Custom breaks** | prep tool, CLI + web upload, 8 slots, on-device slice editor | §4.7 |
| **M6 Performance mode** | Black-key pads, hold-to-loop, scrub, tape stop, p-locks, chains | §5 P0 rows |
| **M7 MIDI + sync** | Clock in/out, note/CC maps, USB audio recording flow | §4.8 |
| **M8 Release 1.0** | Manual, help cards, emulator, web installer, soak test | All P0 + N-1..N-6 |

---

## 14. References

- Amen Break Generator (Revived), App Store listing: feature reference for §4
- fm1-base: https://github.com/charlesvestal/fm1-base (platform, MPL-2.0)
- felucca-bsp: https://github.com/hugelton/felucca-bsp
- X0X for the FM-1 (break part based on BB Gen): https://github.com/charlesvestal/fm1-x0x
- M-VAVE FM1 Open (official SDK, V14): https://github.com/m-vave/fm1_open
- FM-1 reverse engineering: https://github.com/AL-255/FM-1-RE
- Awesome FM-1: https://github.com/cicloid/awesome-fm-1
