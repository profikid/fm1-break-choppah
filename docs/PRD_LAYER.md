# CHOPPAH — The Layer: overlay loops and one-shots on ARP

**Feature PRD** · v0.1 · 26 Nov 2026 · Owner: Laurens (Sylverline)  
**Parent PRDs:** [PRD.md](PRD.md) (ARP, B-55 effect pads) · [PRD_PACKS.md](PRD_PACKS.md) (§7 the overlay, §10 the hub)  
**Status:** proposed, nothing built. Decisions so far in §1.2 (later on 26 Nov 2026: D5, the pack's own effect pads); open questions in §10.

---

## 1. Purpose

A pack today is breaks and at most one overlay loop that runs under them from PLAY to STOP, with a fader and nothing
else. This PRD makes the overlay something you **play**: a pack holds **up to 4 loops** and **up to 16 one-shots**
beside its breaks (the **layer**), and the **ARP page** plays them: the loops on the black keys (tap to latch, hold to
play while held), the one-shots on the white keys, the effect pads on the black keys that are left. On the hub every
sample gets a **type** when it is uploaded (break, one-shot, loop, track), and the pack maker puts each where its type
says.

The first users are the ragga packs of 26 Nov 2026: their shouts (BOMBACLOT, JUNGLIST, SELECTAH …) are cut as 2-bar
breaks today, one per slot, ten a pack. As one-shots they sit sixteen to a pack beside the breaks they belong with,
and fire over the break without leaving HOME (LY-024).

### 1.1 Principles

1. **The break stays the instrument.** The layer plays *with* the break; nothing about HOME, MATHCORE or SEQ changes
   for a pack without a layer.
2. **On the bar, never adrift.** Loops follow the transport (`bar mod length`, PRD_PACKS §7) whether latched or held:
   a loop let in mid-phrase comes in where it would have been.
3. **Tap or hold, one gesture.** A short press latches, a long press plays while held. No mode to remember.
4. **Old packs and old firmware keep working.** A 1.7 FM-1 plays a layer pack's first loop as the overlay it knows and
   skips the rest; a pack without a layer plays on 1.8 exactly as on 1.7.
5. **The flash decides.** Breaks, loops and shots share the 344 KB: the builder and the hub show what fits before
   anything is made.

### 1.2 Decisions (26 Nov 2026, Laurens)

| # | Decision |
| --- | --- |
| D1 | The layer is played on the **ARP page** (not a page of its own) |
| D2 | A pack can hold **one-shots and overlay loops** beside its breaks |
| D3 | Loops **hold**: playing while a key is held is a first-class gesture, beside latching |
| D4 | On the hub a sample's **type** is chosen when it is uploaded: break loop, one-shot, overlay loop, … (§8) |
| D5 | **Each pack carries its own effect pads** (all 11: effect, settings, mix, MOD), so a pack plays with the pads it was made for (§3.6) |
| D6 | **Voices are a GLOBAL setting**, the fallback by default: GLOBAL's fourth part LAYER, K1 LOOPS 1–2 (default 1), K2 SHOTS 1–4 (default 3), K3 HOLD 100–600 ms (the tap / hold line, default 250). Raised once CPU is measured on the FM-1 (Q1) |
| D7 | **A shot can CUT the break** (a fill) or play OVER it (a shout): per shot, its K4 BREAK. While a CUT shot sounds the break (and the split's layer) is silent, the loops and the chain go on; it comes back with a 5 ms fade when the shot ends |

### 1.3 As built (firmware 1.8, branch `layer`, 26 Nov 2026)

Built: LY-001, 002, 005 (with D6's limits), 006, 007, 010, 011, 012, 013, 020, 023, 024, 030 (no reader words yet),
036 (as below), 037, 038, 050–052, 060–062, D7. The image: 577,804 of 581,564 B (**3.7 KB left**); RAM .data+.bss
95,692 of 98,304 B; `.ram_text` 17,284 of 32,768 B. Not yet: LY-003, 004, 014, 021, 022, 025, 035, 039–043, 054,
063, the hub (§8), the screen reader's words.

| As built | |
| --- | --- |
| Loop knobs | MAIN: K1 LEVEL, K2 FILTER (LP ← 0 → HP), K3 QUANT (NOW / BEAT / BAR / LOOP), K4 AUTO. MORE: K1 BARS (the window), K2 FROM (its start bar) |
| Shot knobs | MAIN: K1 LEVEL, K2 PITCH (±24), K3 DECAY (OFF … 30 ms), K4 BREAK (OVER / CUT). MORE: K1 MODE (ONE / GATE / LOOP), K2 CHOKE (0–4), K3 QUANT (NOW / 1/16 / BEAT) |
| Levels | MIXER K2 LOOPS (the old OVERLAY fader, `lane_level`), K3 SHOTS (`set.shot_level`) |
| Keys | black 1–4 (L1–L4) and the white keys (S1–S16) on ARP; SEL + a loop key: off at once; hold ARP on any page: ARP's keys while held |
| Kept per pack | `OBJ_MQ` grown (1.7's 728-byte saves load into its front): the loops' and shots' settings and the pack's pads, by pack id |
| Tests | `./build.sh test` (test_layer), `tests/chpk.test.mjs` (the layer), `tests/layer.sh` (host: shots, CUT, GATE, tap / hold, hold-ARP, a decay edit through SAVE + reboot, pack pads), pack_v2 / pack / custom_break at INFO 5 |

---

## 2. What a pack holds

| Part | Count | Length | Plays from | Voices |
| --- | --- | --- | --- | --- |
| Breaks | ≤ 16 | 2 bars (`CH_PCM_MAX`) | RAM (decoded at the bar swap, as today) | the break's own |
| Loops | ≤ 4 (L1–L4) | 1–16 bars | flash, streamed (`brk_overlay`, PRD_PACKS §7) | ≤ 2 at once |
| One-shots | ≤ 16 (S1–S16) | ≤ 4 s | flash, streamed (forward only) | 4 |

**Flash** (mono IMA ADPCM, 4 bits: `rate / 2` bytes a second). A worked pack at 172 BPM:

| Part | Rate | Bytes |
| --- | --- | --- |
| 4 breaks × 2 bars | 22.05 kHz | ~124 KB |
| 2 loops × 4 bars | 16 kHz | ~89 KB |
| 16 shouts × 0.8 s | 16 kHz | ~102 KB |
| **Total** | | **~315 KB of 344** |

Vocals and stabs sound right at 16 kHz and mostly at 11.025; the builder picks the best rate that fits for each
part, shots first down to 11.025, then loops (§7, LY-071).

---

## 3. The ARP page

ARP keeps its effect pads (B-55) and gains the layer. Which keys do what depends on the pack:

| The pack has | White keys 1–16 | Black keys 1–4 | Black keys 5–11 |
| --- | --- | --- | --- |
| no layer | slices (as today) | effect pads 1–4 | effect pads 5–11 |
| loops only | slices | **loops L1–L4** | effect pads 5–11 |
| shots only | **one-shots S1–S16** | effect pads 1–4 | effect pads 5–11 |
| loops and shots | **one-shots S1–S16** | **loops L1–L4** | effect pads 5–11 |

Fixed positions, so the hands learn them: L1–L4 are always black keys 1–4 (F#3 G#3 A#3 C#4), a loop key without a
loop is dark and silent, a shot key without a shot too. Black keys 5–11 play the **pack's** effect pads 5–11 (§3.6);
pads 1–4 come back when the pack has no loops.

```
 black:  [L1] [L2] [L3]   [L4] [FX5]   [FX6] [FX7] [FX8]   [FX9] [FX10]   [FX11]
 white:  [S1] [S2] [S3] [S4] [S5] [S6] [S7] [S8] [S9] [S10] [S11] [S12] [S13] [S14] [S15] [S16]
```

### 3.1 Loop keys (L1–L4)

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-001 | **Tap** (pressed < 250 ms) **latches** a loop: on at its quantise point (§3.3 MORE: beat, bar or its length; default bar), tap again: off at the same point. Queued, its LED and its row blink | P0 | Host scenario: a tap mid-bar starts on the next bar's 1; a second tap stops on the following 1 |
| LY-002 | **Hold** (≥ 250 ms) plays it **while held**: in at once **in phase** (where the transport says it would be, a 5 ms fade), out on release (5 ms). A latched loop held: plays on while held, stops on release (a "cut" gesture) | P0 | Host scenario: held from bar 3 beat 2 of an 8-bar loop, it sounds its own bar 3 beat 2; the release is click-free |
| LY-003 | **SEL + a loop key**: the loop restarts from its bar 1 at the next bar (a drop) and latches | P1 | |
| LY-004 | **Groups**: each loop has a group (A–D, default each its own). Latching a loop stops the playing loop of its group at the same quantise point (a clip-launcher column: verse bass → chorus bass) | P1 | Two loops of group A never sound together |
| LY-005 | **At most 2 loops at once** (two streaming voices; GLOBAL LAYER K1, 1 by default, D6). A third latched takes the voice of the oldest latched (it stops on the same quantise point), a held one the oldest of all | P0 | |
| LY-006 | **AUTO start** (per loop, default on for L1 when the pack came from a 1.7 overlay, off otherwise): the loop latches itself when PLAY starts, so an overlay pack plays as on 1.7 | P0 | A 1.7 overlay pack on 1.8: the overlay sounds from PLAY with no key pressed |
| LY-007 | PLAY's stop and HOME + PLAY stop every loop; TAPE STOP, HALF and STOP reach the loops as they reach the overlay today (PO-004) | P0 | |

### 3.2 One-shot keys (S1–S16)

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-010 | A white key fires its shot. Quantise per shot (§3.3 MORE): OFF (at once, the default), 1/16 or a beat | P0 | |
| LY-011 | **Modes** per shot: ONE SHOT (plays to its end; the key again restarts it), GATE (stops on release, 5 ms), LOOP (repeats its length while held) | P0 | |
| LY-012 | **Choke groups** (0 none, 1–4): a shot cuts the others of its group (5 ms), so two shouts never pile up | P1 | |
| LY-013 | **4 voices** (GLOBAL LAYER K2; 3 by default, D6); one more hit takes the oldest. The overload guard (E-7) sheds shot voices first (oldest first), then the effect pads, then the chain | P0 | Engine test: 16 hits a beat, render bounded |
| LY-014 | **SEL + a white key** plays the slice instead, so the break stays playable on ARP | P1 | |
| LY-015 | Velocity sets the level (`APP_TOUCH`, when it works on the device) | P2 | |

### 3.3 The knobs and encoders

The key pressed last (a loop, a shot or an effect pad) is the one shown and edited, as B-55 does for the pads today.
**ARP again** = its next part.

| Shown | Part | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- |
| Loop | MAIN | Level | Filter (LP ← 0 → HP) | Pitch (±12; 0 = follows the tempo) | Route (through the chain / around it / dub send) |
| Loop | MORE | Length (a window of ½–16 bars on its bars) | Start bar | Quantise (beat / bar / its length) | Group (A–D) · AUTO with SEL |
| Shot | MAIN | Level | Pitch (±24) | Decay (∞ … 30 ms) | Route |
| Shot | MORE | Start trim | Mode (ONE SHOT / GATE / LOOP) | Choke (0–4) | Quantise (OFF / 1/16 / beat) |
| Effect pad | MAIN · MORE · MOD | as B-55 | | | |

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-020 | ALGORITHM walks the shown key through all of them (L1–L4, the pads, S1–S16) without playing them | P0 | |
| LY-021 | PRESETS on a pad: its effect (as today); on a loop or shot: which of the pack's loops / shots that key plays (two keys may play one sample with their own settings) | P1 | |
| LY-022 | SEL + a knob: back to the pack's value (as LOOP's settings, PE-005) | P1 | |
| LY-023 | SEL + ARP stays randomize (B-42's undo with it) | P0 | |
| LY-024 | **Hold ARP on any page** and the keys are ARP's keys while it is held: on HOME with the break running, hold ARP, hit S3, let go, back on the slices. ARP pressed and let go without a key changes page as today (on release) | P0 | Host scenario: HOME, ARP held + S3 fires the shot, the page stays HOME |
| LY-025 | MIDI: notes 64–79 (E4–G5) fire S1–S16 (velocity = level), notes 80–83 toggle L1–L4 (note on = tap, held over 250 ms = hold) on any channel, as A-75 does for slices | P1 | |

### 3.4 The screen

```
┌ RAGGA JUNGLE        172.0   3.2 ┐  header: pack, tempo, bar.beat
│ L1 BASS   8b ●────────○───── ▓▓ │  four loop rows: name, bars, a phase bar with its playhead,
│ L2 VOX    4b ◌ NEXT BAR         │  state (on / held / queued / off), its meter
│ L3 PAD    8b ·                  │
│ L4 —                            │
├─────────────────────────────────┤
│ the shown key: a loop's bars, a │  the view (as B-55's FX view): a loop's waveform over its bars
│ shot's waveform, trim and decay │  with the window and the playhead; a shot's with its trim and
│                                 │  decay; an effect pad: the FX view as today
├─────────────────────────────────┤
│ L1 L2 L3 L4 F5 F6 F7 F8 F9 F10 F11 │  the pad strip: the black keys (B-55's, with the loops)
│ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ ▪ │  sixteen shot boxes, lit on a hit, their choke colour
│ LEVEL  FILTER  PITCH  ROUTE     │  K1–K4 of the shown key
└─────────────────────────────────┘
```

Drawn in strips of ≤ 60 rows (the 240 × 60 canvas). Key LEDs: dim = loaded, lit = playing, blinking = queued, off =
empty; the effect pads as B-55.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-030 | The page as drawn above; the screen reader speaks the shown key (`L1 BASS LATCHED`, `S3 BOMBACLOT`) | P0 | `tools/sr_audit.sh` clean; `tools/doc_shots.py` pictures; the control map's `PAGES` |

### 3.5 Effect pads and the layer

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-035 | Each effect pad gets a **TARGET**: MIX (today's: the whole mix after the chain), LAYER (the loops and shots only), BREAK (everything but the layer) | P1 | Engine test: a LAYER low-pass leaves the break untouched |
| LY-036 | The MIXER's K2 OVERLAY fader becomes the **loops'** level (LOOPS); K3 the **shots'** level (SHOTS, a settings byte appended) | P0 | |

### 3.6 The pack's effect pads (D5)

A pack carries the 11 effect pads it was made for: each pad's effect, its three settings, its mix, its MORE and MOD
(`brk_fxslot_t` + `brk_fxx_t`, as `set.pfx[]` / `set.pfxx[]` hold them) and its TARGET (LY-035). Loading the pack's
break loads its pads, as it loads its theme and its scenes (PRD_PACKS principle 1).

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-037 | When the pack changes, ARP's pads become the pack's (`PADS` chunk, §6). A pack without `PADS` (any 1.7 pack, the built-in bank) plays the **FM-1's own pads** (`set.pfx[]` as saved today); going back to such a pack brings the FM-1's own back. Latched pads let go when the pads change | P0 | Host scenario: pack A's pad 6 is a REVERB, pack B's a CRUNCH; switching packs switches the pad; a pack without PADS gets the saved pads |
| LY-038 | Pads edited on the FM-1 (PRESETS, K1–K4, MORE, MOD) while a pack with pads is on are kept **for that pack** (by pack id, beside its scenes and LOOP settings: `OBJ_MQ` grown, or an object of its own if it doesn't fit 3840 B), never written over the FM-1's own; SEL + a knob: back to the pack's value | P0 | Edit pack A's pad, switch to B and back: A's edit is there, B's pads untouched |
| LY-039 | GETSET / PUTSET carry the pads, so **Pull edits** (PE-003) takes the FM-1's pad edits into the pack and the hub's editor can send pads live (PH-060) | P1 | `tests/pack_v2.sh`: a pad edited on the host comes back in GETSET byte for byte |
| LY-03A | With loops in the pack (black keys 1–4 are L1–L4), the pack's pads 1–4 are kept, unheard; the builder and hub say so when pads 1–4 are set in a pack with loops | P2 | |

---

## 4. Recording the layer

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-040 | The looper (REC) records shot hits and loop taps like slice hits, and plays them back | P1 | |
| LY-041 | A **SHOTS lane** in SEQ: a step holds a shot (1–16) or none; its hits quantised to the step | P1 | |
| LY-042 | Loop launches in a pattern: a pattern may latch loops at its start (pattern settings) | P2 | |
| LY-043 | MATHCORE lanes may pick shots (a SHOT role beside K S H P) | P2 | |

---

## 5. The engine

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-050 | `brk_overlay` grows to **2 loop voices** reading the pack's loop table; each keeps its own phase (`bar mod its length`), window (length, start bar), level, filter, pitch and gate (latch / hold, the 5 ms fades) | P0 | Engine test: two loops of 4 and 8 bars, each on its own bar after 24 bars |
| LY-051 | In-phase entry: seek to the nearest ADPCM mark (every 4096 samples, as today) and decode on to the transport's position, within the block it starts in | P0 | Engine test: a held loop's first sample = the always-on loop's at that position |
| LY-052 | **Shot voices** (`brk_shot`, platform-free, in `app/dsp/`): 4 voices reading ADPCM forward from flash, Hermite resampled to 44.1 kHz with pitch, a decay envelope, mode, choke; state ~64 B a voice in `.bss`, nothing in the pool | P0 | Engine test: a shot at +12 lasts half as long; a choke cuts within 5 ms |
| LY-053 | **Render cost**: 2 loops + 4 shots + the full chain under 85 % (PO-002 measured first: one overlay on the device). If it isn't, the limits drop (1 loop voice, 3 shots) before anything ships | P0 | Measured on an FM-1 against 1.7 |
| LY-054 | A flash write (SAVE, a pack arriving) fades loops and shots out first (PO-006) | P0 | |
| LY-055 | Code: ~6–9 KB of flash expected against ~19 KB left (1.7: 562 KB of 581.5). Measure the image line; if it doesn't fit, ask Laurens what leaves | P0 | `./build.sh` image line |

---

## 6. The pack format

Bank v2's chunk directory (PRD_PACKS §3.2) takes three new chunks; unknown tags are skipped, so 1.7 reads a layer pack
as it reads any v2 pack.

| Tag | Content |
| --- | --- |
| `OVLY` | unchanged: **loop 1**'s header and ADPCM. 1.7 plays it as its overlay |
| `LOOP` | `n` (≤ 4), then a 32-byte record per loop: `name[8]`, `rate`, `len`, `bpm_x100`, `bars`, `group`, `flags` (AUTO, quantise), `level`, the default MAIN/MORE settings, the offset of its ADPCM (loop 1's: 0 = the OVLY chunk's own); loops 2–4's ADPCM follows the records |
| `PADS` | the 11 effect pads (D5, §3.6): per pad its `brk_fxslot_t` (20 B: type, on, TARGET in `rsv[0]`, p[3], mix) and `brk_fxx_t` (36 B: MORE and MOD), 616 B. Without it: the FM-1's own pads |
| `SHOT` | `n` (≤ 16), then a 32-byte record per shot: `name[8]`, `rate`, `len`, `level`, `pitch`, `decay`, `mode`, `choke`, `quant`, the offset of its ADPCM in the chunk; the ADPCM follows the records (trimmed, faded and levelled by the builder: start = sample 0) |

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-060 | `chpk.js` writes and reads LOOP, SHOT and PADS; `packLayout` places them over both banks (a chunk never spans a bank) | P0 | `tests/chpk.test.mjs`: a pack with 3 loops and 16 shots round-trips byte for byte |
| LY-061 | The firmware reads them (`pk_chunks`, `brk_bank.h` `bank_loop_t`, `bank_shot_t`, `bank_pads_t`, `_Static_assert`ed; PADS's floats as the FM-1 stores them, little-endian IEEE 754) | P0 | `tests/pack_v2.sh` grows a layer pack |
| LY-062 | **INFO version 5** says the FM-1 plays the layer; `installImages` sends LOOP, SHOT and PADS to 5 only (a 1.7 gets OVLY alone, said in the install dialog, PH-031) | P0 | `tests/fm1link.test.mjs` with the fake FM-1 at 4 and 5 |
| LY-063 | The FM-1's own edits to loops and shots (their settings, LY-022) are kept by pack id (`OBJ_MQ` grown: 4 × 16 + 16 × 16 B) and come back by GETSET / go by PUTSET, so Pull edits (PE-003) takes them | P1 | |

---

## 7. Flash budget and the room bar

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-070 | The room bar (builder, hub pack page, PH-044) splits the 344 KB into breaks / loops / shots / free, per bank | P0 | |
| LY-071 | What doesn't fit is refused before anything is made, with what would make it fit: "shots at 11 kHz: fits", "L2 at 4 bars: fits" | P1 | |

---

## 8. The hub: a type for every sample

### 8.1 The types

| Type | What it is | Becomes in a pack | Its editor in the library |
| --- | --- | --- | --- |
| **BREAK** | a drum loop | a break (2 bars, sliced) | today's slicer (PH-040): tempo, bars, hits / grid, warp, drums only |
| **ONE-SHOT** | a hit, a shout, a stab, an FX | a shot | trim (start and end found at −48 dB), fade-out, level (−16 dBFS RMS as the breaks, peaks under −1 dBFS), name (8 letters), mode, choke, rate (22.05 / 16 / 11.025, default 16 kHz) |
| **LOOP** | a bass line, a pad, a vocal bed | a loop | bars on its grid (1–16), start bar, the rest (drums out) or the mix, seam crossfade, level, rate |
| **TRACK** | a whole tune | cut into breaks and a loop, as `pack_from_loop` does today | the bar grid, the loop finder's cards |

### 8.2 Choosing it

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-080 | **On upload** each file gets a type: the library's drop shows a type picker per file, set to the hub's guess; the MCP's `sample_upload` and `sample_upload_link` take `type` | P0 | choppah-hub test/library_ui.mjs |
| LY-081 | **The guess** (beats.js, as the file is heard): under ~1.2 s, or one main hit and a decay → ONE-SHOT; 1–2 bars with a high drum share → BREAK; ≥ 4 bars with a low drum share or a held pitch → LOOP; over ~60 s → TRACK. Shown as a suggestion, never silently final | P0 | The DOA folder's ragga shouts guess ONE-SHOT, its reggae drum loops BREAK |
| LY-082 | The type **can change** later (library, `sample_update`); the sample's edit for its new type starts from the guess (`auto`) | P0 | |
| LY-083 | The library filters and sorts by type; each sample shows its type's badge and its editor (§8.1) | P1 | |

### 8.3 Packs from typed samples

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| LY-090 | **New pack / Add to** puts each sample where its type says: breaks into slots, loops into L1–L4, shots into S1–S16 (in the order chosen); the MCP's `pack_from_samples` takes them typed, or as `breaks`, `loops`, `shots` lists | P0 | |
| LY-091 | The pack page shows three sections (breaks, loops, shots): play a shot by clicking, a loop with the breaks' preview transport; reorder within a section, move a sample between sections (it is cut again for its new place) | P1 | |
| LY-092 | Today's overlay (`pack_overlay`, `makeOverlay`) becomes **L1** of the pack: same cut, same rate rules (PB-011, PB-012) | P0 | An overlay pack made before 1.8 opens with its overlay as L1, AUTO on |
| LY-093 | MCP parity (PRD_PACKS §10.11): `pack_get` lists loops and shots with their settings; tools for a loop's and a shot's settings (as `pack_break_settings`) and their order (as `pack_breaks`); a sample's one-shot cut (trim, fade, level, name, rate) and loop cut (bars, start, rest / mix) | P0 | choppah-hub test/e2e.mjs |
| LY-095 | **The pack's effect pads on the hub**: the pack page (owner) and the builder's section 4 show the 11 pads, each with its effect (the FX library, as PRESETS lists it), its settings, mix, MORE and MOD with the FM-1's names and ranges, and TARGET; start from the FM-1's factory pads, from another pack of yours or a public one, or none (the pack then plays the FM-1's own); MCP: `pack_pads` reads and sets them (one pad or all, or copied from a pack) | P0 | choppah-hub test/e2e.mjs |
| LY-096 | Packs made by the pack maker get the factory pads unless asked otherwise (`pads_from`: another pack's) | P1 | |
| LY-094 | **Turn a pack's breaks into shots**: a break whose sample is a ONE-SHOT is cut again as a shot (one click on the pack page, a tool for the MCP), so the ragga packs of 26 Nov 2026 become breaks + shots | P1 | |

---

## 9. Order of work

| Phase | Scope | Exit |
| --- | --- | --- |
| **L1 Types and format** (hub + `chpk.js`) | Sample types, the guess, the one-shot and loop editors, LOOP / SHOT / PADS chunks, the pads editor, packs from typed samples, the room bar. Ships before the firmware: a 1.7 plays L1 and ignores the rest | LY-060, LY-070, LY-080–083, LY-090, LY-092, LY-093, LY-095 |
| **L2 ARP plays the layer** (firmware 1.8) | Shot voices, 2 loop voices, tap / hold / in phase, AUTO, the page and its screen, hold-ARP from any page, INFO 5 | LY-001, 002, 005–007, 010, 011, 013, 020, 023, 024, 030, 036, 037, 038, 050–055, 061, 062 |
| **L3 Play it deeper** | Groups, restart, choke, slices on SEL, effect pad TARGET, MIDI notes, the looper and SEQ's SHOTS lane, the FM-1's edits pulled back, breaks into shots | LY-003, 004, 012, 014, 021, 022, 025, 035, 039, 040, 041, 063, 071, 091, 094, 096 |
| **L4 Later** | Patterns launching loops, MATHCORE shots, velocity, the pads-1–4 notice | LY-015, 03A, 042, 043 |

---

## 10. Open questions

| # | Question | Default if not decided |
| --- | --- | --- |
| Q1 | 2 loop voices and 4 shot voices: does the FM-1 hold them (LY-053)? | 1.8 ships the fallback (1 loop + 3 shots) as GLOBAL LAYER's default (D6); raise it there and measure |
| Q2 | With a shots pack, do the white keys stay shots on ARP, or slices with the shots on SEL? | Shots on the keys, slices on SEL (LY-014) |
| Q3 | Flash for the code (LY-055): what leaves if 1.8 doesn't fit? | It fits with 3.7 KB to spare; the P1 items will need room: ask Laurens |
| Q4 | Shots longer than 4 s (a full acapella phrase): a shot, or a loop of type LOOP without a tempo? | ≤ 4 s; longer is a LOOP |

---

## 11. Non-goals

- Reversed shots or loops (they need RAM; both stream forward from flash).
- More than 4 loops or 16 shots in a pack.
- Synthesised sounds (CHOPPAH stays sample-only).
- A page of its own for the layer (D1: it is ARP's).
