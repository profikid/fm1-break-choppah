# MATHCORE Groove Fidelity & Choppah Hub Export

**Feature PRD and implementation specification** · v1.1 · 28 Nov 2026  
**Parent:** [PRD_MATHCORE.md](PRD_MATHCORE.md)  
**Related product:** Choppah Hub — Audio & Groove Intelligence  
**Repository:** `profikid/fm1-break-choppah`  
**Implementation status (28 Nov 2026): partial.** The ten-byte GRUV/EVNT/GROO container, reference player,
scene export, cross-lane firmware playback, velocity, slice, gate, signed micro-timing, pitch and timbre paths exist.
This does **not** mean the full target in this PRD is implemented. Same-lane duplicate event handling, early-event identity, malformed chunk bounds and pending-queue saturation are fixed in draft firmware PR #14 and pass current-head host CI; the Hub duplicate-event reference-player and swung-grid export fixes are in draft PR #28. The Hub now carries the measured swung-grid index, preserves ghost/accent flags, allows gate editing, and aligns the event position and microtiming to the same swing template. Firmware still does not interpret ghost/accent flags, has two voices per lane and a four-entry pending queue; full Hub event provenance and lossless round-trip remain incomplete; real-FM-1 voice/CPU/memory headroom has not been measured.

**What is built**

| Piece | Where |
| --- | --- |
| The container: `GRUV` (the context, the swing, the scene map), `EVNT` (the events, ten bytes each), `GROO` (a break's own groove) | `app/site/chpk.js` (vendored into the hub) |
| The reference player: the lanes sound together, a lane with events plays them instead of its pattern | `app/site/mathcore.js` (vendored into the hub) |
| The export: a break → a scene, with the loss report and the capabilities | `app/site/mathcore.js` (`sceneFromBreak`, `exportLoss`) |
| The firmware: the chunk reader, a voice pool a lane, the events' velocity, slice, gate, micro-timing (early and late), timbre and pitch | `app/dsp/brk.c`, `app/app.c` |
| The capabilities: the status says what the device can do | `brk_mc_caps()`, `app/ui/main.c` |
| The Hub: the break → scene action, the scene sets and the packs carry the groove, the analysis measures it, and the analysed events become the device's groove | `choppah-hub`: `public/hub/sample.html`, `public/hub/groove.js`, `src/groove.js`, `src/mathcore.js`, `src/mcp.js` |

**Not yet complete:** device-side interpretation/display of ghost/accent flags (velocity still carries the audible dynamics); higher same-lane polyphony; complete event-level provenance round-trip; per-event gate analysis (the exporter defaults to 100% unless explicitly edited); complete Full/Scene/Hybrid export parity; and measured hardware voice/CPU/memory budget.

**The analysis reaches the device (28 Nov 2026).** The Hub's Audio Intelligence reads a track for its groove (every
hit: its role, its loudness, its time against the grid, its ghosts — the source coordinates never quantized), and
those events become the device's groove: each one lands on the break's slice and on the grid of the break's tempo,
and becomes an event the FM-1 plays (its velocity, its micro-timing, its ghost, its accent). Two events at the same
grid position are two events (a flam's grace note and its main hit), and the device plays both. The scene set is
saved in the library, and the pack carries the scene (`SCNE`, `GRUV`, `EVNT`) so the groove travels with the break.

**The timing resolution:** the sample (1/44100 s, 22.7 µs). The render counts the waiting hits one sample at a
time, so an event's micro-timing lands on the sample it belongs to — the render's block is not the resolution
(what the earlier drafts said). The device says so in its capability byte (`MC_CAPS_EARLY`) and in its status
(`mc_res`, in microseconds).

**What `GROO` is for (28 Nov 2026).** The per-slice groove — the loudness, the ghost, the time against the
grid, the brightness — is the Hub's record of the analysis: it travels with the pack's file, so the groove is
not only in the Hub's database. The device does not read it: the slice's own audio already carries its loudness
and its brightness, and the slice's position is the record of its timing, so the device's copy would count them
twice. The device gets the groove through `EVNT` — the events, which carry the same character per hit.

**The Hub's analysis (28 Nov 2026).** The groove is *heard*, not assumed: the slices are where the slicer cut
them, but the hit's own time is found by the sharpest rise of the energy in the slice, its loudness is the
attack's, and a hit far below the others is a ghost. The grid is the break's 16ths at its tempo, swung as the
break is swung — the swing and the micro-timing are separate things, and a hit that falls on the swung grid has
no micro-timing. The measurement is a slice at a time (`public/hub/groove.js` in the hub): it writes velocity,
flags, microtiming and timbre into `GROO`. The analyser also returns an ephemeral `grid_pos` and `grid_swing`
pair for the export; those two fields are not additional bytes in the `GROO` v1 format. The pack made in the hub
carries the **MATHCORE scene** of its first break too (`SCNE`, `GRUV`, `EVNT`) — so the device plays the groove
with the pack, as scene A, and not only the break's audio.

---

## 0.1 Audited implementation boundaries

The following distinctions are intentional:

- **Implemented subset:** the current firmware can play multiple role lanes together, and each EVNT record supplies velocity, slice, gate, signed microtiming, pitch and timbre. The browser reference player also consumes event records.
- **Fixed in draft firmware PR #14 (host CI green, not merged):** the firmware now iterates all records for a lane/position, tracks early scheduling by event-record index, validates GRUV/EVNT ranges and falls back when the late-event queue is full. The old implementation returned only the first matching record and collapsed multiple early events at the same position.
- **Not advertised after PR #14:** articulation capability. The renderer does not yet interpret the event ghost/accent/articulation flags, so the capability bit is removed rather than making a false promise.
- **Bounded playback:** the current firmware has a two-voice pool per lane and a four-entry pending microtiming queue per lane. This is not unlimited same-lane polyphony. Any increase needs target-device measurements.
- **Not equivalent:** lane-level procedural variation, accent masks and a two-voice lane pool do not equal an editable, source-faithful event timeline with independent articulation and timbre provenance.
- **Not hardware-certified:** host tests and a green firmware build do not establish actual FM-1 timing, CPU, memory, or audible overlap under worst-case load.



## 0.2 Groove export implementation update — 28 Nov 2026

The current draft implementation adds the following fidelity guarantees:

- `grooveOf()` returns the nearest swung `grid_pos` and the `grid_swing` used to measure `micro_us`. This prevents a drag near the midpoint between swung steps from being assigned to a different straight-grid step.
- The sample export passes the user's selected swing override into the analyser. The stored GRUV swing and the measured micro-offset therefore use the same template; swing is applied once by the player/scheduler.
- `sceneFromBreak()` uses the measured grid position when its swing matches, otherwise recomputes the nearest swung position and micro-offset together.
- Ghost and accent bits are both preserved in EVNT flags. The export-loss report only reports articulation loss when such markers are present and explains that current firmware still ignores those flags while velocity remains audible.
- The MATHCORE event editor exposes independent ghost, accent and gate controls; gate round-trips through EVNT. Analysis does not yet infer gate, so an unedited event defaults to 100%.
- Host regression fixtures cover a hit at 175 ms where the nearest swung step is step 2 but the nearest straight step is step 1, plus accent/gate round-trip and explicit capability-loss reporting.

Active code review: firmware [PR #14](https://github.com/profikid/fm1-break-choppah/pull/14), Hub [PR #28](https://github.com/profikid/choppah-hub/pull/28). These remain draft/unmerged; host CI and physical FM-1 acceptance are tracked separately.

## 1. Purpose

Make MATHCORE capable of importing, playing, editing and performing the full rhythmic detail detected by Choppah Hub.

The objective is not merely to convert a break into a four-lane step pattern. The export must preserve the qualities that make the original recording sound like a drummer:

- Exact event timing and microtiming
- Per-hit velocity and accent strength
- Ghost notes, flams, drags and other articulations
- Swing and timing relationships
- Timbre descriptors and per-event sound controls
- Source audio identity and sample boundaries
- Phrase, bar and fill structure
- Simultaneous kick, snare, hat and percussion events

The existing MATHCORE scene/lane system remains the performance and arrangement layer. A new event-based groove layer provides a loss-aware representation of the recorded performance.

**Core principle:** the event timeline is the source of truth; scenes and lanes are musical controls and transformations over that timeline.

The requirements below define the complete target. The implementation is partial: use the status table above
and the roadmap in §17 as the source of truth for what exists. Do not treat the presence of the container or a
reference player as proof that every requirement below is implemented.

## 2. Product goals

1. Export an analyzed break from Choppah Hub into MATHCORE without silently quantizing or discarding detected details.
2. Reproduce the imported groove at its original tempo and timing.
3. Allow the performer to independently manipulate roles, velocity, timing, articulation and timbre.
4. Retain the current MATHCORE scene queue, lane patterns, polymetric behaviour and live performance workflow.
5. Support hybrid playback: exact event playback plus scene-based transformations.
6. Keep the firmware implementation bounded in memory, CPU and voice count.
7. Make compatibility and any lossy conversion explicit to the user.

### Non-goals

- Running audio analysis or ML inference on the FM-1.
- Replacing the sample-based character of MATHCORE with synthesized drum voices.
- Requiring four independent copies of a break in memory.
- Making the firmware store large, unbounded JSON event objects.
- Requiring every Hub feature to be editable from the FM-1 screen.
- Treating a learned embedding or timbre descriptor as a substitute for source audio.

## 3. Existing MATHCORE baseline

The current repository provides a useful foundation:

- Four role lanes: kick, snare, hat and percussion.
- Per-lane hit and accent bitmasks, cycle length, rate and phase.
- Probability, ratchets, pitch, gate, level, seed, rise and variation.
- Scenes and scene chaining.
- A shared master clock.
- Slice role mapping and sample-based playback.
- A browser-side player in `app/site/mathcore.js`.
- Firmware structures and playback in `app/dsp/brk.h` and `app/dsp/brk.c`.

The current `mc_lane_t` is a compact 24-byte lane structure. It is well suited to procedural rhythm generation, but it does not store a separate record for every detected hit. Its velocity is primarily lane-level; accents are binary; generated ghost notes and timing variation are seed-driven.

The browser player returns event timing using a `late` value, but that is not a persisted, high-resolution source-event timeline.

The firmware MATHCORE path evaluates competing lane events and currently resolves them through a single winning event on the break voice. This is incompatible with guaranteed independent simultaneous playback of overlapping role events. The exact current render path must be revalidated at implementation time before changing it.

The existing structures and save formats must remain backward compatible.

## 4. Two complementary layers

### 4.1 Scene/lane layer

Keep the existing scene system for:

- Four role lanes and their familiar pattern controls.
- Scene queue and scene launch behaviour.
- Polymetric cycles and lane phase.
- Density, math, speed and chaos macros.
- Procedural variation and generated fills.
- Live performance shortcuts.

A lane can define a transformation or override over the imported event data. It should not be necessary to convert all imported events into a fixed 16-step mask.

### 4.2 Groove-event layer

Add an event timeline that records each detected hit independently.

Each event retains:

- Stable event ID and source asset ID.
- Original source start/end sample positions.
- Absolute source timestamp.
- Interpreted bar, beat and beat-relative position.
- Signed microtiming offset from a chosen reference grid.
- Role and role confidence.
- Velocity, accent strength and ghost-note classification.
- Articulation and gate.
- Timbre descriptors.
- Optional per-event playback overrides.
- Provenance and analysis version.

An event may be played, muted, moved, reclassified, replaced or transformed without changing unrelated events.

## 5. Canonical event data model

The Hub's canonical representation should be richer than the firmware representation. JSON below is illustrative and not a committed schema.

```json
{
  "schema": "choppah.groove-event",
  "version": 1,
  "asset_id": "asset_001",
  "event_id": "evt_018",
  "role": "SNARE",
  "source": {
    "start_sample": 184320,
    "end_sample": 190080,
    "sample_rate": 44100
  },
  "timing": {
    "absolute_sample": 184320,
    "absolute_time_us": 4179592,
    "bar_index": 1,
    "beat_index": 3,
    "beat_position": 2.5,
    "grid_position": 2.5,
    "micro_offset_us": -8400
  },
  "dynamics": {
    "velocity": 92,
    "accent_strength": 0.18,
    "ghost": true,
    "confidence": 0.94
  },
  "articulation": {
    "type": "GHOST",
    "gate": 0.82,
    "ratchet_count": 1,
    "choke_group": 0
  },
  "timbre": {
    "brightness": 0.68,
    "low_energy": 0.18,
    "mid_energy": 0.83,
    "high_energy": 0.71,
    "transient_strength": 0.59,
    "decay": 0.46,
    "noisiness": 0.62
  },
  "playback": {
    "pitch_semitones": 0,
    "gain": 1.0,
    "pan": 0.0,
    "filter": null,
    "drive": null
  }
}
```

All example values are illustrative, not measurements from a particular break.

### 5.1 Timing invariants

- Original source sample positions and timestamps are immutable provenance fields.
- Musical position, grid position, swing classification and micro-offset are derived interpretations.
- Re-analysis may update interpretations but must not overwrite original source positions.
- Never quantize the canonical event timeline during import.
- Use an explicit timebase and sample-rate field.
- Support negative and positive timing offsets.
- Preserve event order and allow multiple events at the same nominal grid position.
- If tempo or beat phase changes, recompute musical interpretations while retaining source timestamps.

### 5.2 Articulation types

Initial enum:

- `MAIN`
- `GHOST`
- `ACCENT`
- `FLAM`
- `DRAG`
- `ROLL`
- `OPEN_HAT`
- `CLOSED_HAT`
- `CHOKE`
- `GRACE_NOTE`
- `PERCUSSION`
- `UNKNOWN`

A flam or drag should be represented as multiple independently timed events, optionally grouped by a shared articulation-group ID. Do not model a flam only as a generic ratchet.

### 5.3 Confidence and provenance

Each inferred field should be able to carry confidence and provenance, at least in the Hub representation. Firmware export may omit confidence if space is constrained, but it must not confuse inferred values with source facts.

The Hub should retain model/analysis version, event detector version, source checksum, and any manual corrections.

## 6. Timing, swing and scheduler

### 6.1 Separate swing from microtiming

Swing describes a recurring timing relationship, such as delaying alternate subdivisions. Microtiming describes the actual offset of an individual event from a reference grid or swing template.

These must remain separate concepts.

For faithful playback, the detected absolute event time is authoritative. Swing is an editable interpretation or transformation. Do not apply a swing delay on top of a timestamp that already includes that same swing displacement.

### 6.2 Scheduler requirements

- Keep the existing master transport for scene and lane structure.
- Add a higher-resolution event timebase.
- Represent event offsets in a signed fixed-point or sample-domain format.
- Schedule events at sample offsets within the render block.
- Support early and late events, including events crossing nominal step boundaries.
- Handle multiple events at the same time.
- Maintain deterministic ordering for equal-time events.
- Avoid dynamic allocation and unbounded sorting in the audio interrupt.
- Prepare or sort bounded event windows outside the render-critical path where possible.
- Define safe behaviour for tempo changes, transport jumps, scene transitions and loop wrap.

A candidate representation is signed Q16 master ticks, but the final representation must be selected after measuring range, precision, conversion cost and memory on the target device. Sample-domain offsets may be preferable for playback once events enter the render block.

**How the firmware does it (28 Nov 2026).** The offsets are in the sample domain: the event's micro-timing is a
signed number of microseconds, and the engine turns it into a number of samples at the tempo it plays. A lane keeps
a small queue of hits that wait (four a lane); the render counts each of them down one sample at a time, so a hit
lands on the sample it belongs to — the resolution is the sample, not the render's block. An early event is
scheduled before its own grid tick (the lane's events are looked at whole, so an event is known before its tick
comes); a late event is scheduled at its tick. Nothing is allocated: the queue is a fixed array, and the countdown
is the render's own loop.

### 6.3 Swing controls

Support:

- Straight through strongly swung timing.
- Eighth-note and sixteenth-note swing templates.
- Adjustable swing amount.
- Per-role swing amount or timing offset.
- Preserve-original mode.
- Apply-swing mode with a clear preview and reversible transformation.

The Hub should export both the detected event offsets and any inferred swing profile. The device must not double-apply them.

**What is there (28 Nov 2026).** The Hub *hears* the swing: the grid that fits the break's hits best, kept only
when it explains them much better than the straight grid (50 straight .. 75, the device's own scale). The
break's slot carries it (`SSET`), so the device's steps — and with them the lanes' grid — swing the way the break
was played, and the events' micro-timing is measured *against that grid*: the device applies the swing once (to
the grid) and the micro-timing on top of it (the same grid, the same reference), never twice. The browser's
player honours the same swing (the `GRUV`'s), so the reference and the device agree.

**Who owns the swing (28 Nov 2026).** The break's own swing — the slot's `SSET` byte — is what the device plays:
the steps, the lanes' grid and the chop points all swing together, because the grid *is* the transport's ticks.
The `GRUV`'s swing is the *record* of the grid the groove was measured against (the browser's player honours
it). The Hub writes both from one detection, so they agree; a scene plays its groove as measured when it plays
with the break the groove came from (the device's model: a scene re-chops the break that plays). Changing the
swing in the editor alone would move the browser's playback and not the device's, so the editor shows the swing
and does not offer to change it: making the lanes' grid follow a per-scene swing would mean the master-tick
timeline stops being the scheduling backbone, which is not worth it for a device with a 5.8 ms block.

**What is not, yet:** the swing is *heard* and *carried*, not edited: the "apply the swing" and "straighten it"
transformations over the events' micro-timing, and the per-role swing.

## 7. Dynamics and velocity

Replace the assumption that a lane's single level and binary accent mask are enough for faithful groove playback.

Requirements:

- Per-event velocity, preferably normalized 0–127 in the interchange format.
- Per-event accent strength.
- Lane-level velocity scaling.
- Ghost-note velocity.
- Optional phrase velocity curves.
- Deterministic humanization when explicitly enabled.
- Preserve-original dynamics mode.
- Ability to edit one event or a selected group without changing the others.

Imported velocities must be stored as event values. They must not be regenerated from a random seed on each playback.

The existing lane `level` and accent controls remain useful as global multipliers/overrides.

**What is there (28 Nov 2026), and what it costs.** The events carry a velocity (0..127, the measured hit's
loudness against the break's loudest ✓), a ghost and an accent bit ✓, and the device applies them: the lane's
level times the velocity is the gain the hit plays at ✓.

**The approximation:** the slice a lane plays is *the recording's* audio ✓ — it already carries its own dynamics
✓ — so a velocity on top of it counts the hit's loudness twice ✓, and the groove's dynamics come out a little
stronger than the recording's ✓. This is the same on both sides ✓ (the browser is the reference ✓), and the
alternative (levelling every slice flat ✓ and letting the velocity carry the dynamics alone ✓) would flatten the
break's own playback ✓, which is worse ✓. It is noted here rather than hidden ✓.

## 8. Ghost notes and articulations

Ghost notes are first-class events, not merely procedural additions from `vary`.

Requirements:

- Store ghost classification and velocity independently.
- Preserve exact ghost-note timing.
- Support independent mute, gain and role reassignment.
- Support flam/drag grace events with individual timing and velocity.
- Support ratchets as a procedural performance operation, distinct from detected repeated hits.
- Support open/closed hat articulation and choke groups.
- Allow the performer to promote a ghost note to a main hit or vice versa.
- Do not silently drop low-velocity events during import or export.

## 9. Timbre and per-event sound controls

Distinguish source audio from timbre metadata.

### 9.1 Source audio

The original source sample or slice is the audible source of truth. Events should reference a source asset and region rather than duplicate audio.

### 9.2 Descriptors

The Hub should retain available descriptors such as:

- Brightness / spectral centroid.
- Low, mid and high energy.
- Transient strength.
- Decay / tail length.
- Noisiness.
- Spectral shape.
- Pitch or fundamental estimate where meaningful.
- Optional learned sonic embedding reference.

Descriptors enable search, comparison and sound selection. They do not automatically recreate the sound.

### 9.3 Playback overrides

Where supported by firmware, allow per-event overrides for:

- Pitch.
- Gain / velocity response.
- Filter cutoff and resonance.
- Drive / saturation.
- Bit reduction.
- Gate / decay.
- Pan.
- Delay and reverb send.
- Source slice or alternative source selection.

The initial firmware implementation should prioritize pitch, gain, gate, filter and source selection. More expensive effects should be added only after render-time measurements.

**What the firmware does (28 Nov 2026).** The event's **timbre** is its brightness, and the device has it: a
lowpass on the voice, after the slice's own effect (400 Hz at 0, 16 kHz at 255 — the browser's mapping, so the
reference and the device agree on what a timbre means). It is one pole, not the browser's biquad: two flops a
sample, no new state a voice cannot afford. The event's **pitch** is applied too, on top of the lane's own (each
voice carries its own pitch — the lane's, and the event's on top of it). The timbre does not reach the delay and
the verb: the throws are taken before the lowpass.

## 10. Polyphonic event playback

This is a critical engine requirement.

The event engine must be able to play simultaneous events independently. A kick and snare at the same time must not compete for a single winning lane event.

Introduce a bounded sample-voice pool:

1. Master transport and high-resolution clock.
2. Event scheduler.
3. Role-aware event dispatch.
4. Bounded sample voice pool.
5. Per-voice envelope, playback position, pitch and supported timbre controls.
6. Mixer and existing FX/output path.

### 10.1 Voice allocation

- Multiple events may reference the same source audio.
- Do not load four independent copies of the same break.
- Each active voice needs its own playback cursor and envelope.
- Define a configurable, measured maximum number of simultaneous voices.
- Protect main kick/snare events from low-priority ghost-note voice stealing.
- Use deterministic priority and stealing rules.
- Support choke groups, especially for open/closed hats.
- Define behaviour when the voice pool is exhausted.
- Expose voice saturation in status/diagnostics if feasible.

### 10.2 Resource constraints

The existing PRD and firmware comments emphasize strict RAM, flash and render-time constraints. Before selecting event capacity or voice count:

- Measure current flash and static/dynamic RAM on the actual FM-1 build.
- Measure worst-case render time with all supported voices and FX.
- Measure event scheduler worst-case work.
- Account for existing sample buffers, loop voices, keys, overlays and effects.
- Prefer fixed-size arrays, compact integer fields and precomputed event windows.
- Do not allocate, lock, perform file I/O or run analysis in the audio interrupt.
- Set explicit limits for events per clip, simultaneous voices and queued events.
- Document overflow and truncation behaviour.

No unverified resource budget should be treated as a guarantee. Re-measure against the current target build before merging.

## 11. Hub export profiles

### 11.1 Full Groove Export

Target profile for compatible firmware.

Includes all supported event timing, source references, velocity, accents, ghost notes, articulations, phrase structure, swing metadata and timbre descriptors/overrides.

The export UI must show any fields not supported by the target firmware. Full fidelity is only claimed when every required audible field is supported and the source audio is available.

### 11.2 Scene Export

Legacy-compatible approximation using existing MATHCORE lanes and scenes.

Maps detected events into four role lanes and existing pattern features. It may quantize, collapse simultaneous events, approximate velocity, or omit timbre and articulation detail.

The export UI must display a loss report listing each unsupported or approximated feature. Never label this profile lossless.

### 11.3 Hybrid Export

Preferred long-term profile.

- Existing scenes and lanes provide live performance controls and transformations.
- Groove-event data preserves the detailed original performance.
- Lane mute/solo, role replacement, density, phase and scene chaining operate over the event stream.
- Event-level edits remain independently addressable.
- Scene transitions preserve or intentionally reset phase according to explicit settings.

### 11.4 Export validation

Before writing to the device:

- Validate schema and version.
- Validate source references and checksums.
- Validate event count and resource limits.
- Validate timing range and sample-rate conversions.
- Validate role and articulation enums.
- Report unsupported fields.
- Report any quantization, event merging, velocity remapping or voice-limit truncation.
- Require explicit confirmation for lossy export.
- Preserve a copy of the original Hub groove data for round-trip recovery.

## 12. File format and compatibility

Do not silently change the existing MATHCORE scene structure or reinterpret existing reserved bytes without a migration plan.

Add versioned groove chunks to the existing pack/container format. Proposed logical chunks:

| Chunk | Purpose |
|---|---|
| Existing scene chunk | Existing scenes and lane configuration; unchanged |
| Existing chain chunk | Scene queue and shortcut assignments; unchanged |
| `GRUV` | Groove metadata, tempo, timebase, swing profile and phrase information |
| `EVNT` | Compact per-event records |
| Source references | Asset identity, source regions, sample rate and checksums |

The Hub may store richer JSON alongside the compact device payload. Firmware records should use bounded, versioned binary structures.

### 12.1 Capability negotiation

The device or export target should declare supported capabilities, for example:

- `event_timing_resolution`
- `supports_microtiming`
- `supports_per_event_velocity`
- `supports_articulations`
- `supports_polyphony`
- `supports_per_event_timbre`
- `supports_source_audio_references`
- `max_events_per_clip`
- `max_simultaneous_voices`

The Hub uses this capability set to produce an accurate export compatibility report.

### 12.2 Backward compatibility

- Existing scene-only projects continue to load and play unchanged.
- Old firmware ignores or rejects unsupported extension chunks safely; it must not misinterpret them as legacy data.
- New firmware loads legacy projects with default groove-event state.
- Save/load round-trips preserve unknown optional chunks where feasible.
- Format migrations are versioned and tested.
- Never overwrite the original project during a lossy conversion.

## 13. Browser player and editor parity

The browser implementation is the reference environment for validating event semantics before firmware porting.

Extend `app/site/mathcore.js` to:

- Represent groove clips and event records.
- Schedule events at the same semantic timebase as firmware.
- Preserve event order, timing offsets and simultaneous events.
- Reproduce per-event velocity and supported articulations.
- Model bounded polyphony and deterministic voice allocation.
- Serialize/deserialize the new format.
- Keep existing scene player behaviour unchanged for legacy projects.
- Provide deterministic test fixtures shared with firmware tests.

Browser and firmware playback need not be sample-identical where audio hardware differs, but event scheduling, timing, velocity mapping, articulation and voice-allocation semantics must match.

## 14. User experience

The user should be able to:

1. Analyze a break in Choppah Hub.
2. Inspect detected events and correct role/articulation mistakes.
3. Choose Full Groove, Scene or Hybrid export.
4. See a compatibility/loss report for the connected Mathcore target.
5. Export the groove and its required source audio.
6. Open it in Mathcore and hear the original groove.
7. Mute or solo roles without disturbing unrelated event timing.
8. Edit velocity, ghost notes, swing, microtiming and timbre.
9. Chain the groove into other scenes and fills.
10. Save the edited project and, where supported, round-trip it back to the Hub.

The performance workflow must remain immediate. Detailed event editing can live in the browser/Hub; the FM-1 should expose high-value performance controls rather than requiring users to edit hundreds of events on a small display.

## 15. Repository implementation map

Expected areas to inspect and modify during implementation:

| Path | Expected work |
|---|---|
| `app/dsp/brk.h` | Versioned event structures, articulation enums, scheduler API and bounded limits |
| `app/dsp/brk.c` | Event scheduling, sample-offset triggering, bounded polyphonic voices and per-event playback |
| `app/site/mathcore.js` | Browser event model, player, serialization and compatibility |
| `app/site/chpk.js` | Pack/container chunk read/write and migration |
| `app/site/fm1link.js` | Device transfer, capability negotiation and progress/error handling |
| `app/ui/mc.c` | Event-aware status and performance controls |
| `app/app.c` | Persistence, defaults and version handling as required |
| `tests/engine/` | C engine timing, polyphony, velocity, articulation and resource tests |
| Browser tests | Browser/firmware semantic parity and format round-trips |
| `docs/PRD_MATHCORE.md` | Link to this specification and shared roadmap |

This is a proposed change map, not a claim that all paths or interfaces are already confirmed as the final implementation points. Re-inspect current code before each implementation phase.

## 16. Acceptance criteria

### Import and fidelity

- [ ] Import a multi-bar break with detected kick, snare, hat and percussion events.
- [ ] Preserve every event's source sample boundaries and absolute timestamp.
- [ ] Preserve events that occur at the same nominal grid position.
- [ ] Preserve positive and negative microtiming.
- [ ] Preserve per-event velocity and accent strength.
- [ ] Preserve ghost notes and multi-event flam/drag articulations.
- [ ] Preserve phrase/bar boundaries and fill positions.
- [ ] Preserve swing metadata without applying it twice.

### Playback

- [ ] Browser player reproduces imported event timing without grid quantization.
- [ ] Firmware scheduler triggers events at the defined sample offsets within the supported resolution.
- [ ] Simultaneous kick/snare/hat events play independently within the declared polyphony limit.
- [ ] Main kick/snare events are protected by deterministic voice-stealing policy.
- [ ] Per-event velocity changes audible gain consistently.
- [ ] Ghost notes remain independently editable and audible.
- [ ] Choke groups behave deterministically.
- [ ] Scene transitions, transport jumps and loop wrap do not duplicate or lose events.

### Timbre and editing

- [ ] Source audio remains addressable and is not replaced by descriptors.
- [ ] Supported per-event timbre overrides are applied.
- [ ] Unsupported timbre controls are reported during export.
- [ ] Role mute/solo does not alter timing of other roles.
- [ ] Event edits survive save/load.

### Compatibility and performance

- [ ] Existing scene-only projects load and play unchanged.
- [ ] New projects round-trip through browser serialization.
- [ ] Unsupported firmware receives a safe compatibility failure or explicit lossy export path.
- [ ] Export reports all known losses.
- [ ] Firmware tests establish maximum event count and polyphony.
- [ ] Worst-case render time remains within the measured audio deadline.
- [ ] Memory and flash usage are measured on the target hardware.
- [ ] No dynamic allocation or blocking work occurs in the audio interrupt.

## 17. Roadmap

| Milestone | Scope | Priority | Status |
|---|---|---|---|
| G0 | Audit current pack format, firmware render path, memory and timing budgets | P0 | Partial: source/format reviewed; real-device budget not measured |
| G1 | Define canonical Hub groove-event schema and compact device binary schema | P0 | Partial: GRUV/EVNT v1 exists; canonical IDs/provenance and complete validation remain |
| G2 | Implement browser event model, scheduler and deterministic fixtures | P0 | Partial: reference player exists; duplicate-record identity fix and full timing fixtures pending merge/CI |
| G3 | Implement Full/Scene/Hybrid export with loss report | P0 | Partial: Full/Scene paths exist; distinct Hybrid profile/parity not evidenced |
| G4 | Firmware event scheduler with high-resolution timing | P0 | Partial: sample-countdown microtiming exists; duplicate same-position fix pending merge/CI |
| G5 | Bounded polyphonic sample voice pool | P0 | Partial: two voices per lane; target-device saturation/polyphony budget not measured |
| G6 | Per-event velocity, ghost notes and articulations | P0 | Partial: velocity is applied; ghost/accent/articulation flags are not rendered; articulation capability removed in PR #14 |
| G7 | Swing editing and timing transformations | P1 | Partial: swing is measured/stored as context; independent edit/transform semantics not complete |
| G8 | Per-event timbre and source replacement | P1 | Partial: timbre low-pass is applied; source replacement workflow not evidenced |
| G9 | Scene transformations over groove-event data | P1 | Not evidenced |
| G10 | Hub ↔ device round-trip and performance hardening | P1 | Partial: pack chunks round-trip; complete validation, device stress and lossless parity remain |

### Recommended implementation order

Do not begin by rewriting the firmware scheduler.

1. Audit the current firmware render path, pack format and actual resource budgets.
2. Finalize the canonical event semantics and binary encoding.
3. Implement the browser event model and player first.
4. Prove lossless round-trip of timing and event metadata.
5. Implement Hub export profiles and compatibility reporting.
6. Port the tested scheduler semantics to C.
7. Add bounded polyphony and per-event playback controls.
8. Benchmark worst-case device load before increasing limits.

This sequencing reduces the risk of implementing a firmware format that later proves unable to represent the Hub's analysis.

## 18. Risks and open decisions

| Risk / decision | Required resolution |
|---|---|
| Firmware RAM/flash/render budget | Measure current build and establish hard limits before choosing event/voice capacities |
| Single-voice assumptions in current playback | Confirm all call paths and design bounded polyphony without breaking existing loop/key/pad voices |
| Timing resolution | Select sample-domain or fixed-point tick representation after precision/range analysis |
| Source sample ownership | Define whether the device receives full break audio, selected slices or references to an already loaded break |
| Event count | Define maximum events per clip and a deterministic overflow policy |
| Timbre cost | Prioritize low-cost per-event controls; defer expensive processing until measured |
| Swing interpretation | Keep original timing authoritative and avoid double-applying swing |
| Format compatibility | Define chunk versioning, unknown-chunk behaviour and safe legacy handling |
| Role uncertainty | Keep confidence and allow user correction in the Hub |
| Round-trip edits | Define stable IDs and conflict semantics for event changes made on both Hub and device |

## 19. Definition of done

This feature is done when a real break analyzed by Choppah Hub can be exported to a compatible MATHCORE device, played with its original event timing and dynamics, and edited in performance without losing ghost notes, articulations or simultaneous events. The user can intentionally transform the groove, save it, and understand exactly which details are retained or approximated.

A four-lane step-pattern approximation alone does not satisfy this PRD.
