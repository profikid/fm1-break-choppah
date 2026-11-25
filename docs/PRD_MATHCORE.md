# CHOPPAH — MATHCORE Queue & Layered Sample Engine

**Feature PRD and implementation roadmap** · v1.0 · 22 Nov 2026  
**Parent product PRD:** [PRD.md](PRD.md)  
**Repository:** `profikid/fm1-break-choppah`  
**Status:** Released in v1.5.5 (23 Nov 2026, Laurens's call), built on the `mathcore` branch. See §14,
and §15 for the revision to a played instrument (the same evening)

---

## 1. Purpose

MATHCORE turns CHOPPAH from a break slicer with a single pattern sequencer into a playable, sample-only rhythm instrument. It combines:

1. A **Slice Map** that identifies which slices contain kick, snare, hi-hat, percussion or other material.
2. A **Layered Rhythm Engine** in which those roles play independent patterns simultaneously.
3. A **Queue** that plays saved rhythm scenes in order and loops back to the beginning.
4. An **FM-1 performance workflow** that lets the player append scenes with the black keys while continuing to play and loop slices with the white keys.

The sound source remains recorded audio. A kick lane triggers a kick slice; it does not synthesize a kick. The same applies to snare, hi-hat and percussion.

### 1.1 Product principles

- **Sample-only:** every audible lane event plays a slice or region from the loaded break/sample context.
- **Layered, not exclusive:** kick, snare, hi-hat and percussion can all run at once.
- **Instant queueing:** one press appends a scene; no modal editor is required for normal performance.
- **White keys stay playable:** direct slice triggering and held slice looping remain available while MATHCORE runs.
- **Black keys become rhythm shortcuts in MATHCORE mode:** existing performance-pad behaviour remains available outside that mode.
- **Predictable playback:** the queue advances at defined musical boundaries and repeats from its first item.
- **One master clock:** independent lane cycles never create independent or drifting clocks.
- **FM-1 first:** use existing controls and display; do not invent extra physical keys or assume more memory, sample decks or DSP capacity than measured.

### 1.2 Terminology

| Term | Meaning |
| --- | --- |
| Slice | A region of the loaded break, already playable from a white key |
| Role | A musical label assigned to one or more slices: KICK, SNARE, HAT, PERC or OTHER |
| Lane | One independent stream of sample-trigger events |
| Layer | A lane sounding at the same time as other lanes |
| Rhythm | The event pattern and timing rules for a lane |
| Scene | A saved snapshot containing one or more active role lanes and their settings |
| Queue | An ordered list of scenes that plays automatically |
| Shortcut | A black-key assignment that appends a scene to the queue |

A queue item is a **scene**, not an isolated instrument lane. A scene may contain only a kick rhythm, or it may contain kick, snare, hat and percussion together. This makes simple one-layer patterns and complex layered grooves work in the same queue.

---

## 2. Repository baseline audit

The following was checked against the default branch on 22 Nov 2026.

### 2.1 Latest inspected revision

- Latest commit: [006329c](https://github.com/profikid/fm1-break-choppah/commit/006329c8cd5374ce11166b6861e962281fdfa883)
- Commit date: 22 Nov 2026.
- Change: AMEN break preparation now cuts the drummer's actual bar boundaries and uses the tempo of the selected bars (about 144.7 BPM for the selected AMEN passage), rather than forcing the file-wide average tempo.
- The commit message reports 154 engine checks passing. The GitHub combined-status query returned no status records, so this document does not claim a separately verified green Actions run.

### 2.2 Relevant existing implementation

| Area | Existing state | Reuse / implication |
| --- | --- | --- |
| White keys | 16 slice keys; held keys can loop their own sample window | Preserve as direct performance controls in MATHCORE |
| Black keys | 11 performance pads: rolls, reverse, freeze, half, tape stop, gate, random, filter and dub | Do not permanently steal these functions; give them a MATHCORE queue-shortcut role only in MATHCORE mode |
| Break engine | Platform-free engine in `app/dsp/brk.c` and `app/dsp/brk.h`; 44.1 kHz; 16th-based transport; up to 64 existing pattern steps | Extend carefully; do not replace the proven sample playback path |
| Split breaks | Two break halves can be combined; white-key loops can layer across the two halves | Useful precedent for sample layering, but not equivalent to independent rhythm lanes |
| Pattern sequencer | Existing sample-break steps plus separate kick, snare and hat drum lanes | Rework the drum-lane concept into sample-role lanes; preserve compatible break-step behaviour where possible |
| Drum voices | `app/dsp/kick.h` describes a four-layer synthesised kick; `app/dsp/perc.h` describes synthesised snare and hi-hat | These are legacy implementation, not the target MATHCORE sound architecture |
| App glue | `app/app.c` includes `brk_synth.h`, `kick.h` and `perc.h`; settings include kick/perc state | Product cleanup and save migration are prerequisites |
| Persistence | Settings are append-compatible; patterns use their own store objects | Add new fields/objects safely; do not silently reinterpret old drum data |
| Display | 240 × 240 colour display; existing screen and knob labels are context-sensitive | Use compact queue strip and lane overview; detailed editing can remain on setup pages |

### 2.3 Important product/code gap

The parent [sample-only PRD](PRD.md) already defines the intended direction, but the checked-in implementation has not reached it. The current source still contains synthesised kick/snare/hat engines and the README/manual still describe those features. The latest commit is a break-preparation improvement, not a MATHCORE implementation.

Therefore, the roadmap starts with **sample-only product cleanup**. Do not mark the synthesised drum removal, role map, layered lanes, scene queue or black-key queue workflow as implemented.

### 2.4 Existing behaviour to preserve

- Break loading, bank format and user-break workflow.
- 16 white-key slice triggering.
- White-key hold-to-loop, per-key loop settings and click-free voice changes.
- Existing split-break and half-latch behaviour.
- Tempo, tap tempo, MIDI clock in/out and transport.
- Sample FX, per-slice FX and black-key performance pads outside MATHCORE mode.
- Pattern save/recall and safe settings migration.

---

## 3. User experience

### 3.1 Setup flow

1. Load a break and choose its slice layout.
2. Open **SLICE MAP**.
3. Assign slices to roles. A slice may be assigned to more than one role.
4. Create or edit rhythms for KICK, SNARE, HAT and PERC lanes.
5. Save the combination as a scene.
6. Assign scenes to black-key shortcuts.
7. Enter **MATHCORE LIVE** and build a queue by tapping the black keys.

~~Manual role assignment is the first release requirement.~~ **Changed 22 Nov 2026 (Laurens): the Slice Map is
made automatically, nothing manual.** The FM-1 hears each slice's hit when the break loads (three filter bands, no
machine learning, no network: §4.2a), so step 3 above happens by itself and SLICE MAP shows the result.

### 3.2 Live performance flow

- White key: trigger its slice immediately according to the existing snap/quantise setting.
- Hold white key: loop that slice using the existing key-loop behaviour.
- Tap a black key in MATHCORE LIVE: append the scene assigned to that shortcut to the end of the queue.
- Tap the same black key again: append another instance. Repetition is intentional.
- Press PLAY: start the queue.
- The current scene plays for its configured phrase duration, then the next queued scene starts automatically.
- After the last scene, playback returns to the first scene and repeats forever.
- If only one scene is queued, that scene repeats forever.
- White-key performance continues while the queue is playing; manual slice triggers do not clear, pause or advance the queue.

No queue editor is required for the common case. The display gives enough feedback to see the current scene, next scene and remaining queue.

---

## 4. Slice Map

### 4.1 Roles

Initial role set:

- KICK
- SNARE
- HI-HAT
- PERCUSSION
- OTHER / UNASSIGNED

A slice may have multiple role tags. For example, a noisy break hit may be useful as both SNARE and PERCUSSION.

### 4.2 Requirements

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| MM-SM-001 | Assign or clear one or more role tags per slice | P0 | Every slice can be assigned from the device UI |
| MM-SM-002 | Show role tags clearly in the slice map | P0 | The user can identify role assignments without opening each slice |
| MM-SM-003 | Save role map per break / sample identity | P0 | Loading another break does not inherit the previous break's map |
| MM-SM-004 | Allow multiple slices per role | P0 | A lane can select from a set of tagged slices |
| MM-SM-005 | Allow one slice to belong to multiple roles | P1 | Shared/ambiguous hits can be reused |
| MM-SM-006 | Provide a deterministic fallback for unassigned roles | P0 | Empty role produces silence and a visible warning; never synthesize a replacement |
| MM-SM-007 | Preserve slice map across firmware restart | P0 | Save, reboot and reload yields identical assignments |
| MM-SM-008 | Optional browser-side transient suggestions | P2 | Suggestions are editable and never silently overwrite manual tags |

### 4.2a The map, heard (as built)

`mc_auto_roles` (app/dsp/brk.c) runs in the main loop whenever a break is loaded (a bank break, the custom break, a
split): the first 60 ms of each slice through one-pole filters into a low band (under ~120 Hz), a middle band (~250 Hz
to 4 kHz) and a high band (over ~7 kHz). Normalised over the break's slices: loud low → KICK; loud middle, less low
→ SNARE; a quiet, mostly high hit → HAT; other hits with some level → PERC. KICK, SNARE and HAT each get at least their
likeliest slice; an empty PERC takes the quieter snares. Deterministic: the same audio gives the same map, every load
and every boot (MM-SM-003 and MM-SM-007 hold without a store object; MM-SM-001, 002 and 008 are replaced by the
read-only MAP page). On the eight bank breaks it finds the kicks on the downbeats and the snares on 2 and 4 (AMEN:
K K S · H K H S | H P S · H K H S).

### 4.3 Slice selection modes

Each lane chooses from its assigned role set using:

- **FIXED:** always use one chosen slice.
- **ROUND ROBIN:** cycle through assigned slices in order.
- **RANDOM:** choose from assigned slices using the scene's deterministic seed.
- **ALTERNATE:** alternate between two selected slices.
- **VELOCITY / ACCENT:** choose between configured soft and accented slices based on event velocity.

When a role has no assigned slices, the lane is silent and the screen identifies the missing role. There is no synthesized kick/snare/hat fallback.

---

## 5. Layered Rhythm Engine

### 5.1 Default lanes

A scene can contain these four independent sample lanes:

1. KICK
2. SNARE
3. HI-HAT
4. PERCUSSION

All enabled lanes sound simultaneously. A lane is not a mixer channel containing a synthesizer; it is a sample-event scheduler referencing the role map.

The architecture should allow additional user-defined sample lanes later, but four role lanes are the first-release target.

### 5.2 Lane parameters

Each lane has:

- Enable, mute and solo.
- Role assignment / allowed slice set.
- Pattern events and event count.
- Cycle length (initial target: 1–32 events).
- Clock ratio / subdivision.
- Phase offset and rotation.
- Accent, velocity and probability.
- Ratchet / roll count.
- Per-event pitch, reverse and gate locks where supported by the sample engine.
- Slice-selection mode and deterministic random seed.
- Reset policy at scene start.

### 5.3 Timing model

All lanes share the existing master transport and MIDI clock. Each lane can have a different event count and phase, but it must not run from a separately accumulated floating-point clock.

The engine should schedule events against a stable integer/fixed-point master timeline. Lane cycle position is derived from that timeline. This avoids drift when 5-, 7-, 11- and 13-event cycles run together.

Meters and cycle lengths must not be conflated. A 7-event lane is not automatically a 7/8 song meter; it may be a 7-event cycle running against a 4/4 master phrase.

### 5.4 Requirements

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| MM-LE-001 | Run KICK, SNARE, HAT and PERC lanes simultaneously | P0 | Four lanes trigger overlapping sample voices without timing loss |
| MM-LE-002 | Independent event count per lane | P0 | 5-, 7-, 11- and 13-event lanes run together |
| MM-LE-003 | Independent phase and rotation | P0 | Changing one lane does not reset the others |
| MM-LE-004 | Shared master clock and MIDI sync | P0 | No cumulative drift in long host/device tests |
| MM-LE-005 | Per-lane mute and solo | P0 | Muting/soloing does not reset transport or lane phase |
| MM-LE-006 | Role-aware slice selection | P0 | Every triggered event resolves to an assigned sample slice |
| MM-LE-007 | Per-event accent, probability and ratchet | P1 | Values are deterministic when probability seed is fixed |
| MM-LE-008 | Lane visual phase and next-event feedback | P0 | Screen shows each lane's cycle and current position |
| MM-LE-009 | Bounded polyphony and event density | P0 | Worst-case lane combination cannot starve the audio callback |
| MM-LE-010 | No allocations in audio render callback | P0 | Engine tests / instrumentation confirm bounded real-time work |

### 5.5 Layering and voice budget

The first implementation uses one active decoded break/sample context, including the existing split-break context. Do not assume four independent decoded sample decks.

Multiple lanes may trigger the same or different slices from that context. Voice count, simultaneous retriggers, reverse playback and FX sends must be budgeted against the measured FM-1 RAM and CPU limits. If overloaded, use the existing overload policy: shed optional FX or lower-priority voices before audio stability is compromised.

---

## 6. Scene model and Queue

### 6.1 Scene

A scene is a saved snapshot of:

- Which role lanes are enabled.
- Each lane's rhythm pattern, length, phase, ratio and event settings.
- Each lane's role/slice selection.
- Scene phrase duration.
- Scene transition/reset policy.
- Optional scene name and colour.

A scene may be a single lane or a full four-layer arrangement. This lets a simple 5/8 kick rhythm and a dense four-layer mathcore groove coexist in one queue.

### 6.2 Queue rules

The queue is deliberately simple:

1. Queue entries play from first to last.
2. Each entry is a scene.
3. A scene plays for its configured phrase duration.
4. At the scene boundary, the next queued scene becomes active.
5. After the final entry, playback returns to the first entry.
6. A one-entry queue repeats that one scene indefinitely.
7. Appending the same scene multiple times is allowed and means repeat it multiple times before advancing.
8. An empty queue does not start.
9. The queue does not branch, randomise its order or require conditional logic in v1.

The queue's phrase duration is explicit (initial options: 1, 2, 4 or 8 master bars). It must not be inferred from the longest lane cycle. This keeps transitions predictable even when lane cycles have different lengths.

### 6.3 Live queue editing

- Appending a scene while playing adds it to the tail immediately; it does not interrupt the current scene.
- Reordering, deleting or clearing while playing is committed at a safe scene boundary.
- The currently sounding scene is not cut off by a queue edit.
- If the current scene is removed from the pending queue, it finishes, then playback advances to the next valid entry.
- If the queue is cleared during playback, the current scene finishes and transport stops at its boundary.
- Stopping transport is always available immediately.
- The queue wraps to item 1 after the final item. There is no separate song-end state.

### 6.4 Requirements

| ID | Requirement | Priority | Acceptance |
| --- | --- | --- | --- |
| MM-Q-001 | Append a scene to the queue | P0 | One control action appends it |
| MM-Q-002 | Play queue sequentially | P0 | A → B → C → A → B → C |
| MM-Q-003 | Loop a single queued scene | P0 | A → A → A without stopping |
| MM-Q-004 | Allow duplicate queue entries | P0 | A → B → A is valid |
| MM-Q-005 | Configure phrase duration per scene | P0 | Scene switches at its defined boundary |
| MM-Q-006 | Queue edits while playing | P1 | Changes take effect safely without cutting the current scene |
| MM-Q-007 | Remove, reorder and clear entries | P1 | Operations preserve the remaining order |
| MM-Q-008 | Save / recall queue and scene definitions | P1 | Queue survives reboot and recalls deterministically |
| MM-Q-009 | Queue status on screen | P0 | Current, next and remaining entries are visible |
| MM-Q-010 | Empty queue protection | P0 | PLAY cannot start an empty queue |

---

## 7. FM-1 performance interaction

### 7.1 Preserve the existing key roles

**White keys 1–16 remain slice performance keys.** They continue to trigger slices and hold-to-loop exactly as they do today, including while MATHCORE is playing.

**Black keys 1–11 become scene queue shortcuts only while MATHCORE LIVE is active.** Outside MATHCORE LIVE, their existing performance-pad and effect-pad behaviour remains unchanged.

This is a mode-specific reassignment, not a permanent removal of the 11 black-key performance pads.

### 7.2 Instant queue gestures

| Gesture in MATHCORE LIVE | Action |
| --- | --- |
| Tap black key | Append its assigned scene to the queue |
| Tap same black key again | Append another copy |
| Hold black key | Preview the assigned scene without queueing (P1; only if it does not compromise live playback) |
| SEL + black key | Remove the most recently queued instance of that shortcut (P1) |
| Encoder / existing navigation | Select queue entry for inspection and editing |
| Existing PLAY | Start / stop queue transport |
| White key | Trigger its slice; hold to loop |
| Existing page / mode control | Leave MATHCORE LIVE and restore normal black-key pad behaviour |

The precise page entry/exit gesture must be reconciled with the current button map during implementation. Do not add physical controls or rely on undocumented button presses.

### 7.3 Black-key shortcut assignment

- Each of the 11 black keys can be assigned to a saved scene.
- Default assignment is the first 11 saved scenes.
- A key with no assigned scene is visibly inactive and does nothing.
- The key LED uses the scene colour; the display shows its letter/name.
- Shortcut assignment is setup work. During performance, tapping is append-only and immediate.

### 7.4 Screen layout

The MATHCORE LIVE screen should show, within the 240 × 240 display:

- Header: MATHCORE, tempo / external clock, transport.
- Four compact lane rows: role, pattern length, mute/solo and moving phase indicator.
- Queue strip: current scene, next scene and a compact representation of the remaining entries.
- Phrase progress and scene transition indicator.
- Black-key shortcut labels and white-key slice feedback.
- K1–K4 contextual labels for the selected lane or queue operation.

Do not turn the live screen into a tiny DAW piano roll. Queue building must remain possible by touch alone.

### 7.5 Setup pages

- **SLICE MAP:** assign slice roles.
- **LANE EDIT:** edit one lane's rhythm and slice selection.
- **SCENE EDIT:** combine lane patterns, set phrase length and save the scene.
- **MATHCORE LIVE:** append scenes with black keys, play the queue and perform slices with white keys.

Detailed editing is allowed to use existing encoders and K1–K4. Normal queue performance must not require menu navigation.

---

## 8. Persistence and migration

- Store role maps with the break/sample identity, not as one global role map.
- Store lane patterns and scene definitions in versioned pattern/scene objects.
- Store the queue separately from scene definitions so the same scene can be reused multiple times.
- Preserve the existing append-only settings strategy where possible.
- Do not reinterpret old kick/snare/hat synthesis settings as sample-role assignments.
- Old drum patterns must be migrated only where a deterministic sample mapping exists; otherwise retain compatible break-step data and initialise sample lanes safely.
- Keep the previous save readable until the new scene/queue save has been written and verified.
- A missing role slice must produce silence plus a warning, never a synthesized substitute.

---

## 9. Audio, memory and reliability constraints

- Reuse the existing sample voice and render path.
- All event scheduling is deterministic and bounded.
- No heap allocation, file I/O or UI work inside the audio render callback.
- Bound lane count, events per lane, queue length, active voices and per-block event count.
- Do not load four independent copies of the same break.
- Measure the actual firmware build's flash, static RAM, pool use and worst-case render time before raising lane or voice limits.
- Preserve MIDI clock input/output and transport start/stop semantics.
- Queue changes and scene changes must not create clicks, dropped samples or timing discontinuities.
- No Wi-Fi, cloud service, audio-input detection or external host is required for core operation.

---

## 10. Implementation roadmap

Built on the `mathcore` branch, 22 Nov 2026, released in v1.5.5 (§14 has what is done and what is not).

| Milestone | Scope | Exit criteria |
| --- | --- | --- |
| **M0 — Sample-only cleanup** | Remove synthesised kick, snare, hi-hat and synthesised fallback break from the user-facing product and audio path. Remove obsolete synth-drum lanes / MIDI drum mappings deliberately; preserve compatible break playback, FX and saves. | No synthesised drum or fallback voice is reachable; old saves load safely; sample-only regression tests pass. |
| **M1 — Slice Map** | Add per-slice role tags and per-break persistence; build SLICE MAP UI. | Manually assign KICK/SNARE/HAT/PERC; save, reboot and reload; unassigned role is silent and warned. |
| **M2 — Role-aware sample lanes** | Replace synth drum lanes with sample-event lanes that select from role-tagged slices. Start with four lanes. | Kick/snare/hat/perc lanes each trigger assigned sample slices; no synth voice; old break-step playback still works. |
| **M3 — Layered polymetric engine** | Independent lengths, phase, rotation, ratios, mute/solo and deterministic scheduling on one master clock. | 5/7/11/13-event lanes run simultaneously; 64-bar host test with no cumulative drift or audio underrun. |
| **M4 — Scene model** | Save a complete snapshot of active lane patterns, slice selection, phrase duration and transition settings. | A single-layer scene and four-layer scene both save and recall identically. |
| **M5 — Queue core** | Sequential scene queue, append, duplicate, wraparound, one-entry loop and safe boundary transitions. | A→B→C→A; A→A→A; edits do not cut current scene; empty queue cannot start. |
| **M6 — FM-1 instant controls** | MATHCORE LIVE screen, black-key scene shortcuts, white-key slice performance, queue feedback and setup pages. | Build and launch a multi-scene queue using only the device; white keys remain playable throughout. Existing black-key pad mode still works outside MATHCORE. |
| **M7 — Persistence and migration** | Versioned role maps, scenes, queue; save verification and old-save migration. | Upgrade from current firmware without corrupting break bank, FX or compatible pattern data. |
| **M8 — Release hardening** | Device tests, host scenarios, screen-reader labels, manual, emulator and release notes. | P0 acceptance tests pass; one-hour worst-case soak; real-device performance verified. |

### 10.1 Suggested implementation order

Do not implement the attractive queue UI first. The dependency order is:

1. Prove sample-only engine and remove synth drum dependencies.
2. Add slice roles and persistence.
3. Build role-aware lanes.
4. Prove simultaneous lane scheduling and voice budgets.
5. Add scene snapshots.
6. Add queue playback and wraparound.
7. Map the queue to the physical keys and finish the live screen.

This avoids building a polished queue interface on top of the wrong audio model.

---

## 11. Test plan and acceptance demo

### 11.1 Engine tests

- Role lookup returns only slices assigned to that role.
- Multiple roles may refer to the same slice.
- Empty role is silent and reports missing assignment.
- All four lanes trigger sample voices simultaneously.
- 5-, 7-, 11- and 13-event lanes maintain phase against one master clock.
- Mute / solo does not reset lane phase.
- Fixed seed reproduces the same random slice choices.
- Event density and voice count remain bounded.
- Queue A → B → C → A advances in order.
- A single-entry queue repeats A indefinitely.
- Duplicate A entries play twice before advancing.
- Queue append during playback does not interrupt the current scene.
- Queue clear during playback stops at the defined scene boundary.
- White-key manual triggers do not alter queue position.
- Black-key shortcuts append only in MATHCORE LIVE; normal pad behaviour is unchanged elsewhere.

### 11.2 Host scenarios

Add reproducible scenarios under `tests/scenarios/` for:

- Role assignment and role-map persistence.
- Four-layer scene with distinct cycle lengths.
- Queue A/B/C wraparound.
- Single-scene infinite loop.
- Live append and remove at a scene boundary.
- White-key slice trigger / hold-loop while queue is running.
- Black-key shortcut mapping and normal-mode regression.

Render WAV output and assert event timing where the host harness permits it. Add screenshots for SLICE MAP, LANE EDIT, SCENE EDIT and MATHCORE LIVE.

### 11.3 Device acceptance performance

Demonstrate the following without a computer:

1. Load a break and map kick, snare, hat and percussion slices.
2. Create four independent rhythms.
3. Run all four layers simultaneously.
4. Save three scenes: a basic groove, a variation and a dense mathcore scene.
5. Assign them to three black-key shortcuts.
6. Tap the three black keys to build A → B → C.
7. Press PLAY; the queue advances A → B → C → A automatically.
8. Queue only A; verify A loops indefinitely.
9. While it plays, use white keys to trigger and hold-loop slices.
10. Stop, save, reboot and recall the same scene and queue arrangement.

**Pass condition:** all audible rhythmic events are sample playback; layers remain synchronised to the master clock; the queue wraps correctly; white-key performance remains immediate; no existing non-MATHCORE black-key pad behaviour is lost.

---

## 12. Explicit non-goals for this feature

- Synthesised kick, snare, hi-hat or percussion voices.
- A melodic or synth-note sequencer.
- A conventional linear song arranger with dozens of tracks.
- Conditional/random queue branching in the first release.
- Automatic cloud analysis or network-dependent slice classification.
- Multiple independent decoded sample decks in the first release.
- New physical buttons or a touchscreen-only workflow.

---

## 13. Source references

- [Current repository](https://github.com/profikid/fm1-break-choppah)
- [Parent product PRD](PRD.md)
- [Latest inspected commit: AMEN tempo / loop correction](https://github.com/profikid/fm1-break-choppah/commit/006329c8cd5374ce11166b6861e962281fdfa883)
- [Break engine](../app/dsp/brk.c)
- [Break engine interface and existing key/pad definitions](../app/dsp/brk.h)
- [Current app glue and settings](../app/app.c)
- [Current kick voice interface](../app/dsp/kick.h)
- [Current snare / hi-hat voice interface](../app/dsp/perc.h)
- [Current manual](../app/docs/MANUAL.md)

---

## 14. Implementation status (22 Nov 2026, branch `mathcore`)

| Milestone | State |
| --- | --- |
| M0 Sample-only cleanup | Done. `dsp/kick.*`, `dsp/perc.*`, `ui/kick.c`, `ui/perc.c` removed; `brk_synth` moved to `tests/engine/` (test material only); a build without a bank is silent. The settings keep the drums' 188 bytes as reserved (`rsv_kick`, `rsv_perc`; offsets asserted), so older saves load. The MIXER's KICK and PERC faders became one LANES fader (appended `lane_level`) |
| M1 Slice Map | Done, automatic (§4.2a). MAP page read-only |
| M2 Role-aware lanes | Done. The pattern's KICK / SNARE / HAT lanes (SEQ, unchanged data) play the role's first slice; MIDI 36 / 38, 40 / 42, 44, 46 / 37, 39 play a KICK / SNARE / HAT / PERC slice. An empty role: silence and `mc_missing` (a "!" on the lane rows) |
| M3 Polymetric engine | Done. Four lanes a scene, 1–32 events, 1/32 … 1/4 and triplets, phase, rotate, chance (seeded), ratchet ×1–4, gate, level, accents, reverse, per-lane pitch (as speed), FIXED / ROUND / RANDOM / ALT / ACCENT, reset per scene or FREE, mute / solo. All from one integer timeline (12 ticks a 16th from the transport's start, the outside clock's when following): engine test 5/7/11/13 for 64 bars exact. Voices: two a lane (8), bounded by the lane count |
| M4 Scenes | Done. 16 scenes (A–P), 100 B each, `OBJ_MC` at 0xD6000; phrase 1/2/4/8 bars; the break's own loop under the lanes or not (`MCS_BREAK`) |
| M5 Queue | Done. Up to 64 entries, `OBJ_MQ` at 0xD8000 with the 11 shortcuts. Append, duplicates, remove last of a scene, clear (plays out, then stops), move at the cursor; edits never cut the scene playing; an empty queue doesn't start |
| M6 FM-1 controls | Done. EDIT = MATHCORE: LIVE → MAP → LANE → SCENE (EDIT again). §14.1 |
| M7 Persistence | Done. SAVE writes the two objects only when they changed; checked on load (magic, ranges) |
| M8 Hardening | Partly: engine tests (159 checks), host scenario `tests/scenarios/mathcore.txt`, manual, control map. Not yet: device acceptance (§11.3), the one-hour soak, a release |

Not built: hold a black key to preview its scene (P1), per-event pitch / reverse locks (per lane only), scene
colours (letters A–P), lanes of more than four.

**Scene names** (1.7): a scene's name is a byte, an index into `MC_NAME` (app.c) and `NAMES` (app/site/mathcore.js,
held equal by tests/mathcore.test.mjs), so OBJ_MC and a pack's SCNE keep their size and magic. The list only grows at
the end, never reordered: 1–16 are the factory's, 17–53 added in 1.7 for the polyrhythms and more (HEMIOLA, POLY 3/2,
4/3, 5/4, 7/4, 5/3, 7/8, AMEN 3/5, PRIME MATH, PHASE DRIFT, GHOST ROLLS, SHUFFLE VS 16, 3/4 IN 4/4, 6/8 STEPPER, AMEN IN
5S, HALFTIME 7, TRIPLET HATS, JUNGLE, TWO STEP, HALFTIME, DOUBLE TIME, SHUFFLE, GHOST NOTES, BREAKBEAT, BROKEN BEAT, FOUR
TO FLOOR, MINIMAL, STUTTER, GLITCH, REVERSE, CUT UP, HAT ROLL, KICK FILL, SNARE FILL, BUILD, INTRO, OUTRO). At most 14
letters (LIVE's name line). An older CHOPPAH shows a name it doesn't know as blank (the scene plays the same). The names
with numbers have their words in app/words.tsv. Names are picked on choppah.app (the MATHCORE editor, the MCP tools),
not on the FM-1.

### 14.1 The controls as built (§7.2 reconciled with the panel)

| Where | Control | Does |
| --- | --- | --- |
| LIVE | Black key · SEL + black key | Append its scene · take its last entry out |
| LIVE | PLAY (stopped) | Start the queue at entry 1 (empty: "QUEUE EMPTY", nothing starts) |
| LIVE | K1–K4 | KICK / SNARE / HAT / PERC lane: left toward MUTE, right toward SOLO |
| LIVE | ALGORITHM · PRESETS · SEL + REC | Queue cursor · move the entry at it · clear the queue |
| MAP | White keys · ALGORITHM | Play and show a slice (its roles) |
| LANE | ALGORITHM · PRESETS | The lane · its knob set (RHYTHM, SOUND, FEEL, MORE) |
| LANE | White keys (SEL: 17–32) | Its events on / off; on FEEL its accents |
| LANE, SCENE | PLAY (stopped) | The scene edited, round and round |
| SCENE | ALGORITHM · K1–K4 | The scene · PHRASE, BREAK, NEW / COPY, LANES |
| SCENE | Black key · SEL + black key | That key queues this scene · queues nothing |
| Everywhere | — | The queue plays on across pages; the black keys are the pads everywhere but LIVE and SCENE (and ARP's effects) |

The lanes are a channel of their own (`brk_lanes_out`): the MIXER's LANES fader and GROOVE's route (through the FX
chain or around it), not the break's DJ filter, gate pad or tape stop. Flash: image 552 KB of 581.5 KB (+3.5 KB over
v1.5.2 with the drums gone); RAM .data+.bss 91.2 of 98.3 KB.

---

## 15. Revision: an instrument, not a programmer (22 Nov 2026, evening)

Laurens, after the first build: too much programming. Every scene should already be there (beats, risers, fills, of
different lengths), and complex polyrhythms should be playable without programming, as an instrument, and look more
visual. What changed (on the same branch):

| ID | Requirement | State |
| --- | --- | --- |
| MM-P-001 | **Scenes come filled**: A ROLLER, B AMEN, C STEPPER, D MATH 5/7, E FILL, F CHOP FILL, G RISER, H BREAKDOWN, I DOUBLE, J 7/8 BOUNCE, K DROP; L–P grooves with the break under them. Phrases of 1, 2 and 4 bars; lanes of 1–32 events at 1/32 … 1/4 and triplets (`MC_PRESET`, app.c: each lane written as a step string). Store magic MCS2: the first build's scenes give way to these | Done |
| MM-P-002 | **Black keys launch** (replacing append-to-queue): a tap plays the scene (stopped: at once; a groove on the next bar, the groove from then on; a ONCE scene on the next beat, counted from its bar so it ends on the bar line, then the groove by itself). Held past 350 ms: momentary, the groove back on the next beat after. `brk_mc_jump(scene, quant)`; `MCS_ONCE` | Done; engine test test_mc_play |
| MM-P-003 | **Four macros on LIVE's K1–K4**, over every lane: DENSITY (fewer hits, the off-beats first; more, the added hits fixed per cycle position so they groove), MATH (seven steps from the lanes' own lengths to 5 / 7 / 11 / 13, `MC_MATH_LEN`), SPEED (half / normal / double), CHAOS (random slices, ratchets, reversals, seeded). Saved in the settings (`mc_macro`, appended). Mute / solo moved to SEL + K1–K4 | Done |
| MM-P-004 | **Risers**: a lane's RISE (LANE MORE K3): over the scene's phrase its rate halves each quarter (to 1/64), it swells from 35 % and climbs RISE semitones | Done; 8, 16, 32, 64 hits a bar in the engine test |
| MM-P-005 | **The wheel**: LIVE draws a ring per lane (kick inside, hat outside), its cycle's steps round it, the hits lit, the step playing white, turning at ~25 frames a second; the scene in the middle; the 11 scenes as a grid (playing lit, next outlined, ONCE marked), the name, what's next, the phrase, the chain. The black keys glow where they have a scene, the playing one lit, the next blinking | Done |
| MM-P-006 | The queue becomes the **chain**: SEL + a black key adds, REC + a black key takes the last of it out, SEL + REC clears (the groove plays on), ALGORITHM / PRESETS point and move. PLAY never refuses: an empty chain plays the groove | Done (replaces MM-Q-001 and MM-Q-010's "empty queue doesn't start") |
| MM-P-007 | SCENE K4 is PLAYS (LOOP / ONCE); LANE MORE K3 is RISE (RESET / FREE left the panel, kept in the engine) | Done |

Flash after the revision: image ~556 KB of 581.5 KB.

---

## 16. Revision: one voice, no overlay (22 Nov 2026, late)

Laurens, after the played build: the lanes laid over the break playing were too messy; it should be better
integrated, the sequencer too. Changed (same branch):

| ID | Requirement | State |
| --- | --- | --- |
| MM-V-001 | **A scene re-cuts the break on its own voice.** The lanes no longer have voices of their own (`mv` gone, `brk_lanes_out` gone): each tick the lanes' events are weighed (an accent first, then the snare, the kick, the perc, the hat) and one cuts the break's voice to its slice (`chop_hit`). Pads, the gate, the DJ filter, KEEP LENGTH pitch and the key loops act on it as on the loop. A key held or a pad holding the voice keeps it | Done; engine test: only the roles' slices sound under a scene |
| MM-V-002 | **RUN ON** (was BREAK, `MCS_BREAK`): a hit runs on into the break until the next (a re-cut break); off, each hit stops with its slice (dry chops). Grooves A-D, H, I, J run on; fills, riser, drop and L-P are dry. Store magic MCS3 | Done |
| MM-V-003 | **The sequencer integrated**: SEQ's KICK / SNARE / HAT lanes put the role's slice in the loop's place on their steps (one voice: snare, then kick, then hat), instead of a voice on top. While a scene plays, the pattern rests | Done; engine test: step 7's own slice replaced by the kick, the loop's own after |
| MM-V-004 | MIDI drum notes cut the break's voice to the role's slice | Done |
| MM-V-005 | The MIXER's LANES fader and the lanes' route are gone (nothing to mix: one voice); their settings bytes stay reserved | Done |

---

## 17. Revision: natural variation (23 Nov 2026)

Laurens: the patterns should sound more natural, more variation in the hats and the snares.

| ID | Requirement | State |
| --- | --- | --- |
| MM-N-001 | **VARY** per lane (0–100, `mc_lane_t.vary`, LANE FEEL K4): hats: velocity per hit (off-beat 16ths softer), other hat slices, an off-beat left out, a little roll (×2), off-beats laid back up to a tenth of a 16th; snares and perc: ghost notes next to hits (a drag, a bounce) and on off-beats, soft (30 %) and laid back, a flam on an accent, other snare slices; kick: a pickup before a kick (60 %). Up by half plus 20 in a phrase's last bar. Seeded per event (`mc_hash`): the same start, the same variations; ghosts rank below every real hit for the one voice | Done; engine test: ghosts round 5 and 13, identical twice |
| MM-N-002 | The presets: VARY by role (hat 70, snare 60, perc 45, kick 20; ONCE scenes half, risers 0); hat lines with accent shapes instead of flat 16ths, the AMEN hats round-robin instead of random, snares alternating between the break's snare hits. Store magic MCS4 | Done |
| MM-N-003 | LANE MORE: ROLE, RISE, REVERSE, LANE (SEED left the panel; VARY's seed is the lane's own) | Done |

---

## 18. Revision: fills and triplet snare rolls (23 Nov 2026)

Laurens: better fills, rollers with triplet snares, and one with more snares.

| ID | Requirement | State |
| --- | --- | --- |
| MM-F-001 | **The scenes reworked** (`MC_PRESET`, one per scene, store magic MCS5). On the keys: A ROLLER, B AMEN, C SNARE ROLLER (triplet hats, a triplet snare roll closing every second bar, a pickup roll), D SNARES (snares on most 16ths round all the break's snare slices, a triplet roll every bar), E MATH 5/7, F FILL, G TRIPLET ROLL, H SNARE RUSH, I CHOP FILL, J RISER, K DROP; L STEPPER, M DOUBLE, N 7/8 BOUNCE, O BREAKDOWN, P TRIPLET ROLLER (all triplets) | Done |
| MM-F-002 | **Triplet rolls**: a SNARE lane at 1/8 with ratchet ×3 (each hit three 1/16T snares); the PERC lane can take another role (`prole`) so a scene has two snare lanes | Done |
| MM-F-003 | **A roll owns the voice** until its event ends (`mr_hold`): a lighter hit (the hats) at the ratchet's ticks is dropped, not cutting the roll; a roll beats a single hit of the same weight | Done; engine test: hats at 1/16T never cut a bar of triplet snares |
| MM-F-004 | **SWELL** (`MCL_SWELL`): a lane's RISE climbs and swells over the phrase without speeding up (F FILL, G TRIPLET ROLL); the fills keep their climax in their second half so a late launch still lands; rolls and risers keep a third of their VARY | Done |

---

## 19. Revision: no MAP page, LIVE redesigned (23 Nov 2026)

Laurens: remove the MAP page (it's automatic anyway) and put the K S H P letters on the home screen; redesign LIVE
(the scene letters only once, below, with all the interaction); visual feedback when turning DENSITY, MATH and CHAOS.

| ID | Requirement | State |
| --- | --- | --- |
| MM-L-001 | MAP removed: EDIT cycles LIVE, LANE, SCENE. The Slice Map's letters (K white, S amber, H and P grey) are at the top of each slice block on HOME and every page with the blocks (`draw_blocks`, screen.c) | Done |
| MM-L-002 | The black keys' scenes only in the foot's pads row (`mc_draw_pad`): the scene letter, playing lit, held white, next outlined and blinking on the beat, ONCE marked underneath; the right-hand grid removed | Done |
| MM-L-003 | The right side: the scene and its name, the phrase, what's next, the four macros' gauges (DENSITY from the middle, MATH's seven steps, SPEED's three boxes, CHAOS a jagged bar; the one turned lit for 1.5 s), the chain | Done |
| MM-L-004 | The macros on the wheel: DENSITY's added hits white (flashing while turned) and dropped ones outlined, through `mc_event_kind` (dsp: the same decision as the render); MATH's ring lengths; CHAOS shakes the rings and throws sparks; SPEED's trail and an x2 / 1/2 badge. The wheel redraws at 25 fps while playing, while CHAOS is up, and while a gauge is lit | Done |

---

## 20. Revision: the pages carry the music; the gate (23 Nov 2026)

Laurens: switching between HOME and EDIT needs a stop and a start; it should go by itself, smoothly. And the gate
doesn't work with MATHCORE: everything should work together.

| ID | Requirement | State |
| --- | --- | --- |
| MM-T-001 | While playing, EDIT (from any page, MATHCORE not playing) brings the scenes in on the next bar: LIVE the chain from its top or the groove, LANE / SCENE the scene edited (`mc_follow_page`, ui/mc.c; `brk_mc_jump`). HOME (PERFORM, GENERATOR, SPLIT) and SEQ bring the loop and the pattern back on the next bar (`brk_mc_jump(-1, 16)`). FX, MIXER, ARP, GLOBAL, TAP keep what plays. Never a stop | Done; scenario mathcore.txt; no silent 16th across the switches (simulator) |
| MM-T-002 | HOME's GATE (`p_gate`) applies to a scene's hits as to the loop's slices, times the lane's own GATE (`chop_hit`) | Done; engine test: at 25 % each hit's tail silent |

