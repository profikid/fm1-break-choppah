# CHOPPAH — Sample Break Chopper for the M-VAVE FM-1

**Product requirements document** · v2.0 · 21 Nov 2026 · Owner: Laurens (Sylverline)  
Status: revised product direction · Repository: `profikid/fm1-break-choppah`

**New feature roadmap:** [MATHCORE — Queue & Layered Sample Engine](PRD_MATHCORE.md) · v1.0 · 22 Nov 2026

---

## 1. Product definition

CHOPPAH is a **sample-only break manipulation instrument** for the M-VAVE FM-1. Load a recorded break, slice it, trigger and rearrange its slices, layer sample events, and perform rhythmic transformations live.

The instrument is about turning recorded drum breaks into new performances: jungle, drum & bass, breakcore, jazz-derived syncopation, polymeter and mathcore-style rhythmic displacement.

**The sample is the instrument. The sequencer schedules sample playback events.**

This is not a general groovebox. It does not synthesize drums, basses, leads or other voices.

### 1.1 Product principles

1. **Sample-only sound generation.** Every pitched or percussive sound is derived from loaded or built-in audio samples.
2. **Break-first workflow.** The main interaction is selecting a break, slicing it, triggering slices and mangling playback.
3. **Rhythmic freedom.** Independent sample lanes can use different lengths, subdivisions, phase offsets and cycle lengths while sharing one stable master clock.
4. **Playable live.** The panel must support immediate triggering and performance gestures without menu diving.
5. **Musical control over complexity.** Polyrhythms, polymeters and randomisation must be repeatable, editable and synchronisable—not uncontrolled randomness.
6. **FM-1 constraints first.** Keep audio stable within the AC791N's flash, RAM and render budgets. Do not promise features that cannot be sustained on the device.

### 1.2 Explicit non-goals

- No synthesizer sequencer or note/pitch sequencer.
- No synthesized kick, snare, hi-hat, percussion, bass, synth or soundboard voices.
- No X0X-style drum machine lanes.
- No oscillator, FM, subtractive, wavetable or physical-modelling engines.
- No generic MIDI note instrument mode.
- No audio recording input: the FM-1 has no audio input.
- No Wi-Fi / Ableton Link.
- No requirement for time-stretch in the first release; repitch playback is the dependable baseline.

### 1.3 What remains in scope

- Sample break playback and slicing.
- Slice trigger pads and sample-event sequencing.
- Sample-only polymetric / polyrhythmic pattern engine.
- Break generator, probability, rolls, retriggers, reverse, pitch and stutter.
- Live performance FX and sample manipulation.
- Break library, one user break upload workflow, pattern storage.
- MIDI clock synchronisation and USB audio output for recording in a DAW.

---

## 2. Core user experience

The default workflow:

1. Select a break from the library.
2. Set or detect its native BPM and choose a slice layout.
3. Play slices directly from the 16 white keys.
4. Record or program sample-trigger events.
5. Change lane length, rhythmic grouping, rate and phase to create evolving patterns.
6. Apply live break manipulation: retrigger, reverse, freeze, rolls, pitch, filter, delay and degradation.
7. Save the pattern and capture the master output over USB audio.

The instrument should feel like a playable break instrument, not a conventional drum machine with its own drum voices.

---

## 3. Hardware and platform constraints

The following are the current project constraints and must be revalidated against the checked-out fm1-base / felucca-bsp revision before implementation changes.

| Resource | Current project baseline | Design implication |
| --- | --- | --- |
| SoC | JieLi AC791N / WL82, pi32v2, second core available | One deterministic audio scheduler; optional FX work on core 2 |
| Audio | 44.1 kHz stereo output, 256-frame render blocks | Trigger events at exact sample offsets inside each block |
| App flash slot | Approximately 581 KB | Keep code and UI compact; no large embedded sample assets beyond planned bank |
| RAM | 96 KB data/BSS plus 336 KB pool | Decode only the active break(s); budget voices and FX carefully |
| Sample flash | Existing bank layout uses data flash and XDATA windows | Preserve verified bank and update safety rules |
| Display | 240 × 240 colour TFT | Show waveform, slice markers, lane phases and rhythmic relationships |
| Keys | 16 white keys and 11 black keys | White keys are slice pads; black keys are performance controls |
| MIDI | USB MIDI, TRS MIDI input, Bluetooth MIDI | MIDI clock sync; no requirement for synth-note output |
| USB audio | Master output available as USB audio input to host | Record performances in a DAW |
| Persistence | Torn-write-safe store objects | Version pattern/settings structures; migrate old saves safely |

---

## 4. Instrument and panel model

### 4.1 Controls

| Control | Sample-only role |
| --- | --- |
| White keys 1–16 | Trigger slices; in pattern edit, select/toggle sample events |
| Black keys 1–11 | Momentary or latched sample-performance actions / FX |
| PLAY | Start / stop transport |
| REC | Record sample-trigger gestures into the event loop; overdub |
| SEQ | Sample-event pattern editor |
| EDIT | Break selection, waveform, slice markers, trimming and slice mapping |
| ARP | Performance FX / sample manipulation pad page; not a synth arpeggiator |
| FX | Sample bus effect chain |
| ENV | Slice amplitude envelope / gate / transient shaping; no synthesized envelopes |
| LFO | Modulation of sample playback and FX parameters |
| GLO | Clock, MIDI, pattern, quantise and global settings |
| SEL | Shift / alternate page / latch modifier |
| OCT− / OCT+ | Sample pitch or playback speed down/up |
| SAVE | Save current sample pattern and performance settings |
| SELECT encoder | Navigate / edit value; tempo shortcut where appropriate |
| ALGORITHM encoder | Select break or sample algorithm |
| PRESETS encoder | Select pattern / performance preset |
| K1–K4 | Contextual sample, rhythm and FX parameters |
| MASTER | Master output level |

No control or page may expose a synthesized drum voice, oscillator, synth note lane or synthesized instrument preset.

### 4.2 Performance screen

HOME is the live instrument view:

- Break name, BPM, transport position, clock source and pattern.
- Break waveform with slice markers and current playhead.
- Current slice and active voice indicators.
- A compact visualisation of active sample lanes and their cycle positions.
- Black-key performance pad states.
- K1–K4 labels and values for the current performance macro set.
- White-key LEDs indicate slice triggers / current playback; they must not imply synthesized drum steps.

---

## 5. Sample engine requirements

### 5.1 Break library and playback

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| S-001 | Play a selected break as a tempo-locked loop | P0 | Stable loop timing at supported BPM range |
| S-002 | Built-in break library from the existing approved bank | P0 | Break selection is quantised to bar boundary while playing |
| S-003 | One user break slot via the existing browser break manager / SysEx workflow | P0 | Upload, verify, read back, play and delete |
| S-004 | Slice counts of 8, 16 and 32 | P0 | Slice map persists per break |
| S-005 | Equal-grid slicing and manually adjusted slice markers | P0 | Marker edits are saved and survive reboot |
| S-006 | Trigger slices from 16 white keys | P0 | Immediate while stopped; quantised when transport is running |
| S-007 | Click-free slice boundaries and retriggers | P0 | No audible clicks during rapid retrigger tests |
| S-008 | Repitch / speed playback | P0 | Predictable pitch-speed relationship |
| S-009 | Keep-length pitch mode | P1 | Pitch can change without changing loop duration, within CPU budget |
| S-010 | Reverse playback and reverse gate | P0 | Reverse starts/stops cleanly and remains clock-aligned |
| S-011 | Per-slice gain, start/end trim and envelope | P1 | Edits do not alter source sample data |
| S-012 | Transient detection in desktop preparation tool | P1 | Suggested markers can be reviewed and edited |
| S-013 | Time-stretch | P2 | Only after quality, RAM and CPU feasibility are demonstrated |

### 5.2 Sample voices

All voices play sample material. A voice can play a full break, a slice, or a sub-region of a slice.

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| S-020 | Multiple simultaneous sample voices | P0 | Voice count is configurable and bounded |
| S-021 | Voice stealing policy | P0 | Oldest released voice is stolen first; active voice is last resort |
| S-022 | Choke groups for sample voices | P1 | Triggering a voice can fade/choke a configured group |
| S-023 | Per-event slice, pitch, reverse and gate values | P1 | Event parameters are applied without changing the source pattern |
| S-024 | Voice overload protection | P0 | Shed optional FX / voices before audio underruns; expose overload state |

The implementation must not add oscillator-based fallback voices. If no valid sample is loaded, the relevant trigger is silent and the UI reports the missing sample.

### 5.3 Split breaks (added 21 Nov 2026, v1.5)

A split puts two breaks into the one decoded break buffer: bar 1 is a bar of break A, bar 2 a bar of break B. It is
one break to the engine (2 bars, A's tempo and rate, A's slices then B's), so playback, slicing, the sequencer and
the FX are unchanged. Set on HOME's third part (HOME three times, after PERFORM and the GENERATOR; GLOBAL's third
part until v1.5.1, 21 Nov 2026): SPLIT: K1 on / off, K2 break B, K3 A's bar, K4 B's bar.

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| S-030 | **Split break:** bar 1 of the split is bar 1 or 2 of break A as it is; bar 2 is bar 1 or 2 of break B, repitched (4-point Hermite, at load) to A's bar's length so it plays at A's tempo. White keys 1–8 A's slices, 9–16 B's. Turning it on or changing B or a bar reloads the break at the next bar, as a break change (S-002). Saved (settings appended: `split`, `split_b`, `split_bars`) | P1 | *Done; scenario split.txt; the loader decodes both bars into `pcm_a` (B's to the buffer's end, read forwards behind A's), no second buffer* |
| S-031 | **Half latch:** a white key pressed latches its half: from the next bar line the loop rounds that bar alone; SEL + a key, both halves again. The loop's own slices (pattern, generator, RANDOM) fold into the half; keys still play any slice. A pattern queued changes at the half's end | P1 | *Done; engine test test_split (HALF 1 three bars on steps 0..15, HALF 2 asked mid-bar waits for the bar line, COMPLEXITY stays in the half, no split ignores it); scenario split.txt* |
| S-032 | **Layered halves:** with a split, a key held in the half the looping key isn't in loops on a second voice pair of its own, on top (one key a half; within a half the last key pressed wins). It snaps as a key; let go, it plays out its window. The layer follows the tempo, HALF, REVERSE's speed, the tape stop and its key's pitch; the other pads act on the first key. Pressing the layer's key doesn't change the latch | P1 | *Done; engine test test_split (both sound, a second key per half takes it over, playing: the layer waits for its snap, then loops on over the loop); no split: the last key wins as before. Host cost: about +5 µs a block on the break engine while layering, nothing otherwise* |

| S-033 | **ALGORITHM turns the looping half's break:** with bar 2 latched (S-031) ALGORITHM (and SEL + ALGORITHM anywhere) changes break B, otherwise break A; in at the next bar as S-002, the latch kept | P1 | *Done; scenario split.txt: B latched, ALGORITHM moves B and leaves A; both halves, it moves A* |

To make room (the flash was full: 132 B free), **Bluetooth audio out (A2DP) was removed** (`bt_audio = false`,
~32 KB of flash, ~7 KB RAM and ~23 KB pool back; its settings byte kept as reserved). Bluetooth MIDI stays.

---

## 6. Sample-only rhythm engine: POLYMATH

POLYMATH is the central new feature direction. It is a multi-lane event sequencer where every lane triggers or manipulates sample playback. It does not generate sound.

### 6.1 Lane model

A lane is a stream of timed sample events. A lane may reference the active break's slices or a defined region of the same loaded sample. The first implementation should use one active break bank / decoded sample context and multiple event lanes; independent full-length sample decks are not assumed.

Each lane has:

- Enable / mute.
- Event pattern and pattern length.
- Slice or slice-selection rule.
- Clock division / multiplication.
- Rhythmic grouping and subdivision.
- Phase offset and pattern rotation.
- Swing / microtiming.
- Per-event probability and velocity.
- Retrigger / ratchet settings.
- Pitch, reverse and gate locks.
- Reset / restart behaviour.

### 6.2 Independent lengths and polymeter

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| R-001 | Independent pattern length per sample lane | P0 | Lanes can use different cycle lengths without resetting each other |
| R-002 | Support practical lengths from 1–32 events per lane | P0 | Length changes do not corrupt other lanes |
| R-003 | Independent phase offset | P0 | Lane can start at a different master-clock phase |
| R-004 | Pattern rotation | P1 | Rotate events without rewriting their relative order |
| R-005 | Per-lane clock division and multiplication | P0 | Ratios such as 1/2, 1, 2, 3 and supported tuplets remain stable |
| R-006 | Polyrhythm ratios | P0 | Support 3:2, 2:3, 3:4, 4:3, 5:4, 4:5, 7:4 and user-defined integer ratios within bounds |
| R-007 | Odd meters and asymmetric groupings | P0 | Support groupings such as 5/8, 7/8, 9/8, 7/4 and additive groupings |
| R-008 | Per-lane reset modes | P1 | Free-running, reset on bar, reset on pattern start |
| R-009 | Visual phase feedback | P0 | Screen shows each lane's progress and next event position |
| R-010 | External clock compatibility | P0 | Polymetric lanes remain phase-stable when following MIDI clock |

A 7-event cycle over a 16-step reference is a polymetric relationship, not a requirement to force the entire song into 7/8. The engine must distinguish:
- **Meter:** how beats are grouped into bars.
- **Subdivision:** how a beat or bar is divided.
- **Polymeter:** simultaneous cycles of different lengths.
- **Polyrhythm:** simultaneous rhythmic ratios such as 3 against 2.

### 6.3 Event operations

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| R-020 | Trigger a selected slice on an event | P0 | Sample-accurate event scheduling |
| R-021 | Select a fixed slice, rotate through slices or choose from a defined slice set | P0 | Selection is deterministic when probability is disabled |
| R-022 | Per-event probability | P0 | Same seed and pattern reproduce the same result |
| R-023 | Per-event velocity / level | P0 | Scales sample voice level only |
| R-024 | Ratchets and retriggers | P0 | 2×, 3×, 4×, 6×, 8× and supported tuplets |
| R-025 | Burst / roll events | P1 | Bounded event density; no scheduler overflow |
| R-026 | Conditional events / every-N-cycle triggers | P1 | Conditions repeat deterministically |
| R-027 | Skip, repeat, reverse and neighbour-slice operations | P1 | Operations apply to sample events only |
| R-028 | Event-level pitch, gate, start offset and reverse locks | P1 | Locks are saved with the pattern |
| R-029 | Deterministic random seed per pattern | P0 | Recalling a pattern reproduces the same generated sequence |
| R-030 | Live mutation amount | P1 | Mutation can be performed without losing the saved base pattern |

### 6.4 Rhythm generation tools

These are pattern generators for sample events—not sound generators.

- **Euclidean:** distribute a chosen number of sample triggers across a lane length; rotate the result.
- **Clave / Afro-Cuban templates:** provide editable event templates and accent maps for 3-2 and 2-3 son clave-inspired phrasing.
- **Odd-meter templates:** 5/8, 7/8, 9/8, 7/4 and additive groupings.
- **Break shuffle:** reorder, skip, repeat or displace slices from the source break.
- **Breakcore mutation:** bounded bursts, rapidly changing event density, slice jumps and phase displacement.
- **Mathcore phrasing:** user-defined sequences of unequal group lengths, rests, abrupt phrase-length changes and accent displacement.
- **Humanise:** controlled timing and velocity variation, constrained by the timing grid and user-set limits.

Templates are starting points, not hard-coded genre emulations. All generated events must remain editable.

### 6.5 Generator and phrase controls

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| R-040 | Complexity control | P0 | Controls slice jumps, skips and repeats; zero is unmodified playback |
| R-041 | Anchor control | P1 | Protect selected events / accents from mutation |
| R-042 | Roll control | P0 | Controls probability and density of sample retriggers |
| R-043 | Fill control | P1 | Adds bounded event variations at phrase endings |
| R-044 | Phrase lengths 1, 2, 4, 8 and 16 bars / cycles | P1 | Fill and mutation timing is repeatable |
| R-045 | One-shot randomise with undo | P0 | Randomises sample-event pattern only; undo restores prior state |
| R-046 | Freeze / lock generated pattern | P1 | Locked pattern does not mutate until unlocked |

---

## 7. Looper and pattern performance

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| P-001 | Record live slice triggers and performance gestures as events | P0 | Captured events replay in time |
| P-002 | Loop recorded events | P0 | Loop lengths include 1, 2, 4, 8 and 16 bars or supported cycles |
| P-003 | Overdub additional sample events | P0 | Existing events remain intact |
| P-004 | Undo last overdub | P1 | Restores previous event layer |
| P-005 | Commit loop to a pattern | P1 | Pattern recalls identically |
| P-006 | Pattern chain | P1 | Queue pattern changes to a defined boundary |
| P-007 | Live clear / mute / solo per lane | P0 | No unintended transport reset |
| P-008 | Pattern save and recall | P0 | Pattern includes lane lengths, phases, event locks and random seeds |

The looper records actions that trigger or manipulate samples. It does not record or sequence synthesized notes.

---

## 8. Live sample manipulation and FX

### 8.1 Performance actions

The 11 black keys are dedicated to sample and break performance. Suggested default map:

| Pad | Action | Behaviour |
| --- | --- | --- |
| 1 | Freeze | Hold a short captured region / slice |
| 2 | Stutter | Retrigger current region at a selected division |
| 3 | Roll | Repeated sample triggers at selected subdivision |
| 4 | Reverse | Reverse current sample voice while held |
| 5 | Scrub | Move through the active sample region |
| 6 | Tape stop | Slow playback to stop; release / retrigger resumes on grid |
| 7 | Slice random | Choose a random slice from the active set |
| 8 | Slice repeat | Repeat the current slice |
| 9 | Pitch drop | Momentary downward pitch / speed gesture |
| 10 | Break mute | Gate the break while clock continues |
| 11 | Fill / mutation | Apply a phrase-bounded sample-event variation |

Pads are assignable. Each action must have momentary and, where useful, latched behaviour. No pad may trigger a synthesized sound.

### 8.2 Effects

The existing sample FX chain is retained and refined. Effects process sample audio only.

- DJ low-pass / high-pass filter.
- Ring modulation as an audio effect on the sample (not an oscillator voice).
- Flanger.
- Degrader / bit reduction / sample-rate reduction.
- Drive, crunch and tape saturation.
- Delay / ping-pong delay.
- Reverb.
- EQ.
- Compressor / clipper / limiter.
- Tremolo / amplitude modulation of sample playback.

The FX chain must remain bounded and bypassable. Preserve current per-slot settings and effect ordering where practical.

### 8.3 Modulation

LFOs may modulate sample playback or effect parameters:
- Filter cutoff / resonance.
- Pitch or playback rate.
- Slice start / scrub position, with safe bounds.
- Delay time / feedback.
- Flanger depth / rate.
- Sample gate / amplitude.
- FX mix.

Modulation is control data; it is not a synthesized audio source.

---

## 9. Clock, timing and MIDI

### 9.1 Clock architecture

Use one high-resolution master transport clock and a deterministic event scheduler. Do not create independent timer threads per lane.

- Use fixed-point or integer phase accumulation.
- Schedule event boundaries at sample offsets within the 256-frame audio block.
- Each lane derives its next event time from the master clock plus its ratio, length and phase.
- Avoid cumulative floating-point drift.
- UI communicates with the audio engine through bounded event queues / snapshots.
- All event generation must have a strict upper bound per render block.

### 9.2 MIDI

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| M-001 | MIDI clock input (24 PPQN) | P0 | Stable external sync |
| M-002 | MIDI Start / Stop / Continue handling | P0 | Transport follows external host |
| M-003 | Optional MIDI clock output | P1 | Sends clock aligned to internal transport |
| M-004 | MIDI CC mapping for performance parameters | P1 | Assignable to selected sample / FX controls |
| M-005 | No MIDI note instrument mode | Explicit non-goal | MIDI notes must not trigger synthesized voices |

---

## 10. Sample preparation and break management

Keep the existing desktop preparation tool and browser break manager as the source of prepared break assets.

- WAV input with native BPM and bar count.
- Trim to musical region.
- Level normalisation with headroom.
- Resample to supported device rate.
- Equal-grid or transient-assisted slice markers.
- Encode to IMA ADPCM using the existing bank format.
- Preview the encoded / decoded result before upload.
- Upload, verify CRC, read back and delete user break.
- Preserve the existing safe commit order: data first, bank header last.

The first release continues to target the existing bank architecture. Multi-break simultaneous streaming is not assumed.

---

## 11. Architecture

### 11.1 Proposed code layout

Adapt names to the actual codebase during implementation; do not create duplicate engines if equivalent modules already exist.

```
app/
  app.c / app.h
  ui/
    home.*          performance / waveform / lane phase view
    seq.*           sample-event lane editor
    edit.*          break and slice editor
    pads.*          performance pad mapping
    fx.*             sample FX chain
    global.*         clock, MIDI and settings
  dsp/
    brk_bank.*       sample bank and slice metadata
    adpcm.*          sample decoder
    brk_voice.*      sample voices, interpolation, pitch, reverse, fades
    brk_clock.*      master clock and transport
    brk_seq.*        sample-event lanes, polymeter and scheduler
    brk_gen.*        Euclidean, templates, mutation and probability
    brk_loop.*       event looper / overdub
    brk_fx.*         sample FX chain
tools/
  prep_breaks.py
  fm1_breaks.py
tests/
  engine/
  scenarios/
```

### 11.2 Explicit removals / deprecations

The following are outside the v2 product and must not be developed further:

- Synthesized kick engine and kick presets.
- Synthesized snare and hi-hat engine and presets.
- Synth drum step lanes.
- Drum mixer controls and drum routing.
- Synthesized soundboard / one-shot sound effects.
- Synth-note sequencing, arpeggiator and melodic note lanes.
- Drum-specific MIDI note mappings.
- Any future bass or synth layer.

Existing code may be retained temporarily to keep builds working during refactoring, but it must be disabled from the user-facing product and removed in a deliberate cleanup after dependencies are understood. Do not delete code blindly before checking build, settings migration and memory effects.

### 11.3 Memory and performance

- Keep the current decoded-break buffer strategy unless measurements justify a change.
- Budget sample voices and delay/reverb buffers against the 336 KB pool.
- Avoid decoding a second full break unless the buffer budget supports it.
- Bound maximum events per lane and maximum events per audio block.
- Keep the audio scheduler allocation-free.
- Keep DSP tests platform-independent where possible.
- Preserve overload shedding: optional FX quality / extra voices are reduced before audio stability is compromised.

### 11.4 Persistence and migration

- Introduce a new settings / pattern schema version for sample-only patterns.
- Preserve break bank compatibility unless a format change is necessary.
- Old patterns containing synthesized drum lanes must not silently map those lanes to sample triggers.
- On loading an old save, retain compatible break, tempo, FX and sample settings; ignore deprecated drum/synth fields and initialise new sample lanes to safe defaults.
- Provide a clear migration / reset path and do not overwrite old data until the new save has been verified.

---

## 12. UI / interaction requirements

### 12.1 SEQ page

SEQ edits sample-event lanes only.

- Lane selector.
- Event grid with variable lane length.
- Current lane length, ratio, phase and subdivision.
- Event slice assignment / slice rule.
- Probability, velocity, ratchet and event locks.
- Lane mute / solo.
- Visual indication of master beat and lane cycle.
- A clear distinction between bar position and lane cycle position.

### 12.2 Rhythm editor

A dedicated POLYMATH view should make the relationship between cycles visible:

- Each lane is a horizontal ring / timeline with its own number of events.
- A shared master phase indicator.
- Current event and next event.
- Lane length and ratio labels.
- Accent and probability indicators.
- A way to rotate / offset a lane.
- A compact mode for live use; detailed edit mode for setup.

Do not use a static 16-step grid as the underlying timing model. A 16-key grid can be a convenient editor, but the engine must support variable-length event sequences.

### 12.3 Help and feedback

- Every page has concise help cards.
- Show clock source and sync state.
- Show overload and missing-sample warnings.
- Clearly show when a pattern is mutated but not saved.
- Never imply that a silent / empty sample lane is a synthesized drum voice.

---

## 13. Non-functional requirements

| ID | Requirement | Target |
| --- | --- | --- |
| N-001 | Audio stability | Zero xruns in a 1-hour soak at supported maximum voice / FX configuration |
| N-002 | Timing | Sample-accurate event placement within each render block |
| N-003 | Trigger latency | Under 10 ms for free-play pad triggers |
| N-004 | Determinism | Same pattern, seed and clock produce the same generated events |
| N-005 | Bounded work | No unbounded event generation or memory allocation in audio callback |
| N-006 | Boot | Reach playable state in under 2 seconds |
| N-007 | Regression | Engine tests and host scenarios run in CI |
| N-008 | Recovery | Existing safe update / stock restore workflow remains documented |
| N-009 | UI clarity | No synth, drum-machine or melodic-sequencer controls remain in the user-facing flow |

---

## 14. Milestones

| Milestone | Scope | Exit criteria |
| --- | --- | --- |
| M0 — Product cleanup | Remove synth drum UI from the active product flow; document deprecated code | No synth drum lanes or voices exposed |
| M1 — Sample player | Break library, playback, slices, pads, pitch, reverse and fades | Sample playback and slice acceptance tests pass |
| M2 — Sample sequencer | Variable-length sample-event lanes, sample-accurate scheduler | Different lane lengths run stably against one clock |
| M3 — POLYMATH core | Ratios, phase, odd groupings, polymeter, visual phase | 3:2, 5:4, 7-event and 16-event cycles stay aligned to master clock |
| M4 — Generators | Euclidean, clave templates, odd-meter templates, break mutation | Generated patterns are editable and deterministic |
| M5 — Performance | Sample-only pads, stutter, rolls, freeze, scrub, tape stop and fills | All performance actions work without audio glitches |
| M6 — FX and modulation | Sample FX chain and modulation refinement | CPU / memory budgets and soak tests pass |
| M7 — Looper and MIDI | Event recording, overdub, pattern chain, external sync | Reproducible loop and MIDI clock scenarios pass |
| M8 — Release | Manual, help, emulator, web installer, recovery and soak test | All P0 requirements and non-functional tests pass |

---

## 15. Acceptance demo: BREAK INTERFERENCE

A release candidate must demonstrate a sample-only performance:

1. Load one break and set its BPM.
2. Slice it into 16 regions.
3. Trigger slices live from the white keys.
4. Create four sample-event lanes with different cycle lengths (for example 16, 7, 14 and 15 events).
5. Set one lane to a 3:2 relationship and another to a 5:4 subdivision.
6. Let the lanes run simultaneously for at least 64 bars without timing drift or unwanted resets.
7. Apply stutter, reverse, roll, freeze and a phrase fill.
8. Record live gestures into the event looper and overdub.
9. Save, reboot and recall the pattern.
10. Record the master output over USB audio.

**Pass condition:** every audible event comes from sample playback; all lanes remain synchronised to the master clock; no synthesized drum or synth voice is required.

---

## 16. References

- Existing CHOPPAH implementation and history: https://github.com/profikid/fm1-break-choppah
- Platform: https://github.com/charlesvestal/fm1-base
- Board support: https://github.com/hugelton/felucca-bsp
- FM-1 X0X reference (study only; do not inherit synth/drum sequencing scope): https://github.com/charlesvestal/fm1-x0x
- M-VAVE FM-1 Open SDK: https://github.com/m-vave/fm1_open
- FM-1 reverse engineering: https://github.com/AL-255/FM-1-RE
