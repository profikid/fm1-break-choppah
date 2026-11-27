# CHOPPAH — MATHCORE Groove Fidelity

**Status:** spec. Companion to `docs/PRD_MATHCORE.md` (the queue and the layered rhythm engine) and the hub's
`docs/PRD_AUDIO_INTELLIGENCE.md` / `docs/ARCHITECTURE_AUDIO_INTELLIGENCE.md` (the analysis that produces the
events this spec plays).

## 1. Purpose

MATHCORE today says *when* to hit and *which slice* to play. It cannot say *how hard*, *how late*, *how
bright* or *that a note is a ghost*. A groove is mostly those four things, so a groove exported to MATHCORE
today is a skeleton: the right rhythm with every hit at the same level, on the grid, with the ghosts
invented at random.

This spec adds a **groove-event layer** beside the scene/lane layer: the per-hit velocity, micro-timing,
articulation and timbre, and the swing they are measured against. The scene/lane layer stays what it is —
the arrangement and the performance controls. The groove-event layer is the detail.

Nothing here replaces the scene/lane model. A device that ignores the new chunks still plays the scene.

## 2. What the code does today (the gap)

Read off `app/dsp/brk.c` and the hub's `public/hub/mathcore.js`:

| | Today | The source |
| --- | --- | --- |
| Layers | **One event per tick.** The four lanes compete on a score and the best one wins; only the winner is played, on the break's single voice, cutting what was sounding. The engine's pool is `voice_t v[2]`. | `brk.c:66`, `brk.c:877-890`, `brk.c:630` |
| Velocity | Two levels: the accent ×1.0, a plain hit ×0.55, times the lane's level; `vary` softens it at random. | `brk.c:860` |
| Micro-timing | Only a random "laid back", at most a tenth of a 16th, and only for ghosts and off-beat hats. Never early. | `brk.c:846` |
| Ghost notes | **Generated** by `vary` (a drag before a hit, a bounce after it, an off-beat), seeded so they repeat. | `brk.c:811` |
| Flams and drags | A ratchet: the slice again, evenly inside the tick. Not two independently timed events. | `brk.c:836` |
| Timbre | One pitch per lane, in semitones, plus a riser's `rise`. | `brk.c:869` |
| Swing | Not in the lane at all: the lanes run on the integer master timeline (`rel % r`). The break has a swing, in its SSET, applied when the *break* plays. | `brk.c:793` |

And the deepest one: **MATHCORE re-schedules the break's slices onto a 16th grid.** The break's own timing —
where its slices actually land, which is where the groove lives — is not what MATHCORE plays. The lane's
mode chooses *which* slice of the role's pool; *when* is the grid's. Unless an event's own timestamp
survives into the container, the groove is lost on export.

`MM-LE-001` ("Run KICK, SNARE, HAT and PERC lanes simultaneously", P0) is therefore not what the code
does today. **The layering comes first.** The format is the easy part.

## 3. The groove-event layer

### 3.1 The event

An event is one hit, with everything that makes it that hit:

| Field | Range | Meaning |
| --- | --- | --- |
| `slice` | 0–31 | the source slice of the break |
| `velocity` | 0–127 | how hard (the engine's own level is a multiplier on this) |
| `micro_us` | ±32767 µs | the offset from the grid, signed: negative is early |
| `pitch` | −24…+24 | semitones |
| `timbre` | 0–255 | the brightness (the engine's own filter, coarse) |
| `gate` | 0–100 | how long, as a share of the slice |
| `flags` | — | the ghost, the accent, the flam, the drag, the articulation |

The articulation (`flags` bits 4–5): `0 NONE`, `1 MAIN`, `2 GHOST`, `3 GRACE_NOTE`. A flam and a drag are
**two events**, not a flag: a flam is the same slice twice, the second 18–30 ms later and quieter; a drag
is the same slice twice, the second 30–60 ms later and softer.

### 3.2 The swing

The swing is a **phrase/grid-level** interpretation (a ratio, 50–75 for the off-8ths), not a per-event
value. The micro-timing is the per-event offset **from that grid**. The two are independent and must not
be double-applied: an event whose timestamp already contains the swing must not be swung again.

The rule: the *grid* is straight (or swung, if the profile says so) and the event's `micro_us` is measured
against **the grid as declared**. The exporter declares the grid it measured against, so the device applies
the swing to the grid and the micro-timing to the event, once each.

### 3.3 The scene/lane layer stays

The 24-byte lane is a fine arrangement layer: rate, phase, probability, ratchet, mode, pick, gate, level,
seed, vary. It stays as it is, and the `SCNE` chunk (MCS5) is not changed.

A scene with a groove-event layer is a scene **and** an event list. A scene without one (the factory's, an
old pack's) plays exactly as it does now.

## 4. The container

Three new chunks. The old ones are untouched, and an old device ignores all three.

### 4.1 `GRUV` — the groove's context

Fixed 8 bytes, then the scene map.

```
+0  u8   version = 1
+1  u8   ticks_per_16th = 12        the master: 12 ticks a 16th, 48 a beat
+2  u8   swing                     50..75: the off-8ths' ratio in %
+3  u8   flags                     bit0: the swing applies to the lanes
+4  u16  bpm_centi                 the master tempo; 0: the break's own
+6  u16  scene_count               how many scenes have events
     then scene_count × 4 bytes:
+0  u8   scene                     the scene's index (0..15)
+1  u8   event_count               its events
+2  u16  first_event               where they start in EVNT
```

### 4.2 `EVNT` — the events

Ten bytes an event, in the order the scene map gives.

```
+0  u8   flags    bit0 ghost, bit1 accent, bit2 flam, bit3 drag, bits 4-5 the articulation,
                   bits 6-7 the lane it belongs to
+1  u8   velocity 0..127
+2  i16  micro_us the offset from the grid, signed
+4  i8   pitch    -24..+24
+5  u8   slice    0..31
+6  u8   timbre   0..255
+7  u8   gate     0..100
+8  u8   pos      the lane's event index (0..31): where on the lane's grid it falls
+9  u8   rsv
```

The event's `pos` is the same index space as the lane's hit bitmap, so the lane's pattern still says
*which* of its positions are hits and the events say *what* each of them is. The `micro_us` moves it off
that grid — and a flam's second note (or a drag's) is the same position again, later by its micro.

### 4.3 `GROO` — a break's own groove

The break's slices, with the same per-slice character, so the groove travels **in the file** and not only
in the hub's database. Two bytes of header, then the breaks in order.

```
+0  u8   version = 1
+1  u8   break_count
     then, per break:
+0  u8   slice_count
     then slice_count × 5 bytes:
+0  u8   velocity 0..127
+1  u8   flags    bit0 ghost, bit1 accent, bits 2-3 the articulation
+2  i16  micro_us
+4  u8   timbre   0..255
```

**The groove does not go in the SSET.** The SSET is part of the device's own saved state
(`mq_t`, with a `_Static_assert` on its size for the 1.7's format), so growing it would break the saved
settings of every FM-1. A separate chunk keeps the device's save format untouched.

## 5. The playback semantics

### 5.1 The voices

The scenes need **one voice per lane**: four, so the four lanes sound together. They are the scenes' own
pool, separate from the break's two (`v[2]`) and the layer's two (`lv[2]`), so a scene playing does not
fight the break or the layer for a voice.

The voice pool is bounded and the stealing is by priority: a **main** kick or snare is never stolen by a
ghost; a ghost is the first to go; a lane that is already sounding is cut by its own next hit, as the
break's voice is today.

### 5.2 The scheduler

Events fire at their own time: the grid's time plus the event's `micro_us`. So:

- a **late** event is scheduled at the tick, like the `late` of today;
- an **early** event is scheduled at the **previous** tick, with its own time inside the block, so it is
  heard early and not late;
- several events may fall in one render block, and a block may hold more than one lane's hit.

The resolution: the master ticks 12 times a 16th (`MC_TICKS = 12`), so a tick is ~10 ms at 120 BPM. The
engine resolves to the **sample** for the offset (the block is 256 frames at 22.05 kHz ≈ 11.6 ms), so the
practical resolution is the block, ~11.6 ms. The capability byte says so, and the exporter rounds or
reports the loss.

### 5.3 The parameters

At trigger time, an event plays:

```
gain  = the lane's level × the event's velocity / 127 × (the accent's own factor)
pitch = the lane's pitch + the event's pitch
gate  = the lane's gate × the event's gate / 100
timbre= the event's timbre (the engine's filter, coarse)
```

An event with no velocity (0xFF in `GROO`, or a scene with no events) plays at the lane's level and the
accent's factor, exactly as today.

### 5.4 The ghosts

A ghost is a **first-class event** with `flags` bit 0 set, its own velocity (usually 20–45) and its own
micro-timing (a drag's first note is late, a bounce's is early). The `vary` generator stays as it is — a
drummer's variations — and is not the same thing.

## 6. Capability negotiation

The device says what it can do. New MCCTL op 7:

```
MCCTL op 7 (the groove's capabilities) -> 8 bytes:
+0 u8  caps        bit0 the per-event velocity, bit1 the micro-timing, bit2 the articulation,
                          bit3 the timbre, bit4 the layering (the lanes together), bit5 the early events
+1 u8  voices      how many voices the scenes have
+2 u8  max_events  the events a scene may hold
+3 u8  resolution  the timing resolution, in 100 µs
+4 u16 max_scenes  the scenes that may carry events
+6 u16 rsv
```

The exporter asks (the hub's `mcCaps`, the MCP's `mathcore_capabilities`) and then:
- **Full Groove**: everything the events carry, if the device says yes to each field; the ones it says no
  to are listed in the loss report.
- **Scene**: the existing four-lane model; the loss report lists everything that did not fit.
- **Hybrid**: the scenes and the chain as they are, plus the events for the devices that can play them.

**Never degrade silently.** An export that cannot carry a field says so, in the export preview and in the
report.

## 7. The export

The hub's export adapter (`src/audio/mathcore-export.js` in the Audio Intelligence plan) turns an event
timeline into a pack:

1. the events → the roles (kick, snare, hat, perc) → the four lanes;
2. the events → the lane patterns (the hits on the grid) and the lane's level, accent and seed;
3. the events → the `EVNT` records, with the micro-timing measured against the declared grid;
4. the swing → `GRUV`;
5. the break's own groove → `GROO` (so it travels with the file);
6. the loss report: what the target cannot represent.

A **break** (not an analysed asset) is exported the same way, from its own slices: its role map
(`SSET`) gives the lanes, its slice positions give the patterns, its swing gives the grid, and its `GROO`
gives the per-slice character. That export needs no analysis at all — it is the **Scene** profile, and it
works on the firmware of today.

## 8. The browser player is the reference

`public/hub/mathcore.js`'s `Player` is today a faithful port of the firmware's single-winner arbitration.
Before the firmware work, it becomes the **reference**: the lanes layer, the per-event velocity, the
micro-timing (early and late), the ghosts as events, the swing. The semantics are settled there — in the
hub, with no firmware release, no flash budget and no device — and the firmware follows.

The hub's player is the yardstick for the loss report: what the browser can play, the device can play.

## 9. Requirements

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| MG-001 | The four lanes sound together | P0 | A scene with a kick and a snare on the same tick plays both, not the louder one |
| MG-002 | One voice per lane, bounded | P0 | Four voices; the scenes never take the break's or the layer's |
| MG-003 | Voice stealing protects the main hits | P0 | A ghost is stolen before a kick or a snare |
| MG-004 | Per-event velocity | P0 | Two events of one lane play at their own levels |
| MG-005 | Per-event micro-timing, early and late | P0 | An event 7 ms early is heard early, not late |
| MG-006 | Several events in one render block | P0 | Two lanes' hits 3 ms apart both sound |
| MG-007 | Ghosts are first-class events | P0 | A ghost plays at its own velocity and time, not at a generated 0.3 |
| MG-008 | Flams and drags are two events | P1 | The second note of a flam is quieter and later |
| MG-009 | Per-event pitch, gate and timbre | P1 | The event's values reach the voice |
| MG-010 | The swing is a profile, the micro-timing per event | P0 | A swung groove is not swung twice |
| MG-011 | `GRUV`, `EVNT` and `GROO` round-trip | P0 | A pack written and read back keeps every event |
| MG-012 | The old chunks stay readable | P0 | A pack with the new chunks plays on a device that ignores them |
| MG-013 | The device reports its capabilities | P0 | The exporter can ask and act on the answer |
| MG-014 | No silent degradation | P0 | Every export lists what it could not carry |
| MG-015 | The browser player is the reference | P0 | The browser plays what the device plays |

## 10. The test plan

- **Engine** (`tests/engine/test_engine.c`): two lanes on one tick both sound; a ghost is quieter and
  later; an early event is early; two events in one block; the voice pool never exceeds its bound; a
  main hit is not stolen by a ghost.
- **Format** (`tests/chpk.test.mjs`): `GRUV`, `EVNT` and `GROO` round-trip; a bad length is refused; the
  old chunks are untouched; a pack with the new chunks still parses.
- **Reference** (`tests/mathcore.test.mjs`, the hub's): the player layers, applies the velocity, the
  micro-timing and the timbre, and plays the ghosts as events.
- **Export** (the hub's): a break → a scene; the loss report lists what a Scene export drops; the
  capabilities decide what a Full Groove export keeps.

## 11. The order

1. The format: `GRUV`, `EVNT`, `GROO`, and the chunk writer/reader (`app/site/chpk.js`, vendored into the
   hub).
2. The browser player: the reference semantics, in the hub.
3. The export: break → scene, with the loss report and the capabilities.
4. The firmware: the chunk reader, the voices, the scheduler, the parameters.
5. The loss report's completeness: what each profile drops, measured against the reference.

## 12. The decisions

- **The voices.** Four is the minimum. The PRD's open decisions ("measure target timing resolution, event
  and voice limits") have to settle it: the FM-1's RAM and CPU are the budget, and the layer's two voices
  are already spoken for.
- **The resolution.** The block is 256 frames at 22.05 kHz ≈ 11.6 ms. That is the practical resolution
  unless the block is shortened. The capability byte reports it, so the exporter can round and say so.
- **`EVNT` per scene or per pack.** Per scene keeps the scenes independent; per pack is smaller but couples
  them. The scene map in `GRUV` allows either; per scene is the first.
- **The groove in the file or only in the hub.** In the file (`GROO`), so it travels to another FM-1 — that
  is the recommendation, at ~80 bytes a break.
- **The break's own groove: authored or analysed.** The analysis can fill `GROO`; the maker can edit it.
  Both write the same chunk.
