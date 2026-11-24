# CHOPPAH — Packs, the Pack Builder and the Pack Hub

**Feature PRD** · v0.1 · 24 Nov 2026 · Owner: Laurens (Sylverline)  
**Parent product PRD:** [PRD.md](PRD.md) (S-003, §10) · related: [PRD_MATHCORE.md](PRD_MATHCORE.md)  
**Status:** 24 Nov 2026 (later): the FM-1 side is built for **CHOPPAH 1.7** (branch `packs-v2`, merged): pack v2,
the slot settings and HOME's LOOP, scenes per pack, the theme, the overlay, GETSET; tested in the host simulator and
built for the device by CI (image 562 KB of 581.5). To check on an FM-1 before the 1.7 release: the overlay's render
cost (PO-002). Earlier the same day: the hub is **live on choppah.app** (§10, all PH-* rows; repo `profikid/choppah-hub`), the
`.chpk` file (PK-010, PK-011: `app/site/chpk.js`) and the builder's pack details, export / import, upload and open from
the hub (PB-020–025) are built; the builder's tempo, bars, loop finder, stems (v1) and overlay (PB-001–012) too, and the hub's pack manager (§10.6) with the builder inside the hub (§9.4). Not yet: the firmware side (PK-001–004, PS, PT, PM, PO, PE),
Demucs stems (PB-002 v2), the destem server (PB-003: endpoint built, no GPU provider). Decisions in §1.2; open questions in §11.

---

## 1. Purpose

Today a pack is only audio: up to 16 breaks written over SysEx by the break manager. This PRD makes the **pack the
unit of a CHOPPAH sound**: its breaks, how each break loops, a longer overlay loop under them, the MATHCORE scenes
that fit them, and the look of the instrument while they play. Packs are made in a **pack builder** (from any WAV,
destemmed, tempo found, loops found) and shared on a **pack hub**.

### 1.1 Principles

1. **The pack carries its settings.** Loading a pack's break loads that pack's theme, scenes and per-break settings.
   Theme is never a menu on the FM-1.
2. **The device stays lean.** Only what the FM-1 plays or draws goes to flash; thumbnails, long text and builder
   state stay in the browser and on the hub.
3. **Edit anywhere, export always.** What is changed on the FM-1 can be pulled back into the pack file and shared.
4. **Old banks keep working.** A v1 bank (1.6's pack, the built-in bank) loads as before, with factory defaults.
5. **Private first.** Uploads are private until their owner publishes them.

### 1.2 Decisions (24 Nov 2026, Laurens)

| # | Decision |
| --- | --- |
| D1 | Destemming runs **in the browser** where it can; a **server** fallback is acceptable |
| D2 | The hub runs on **Cloudflare** (Workers, D1, R2) |
| D3 | Accounts start **only with an email link** (never a passkey alone). Opening the link: the email's account has a passkey → sign in with it; no account → the name, then a passkey for that email; an account without one → a passkey for it. Returning users sign in with a passkey (24 Nov 2026) |
| D4 | Uploads are **private by default**; the owner can **publish**; signed-in users can **rate** published packs |
| D5 | The hub's code lives in a **separate private repository** (§10.1) |
| D6 | A pack is **installed from the hub** in one click (§10.7); its `.chpk` downloads only when the owner allows it (off by default) or for the owner (24 Nov 2026) |

---

## 2. Baseline (repository at f193f88, 23 Nov 2026)

| Pack part | Today | Gap | Effort |
| --- | --- | --- | --- |
| Break loops | **Done** (S-003, v1.6): 16 slots in two banks (bank 1 0x97000–0xD5FFF 252 KB, bank 2 XDATA1 92 KB), CRC per bank, read back, delete; one quality per pack; `.chb` = the raw bank | — | — |
| id, name, author, created | None: the bank has magic + CRC; each slot a 12-character name | A pack chunk (§3) | S |
| Thumbnail | None | Browser and hub only (§3) | S |
| Theme | Amber compiled in (`APP_PALETTE 1u`, fm1-base gfx) plus ~16 RGB constants in `app/ui/*.c` | A runtime palette (§5) | M (touches fm1-base) |
| MATHCORE presets | 16 global scenes in `OBJ_MC` (100 B each, magic MCS5); chain and black keys in `OBJ_MQ` | Scenes and chain per pack (§6) | S–M |
| Loop length per sample | Slot: bars, slices, BPM, slice offsets; breaks fixed at 2 bars (32 steps) | Per-slot settings and a loop window in the engine (§4) | M |
| Overlay loop (4+ bars) | None. Pool ~326 of 344 KB used (`pcm_a` 208 KB, dub line 60 KB): a 4-bar loop decoded would be ~250 KB | A streaming voice reading ADPCM from flash (§7) | **L** |
| Edit on device → export | Edits land in the global `OBJ_SETTINGS`; SysEx reads breaks back, not settings | Per-pack edits and a settings read-back (§8) | M |
| Builder | `breaks.html`: decode, tempo from the file's length (`guessTempo`: the file must already be a loop), onsets, warp, pack layout, IndexedDB, `.chb` | Beat tracking, loop finding, destemming, overlay, pack editor (§9) | L |
| Hub | A curated read-only library (`lib/index.json`, Cyberworm's Amens) | Accounts, uploads, publish, ratings, moderation (§10) | L |
| Flash for code | ~25 KB left of the 581,564 B app slot | Everything on the device must fit, or something leaves first | Risk |

---

## 3. The pack and its file

### 3.1 What a pack holds

| Field | Lives on | Size on the FM-1 | Notes |
| --- | --- | --- | --- |
| `id` | device, browser, hub | 16 B | random 128-bit, made by the builder; a new id on "save as" |
| `name` | device, browser, hub | 16 B | upper case, shown in the header with the break's name |
| `author` | device (short), hub (full) | 16 B | the hub's display name of the uploader is separate |
| `created`, `updated` | device, browser, hub | 8 B | Unix seconds |
| `theme` | device | 32 B | §5 |
| `breaks` | device | the slot table (168 B a slot) + ADPCM | as today |
| `slot settings` | device | 48 B a slot | §4 |
| `scenes` | device | ~1.6 KB (16 × `mc_scene_t`) + chain and black keys (~80 B) | §6; absent = factory scenes |
| `overlay` | device | header 32 B + ADPCM | §7; optional |
| `thumbnail` | browser, hub | — | WebP ≤ 64 KB, 512 × 512; made from the waveform in the theme's colours, or uploaded |
| `description`, `tags`, `source notes` | browser, hub | — | |

### 3.2 Bank v2 (on the FM-1)

The bank keeps `CHB1` and its 16-byte header; `version` becomes **2**. After the slot table comes a **chunk
directory**, then the audio:

```
header (16 B, version 2) | slot table (nslots × 168 B) | chunk count (u32) | chunks: tag[4] len(u32) data, 4-aligned
                         | ADPCM (breaks, then the overlay)
```

| Tag | Content |
| --- | --- |
| `PACK` | id, name, author, created, updated |
| `THEM` | the palette (§5) |
| `SSET` | slot settings, one record per slot (§4) |
| `SCNE` | MATHCORE scenes + chain + black keys (§6), with the scene format's magic (MCS5) |
| `OVLY` | overlay header (§7); its audio sits with the breaks' and is addressed by offset |

Unknown tags are skipped (forward compatible). The CRC still covers everything after the header; the commit order
(data first, header last) stays. A pack spans both banks as today; the chunks live in bank 1.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PK-001 | Firmware reads bank v2 and v1; v1 gets factory theme, scenes and slot settings | P0 | *Done (1.7): tests/pack.sh (v1), tests/pack_v2.sh (v2)* |
| PK-002 | Firmware before packs v2 refuses a v2 bank instead of misreading it (check `pk_scan` on 1.6 now; ship the guard in the next patch if it reads only the magic) | P0 | *1.6 checks the version byte (bank_check): a v2 bank isn't read there; the builder sends v2 only to INFO 3* |
| PK-003 | INFO answers version 3: pack support, chunk tags known, bytes free per bank | P0 | *Done: INFO version 3; the builder reads the pack's id back (READ) and its edits (GETSET)* |
| PK-004 | The built-in bank is the **factory pack** (amber, factory scenes, default slot settings) | P0 | *Done: no pack, or a built-in break: amber, the factory's scenes, the break as it is* |

### 3.3 The exchange file `.chpk`

One binary file for import, export and the hub: **a bank v2 image followed by browser-only chunks** (`THMB` WebP,
`META` UTF-8 JSON: full author, description, tags, source notes, builder state such as the source's markers). The
builder strips browser-only chunks before sending to the FM-1. `.chb` (v1) stays importable.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PK-010 | Export / import `.chpk`; import `.chb` as a pack with factory settings | P0 | Round trip byte for byte (`tests/page_pack.sh`) |
| PK-011 | One parser shared by the builder and the hub's Worker (a JS module) | P1 | The Worker refuses what the builder refuses |

---

## 4. Slot settings (`SSET`)

Each break carries how it plays. Defaults are today's behaviour, so a v1 break sounds the same.

| Field | Range | Default | Note |
| --- | --- | --- | --- |
| `loop_steps` | 4, 8, 16, 32 (¼, ½, 1, 2 bars) | 32 | the loop window; breaks stay at most 2 bars (`CH_PCM_MAX`) |
| `loop_start` | 0 … 31 steps | 0 | where the window starts |
| `bpm_x100` | override of the slot's BPM | slot's | |
| `gain_db` | −24 … +12 | 0 | after the prep tool's levelling |
| `pitch` | ±24 semitones, ±100 cents | 0 | |
| `swing` | 50 … 75 % | global | `0xFF` = follow the global swing |
| `pitch_mode` | KEEP LENGTH / SPEED / global | global | |
| `roles` | K S H P – per slice (32 × 2 bits) | auto | auto = `mc_auto_roles` heard at load; a set role wins |
| `rsv` | | | to 48 B |

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PS-001 | Loading a break applies its slot settings at the bar swap | P0 | *Done (1.7): slot_apply when the break loads (the bar swap)* |
| PS-002 | Loop window in the engine (`brk_break_t`): the loop, the generator, MATHCORE and SEQ read the window, not the whole break | P0 | *Done: slot_window tiles the window over the two bars; tests/pack_v2.sh (a 1/2-bar window: the break repeats every 8 steps)* |
| PS-003 | EDIT shows and edits the slot's settings (BREAK part) | P1 | *Done, on HOME's fourth part LOOP (EDIT brings MATHCORE in): LENGTH, FROM, PITCH, GAIN* |
| PS-004 | Roles set by hand override the heard Slice Map for that slice only | P1 | *Done: slot_roles, through the window's tiling* |

---

## 5. Theme (`THEM`)

Not a setting on the FM-1: the pack's theme is drawn while one of its breaks plays.

| Colour | Used for (today's amber) |
| --- | --- |
| `bg` | screen ground |
| `panel` | block ground (`C_BLOCK`) |
| `text`, `dim` | labels, inactive |
| `hi` | the accent (`C_HI`, `C_BLOCK_HI`, the splash) |
| `mid` | second accent (tabs, gauges) |
| `wave` | the waveform |
| `warn` | overload, battery low |

Eight RGB565 colours + 16 B reserved (e.g. a splash style) = 32 B. Band colours with a meaning (MB COMP's three
bands, the K S H P letters) stay fixed for legibility, or are derived from `hi` and `mid`.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PT-001 | A runtime palette: the UI's named colours read from a RAM table; fm1-base's `APP_PALETTE` becomes the default table (an fm1-base change: propose upstream) | P0 | *Done (1.7): gfx.c's pal[] is RAM already: no fm1-base change; tests/pack_v2.sh* |
| PT-002 | The theme changes with the break, at its bar swap (see Q1) | P0 | *Done* |
| PT-003 | The builder warns on low contrast (text on bg < 4.5 : 1) | P1 | *Done: the builder names the colours under 4.5 : 1* |
| PT-004 | The builder previews the theme in the browser emulator | P1 | *Open: the builder draws the HOME screen's colours (its picture); the emulator itself takes packs once it reads banks from the page* |

---

## 6. MATHCORE per pack (`SCNE`)

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PM-001 | A pack may carry its 16 scenes, its chain and its black keys; without them, the factory `MC_PRESET` | P0 | *Done (1.7): mc_pack; tests/pack_v2.sh (scene A's phrase from the pack)* |
| PM-002 | `OBJ_MC` holds the **active pack's id** and its edited scenes; `OBJ_MQ` its chain and keys. A different pack loads its own (the edits of the previous one are pulled first, §8) | P0 | *Done: keyed by the pack's id in mq.rsv; a 1.6 save keeps its scenes for its v1 pack* |
| PM-003 | A scene format change (MCS5 → MCS6) converts old packs in the builder, not on the device | P1 | *Standing rule: none needed yet (MCS5)* |

---

## 7. Overlay loop (`OVLY`)

A longer loop (4, 8 or 16 bars) under the breaks: a bass line, a pad, a vocal bed, typically the destemmed rest of
the track the breaks came from (§9). It is **not** a lane over the break (PRD_MATHCORE §16): it doesn't chop,
it's a bed with its own fader.

**Engine:** decoding it into RAM can't work (§2), and it needn't: an overlay only plays forwards. It is decoded
**from flash as it plays**, a block at a time (~70 B of ADPCM a 256-frame block at 22.05 kHz), with a few samples of
history for the Hermite resampler; its loop seam resets the ADPCM state (the builder encodes the loop from state 0
and bakes a crossfade into the seam). Its position follows the transport: `bar mod length`, so it never drifts and
a break change doesn't restart it. It repitches with the tempo like the breaks.

**Flash** (mono IMA ADPCM, 4 bits):

| Length | 172 BPM, 22.05 kHz | 120 BPM, 22.05 kHz | 120 BPM, 11.025 kHz |
| --- | --- | --- | --- |
| 4 bars | 62 KB | 88 KB | 44 KB |
| 8 bars | 123 KB | 176 KB | 88 KB |

Bank 2 (92 KB) is its natural home; longer or slower overlays take room from the breaks.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PO-001 | Streaming overlay voice in `app/dsp/` (platform-free): flash pointer in, PCM out, phase from the transport | P0 | *Done (1.7): brk_overlay; test_overlay (bar 2 on its bar 2), tests/pack_v2.sh (heard)* |
| PO-002 | Render cost measured on the device against the last release (flash-cache misses: the reason `fx_samples` is in RAM) | P0 | Render < 85 % with all FX + overlay. *Host: 64 samples decoded at a time, a mark every 4096; on the FM-1: to measure before the release (OVERLOAD sheds the FX first)* |
| PO-003 | MIXER: an OVERLAY fader and mute (the old LANES fader's reserved bytes) | P0 | *Done: MIXER K2 OVERLAY with its meter* |
| PO-004 | Which performance actions reach the overlay (Q2): default **TAPE STOP, HALF and STOP** do; ROLL, FREEZE, REVERSE, scrub and slice FX don't; the DJ filter: a setting | P1 | *Done: TAPE STOP, HALF and STOP; the DJ filter: the route (through the chain after it, or around)* |
| PO-005 | Through the FX chain or around it (like the old drums' route) | P1 | *Done: GROOVE K2 OVL ROUTE* |
| PO-006 | While a flash write stalls reads (SAVE, a pack being sent), the overlay fades out and back | P0 | *Done: let go before a bank's erase; stopped, it fades out first* |

---

## 8. Edit on the FM-1, export from the browser

The pack in flash holds the **defaults** and is only written by the builder. The FM-1 keeps its edits (slot settings,
scenes, chain, keys) in its store objects with the pack's id.

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PE-001 | Store edits keyed by pack id: `OBJ_MC` (scenes), `OBJ_MQ` grown with the slot settings (16 × 48 B) | P0 | *Done (1.7): mq.ss[] in OBJ_MQ (scenes in OBJ_MC), keyed by the pack* |
| PE-002 | SysEx `GETSET`: the edits of the pack on the FM-1, as chunks (`SSET`, `SCNE`) | P0 | *Done: GETSET (7) = OBJ_MC's scenes, OBJ_MQ's chain (88 B), the LOOP settings (40 B each); tests/pack_v2.sh* |
| PE-003 | The builder's **Pull edits** merges them into the pack in IndexedDB (a new `updated`; "save as" = a new id) | P0 | *Done: Pull the changes from the FM-1* |
| PE-004 | Before **Send** replaces a pack with edits on the FM-1, the builder pulls them and asks: keep in the old pack / discard | P0 | *Done: Send asks first when the FM-1's pack is this one and it changed there* |
| PE-005 | On the FM-1, SEL + SAVE on EDIT's BREAK part: revert the slot to the pack's defaults | P2 | *Done: SEL + a knob on LOOP* |

---

## 9. Pack builder (the break manager grown up)

`app/site/breaks.html` becomes the **pack builder**: same page, same Send, now working on packs. Everything runs in
the browser; heavy work in Web Workers.

### 9.1 From any WAV to breaks

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PB-001 | Import any audio the browser decodes (WAV, AIFF, FLAC, MP3, M4A), up to 10 minutes | P0 | *Done (24 Nov 2026): anything decodeAudioData reads* |
| PB-002 | **Destem** (D1), in the browser: v1 (24 Nov 2026) a harmonic / percussive split (median-filtered spectrogram, Fitzgerald 2010; `stems()` in beats.js, in a Web Worker): drums for the breaks, the rest for the overlay; no download, no server. v2: Demucs (htdemucs, MIT) through ONNX Runtime Web on WebGPU for four stems | P0 | *v1 done: tests/beats.test.mjs (a pad 97 % to the rest), tests/page_pack.sh in Chrome. v2 open* |
| PB-003 | **Server fallback** (D1) when WebGPU is missing or too slow: a hub Worker forwards to a GPU endpoint (provider to choose, Q5); signed-in users only, a daily quota, the audio deleted when the stems are returned | P1 | No audio kept server-side (R2 lifecycle 1 h) |
| PB-004 | **Tempo**: beat tracking on the drum stem (spectral-flux onsets → tempogram), the 80–180 prior and ×2 / ÷2 as today; tap to correct | P0 | *Done: app/site/beats.js (spectral-flux onsets, autocorrelation, a comb refined to 0.01 BPM, a tempo prior; loops of 1/2/4/8/16 bars exact); tests/beats.test.mjs: the 10 classics within 1.5 %, a track at 99.88 for 99.81* |
| PB-005 | **Downbeat**: bar phase from the kick band and the snare's backbeat; nudge by a beat | P0 | *Done: kicks on 1 and 3, snares on 2 and 4; a loop starts on its 1; the test track's bars 100 % on the break's* |
| PB-006 | **Loop finder**: every 1- and 2-bar window scored (drum energy, self-similarity with the next window, no vocal/bleed) → the best 8 as cards to audition and add | P0 | *Done: findLoops (onset density, the next bar alike, drum share); the test's best loop on a bar line inside the break* |
| PB-007 | Slices as today (grid / onsets, warp), on the drum stem | P0 | *Done (unchanged; page_parity.sh)* |
| PB-008 | Own code for beat tracking: no AGPL (essentia.js) in an MPL-2.0 project | P0 | *Done: no libraries at all* |

### 9.2 The overlay from the stems

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PB-010 | Choose stems for the overlay (bass, other, vocals), 4 / 8 / 16 bars on the same bar grid as the breaks | P1 | *Done: from bar n, 4/8/16 bars, the rest or the mix (tests/page_pack.sh)* |
| PB-011 | Seam crossfade, level matched to the breaks (−16 dBFS RMS target as `level()`) | P1 | *Done: seamless() (12 ms into the lead-in), level() as the breaks* |
| PB-012 | Rate chosen to fit the room left (22.05 / 16 / 11.025 kHz) | P1 | *Done: 22.05 → 16 → 11.025 kHz, the first that fits (chpk packLayout reserves it, bank 2 first)* |

### 9.3 The pack editor

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PB-020 | Pack name, author, description, tags; the id made on creation | P0 | *Done* |
| PB-021 | Theme editor: presets (AMBER, ICE, ACID, DIP …), eight colour pickers, the HOME screen previewed (PT-004) | P1 | *Done (presets AMBER ICE ACID DIP TERMINAL PAPER + 8 pickers); the emulator preview waits for the firmware's palette (PT-001)* |
| PB-022 | Thumbnail: generated (waveform in the theme), or an image cropped to square | P1 | *Done* |
| PB-023 | Slot settings per break (§4) and the scenes (factory, from another pack, or a `.chpk`) | P0 | *Done: slot settings; scenes factory / this pack's / another .chpk's* |
| PB-024 | Capacity meter: both banks, breaks + overlay + chunks (`packLayout` extended) | P0 | *Done, the overlay included* |
| PB-025 | Library of packs in IndexedDB; export / import `.chpk` | P0 | *Done* |

---

## 9.4 The builder on the hub (24 Nov 2026)

On choppah.app the builder is the hub's: a third source tab, **From your hub library** (your samples, starred
breaks, your packs), **Keep this sample in my hub library** for a file you load, **Save to my library** (a pack keeps
its id: saving again replaces it, private until published), **Start a new pack**, and links from the hub straight in:
`/breaks/#hub=<pack>`, `#sample=<id>`, `#break=<pack>:<slot>`, `#new`.

## 10. Pack hub

### 10.1 Architecture (D2, D5)

- **Repository (D5):** the hub lives in a **separate private repository** (proposed `profikid/choppah-hub`): the
  Worker, the D1 schema and migrations, the hub's pages, the admin view, deploy workflows. Nothing of the hub goes into
  this public firmware repository, and nothing of the firmware goes into the hub's.
- **What crosses:** the `.chpk` parser (PK-011) lives **here**, public, as one MPL-2.0 module (`app/site/chpk.js`),
  used by the builder; the hub vendors it pinned to a commit of this repository (MPL-2.0 is per file: the file stays
  MPL and its source is already public; the rest of the hub stays private). The builder knows the hub only by its URL
  and the API of §10.5.
- **Secrets** (mail provider key, GPU provider key, session signing key) are Worker secrets (`wrangler secret`),
  never in either repository.
- **Site:** the hub pages are part of the GitHub Pages site (or `hub.` on the same domain); they call the API.
- **API:** one Cloudflare Worker (`/api/*`).
- **D1:** users, passkeys, packs, ratings, reports.
- **R2:** `.chpk` files and thumbnails; public packs served through the Worker (counted downloads).
- **Mail:** Cloudflare Email Service (Email Sending, choppah.app onboarded 24 Nov 2026; a `send_email` binding), Resend
  as a fallback (Q4 decided).
- **WebAuthn:** SimpleWebAuthn (MIT) runs on Workers.

### 10.2 Accounts (D3)

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PH-001 | Sign up: email → a link (15 min, one use) → the name → a passkey for that email; the link used up only when the passkey checks | P0 | *Done; choppah-hub test/e2e.mjs* |
| PH-002 | Sign in with a passkey (discoverable credentials: no username typed) | P0 | |
| PH-003 | A new device: sign in with the email link, then add its passkey; several passkeys per account, each removable | P0 | |
| PH-004 | Sessions: HttpOnly, Secure, SameSite=Lax cookie, 30 days, revocable | P0 | |
| PH-005 | Display name (shown as the uploader), separate from the pack's `author` | P1 | |
| PH-006 | Delete account: packs, ratings, passkeys removed | P0 | |

### 10.3 Packs, publishing and ratings (D4)

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PH-010 | Upload from the builder (one click); the Worker validates the `.chpk` (PK-011: CRC, chunks, fits 344 KB, thumbnail WebP ≤ 64 KB, total ≤ 512 KB) | P0 | Refused files say why |
| PH-011 | **Private by default**: only the owner sees and downloads it | P0 | |
| PH-012 | **Publish / unpublish** by the owner; publishing asks to confirm the owner may share the audio (terms) | P0 | |
| PH-013 | A new version of a pack (same id) replaces the file; ratings stay | P1 | |
| PH-014 | **Rate** published packs 1–5, signed in, one rating per user per pack (changeable), not your own; average and count shown | P0 | |
| PH-015 | Browse: newest, top rated (Bayesian average, so 1 rating of 5 doesn't top the list), most downloaded; search by name, author, tag, BPM range | P0 | |
| PH-016 | Pack page: thumbnail, theme swatch, breaks with names and BPM, an audio preview (decoded from the `.chpk` in the browser), **Open in builder** → Send to the FM-1 | P0 | Hub → FM-1 in three clicks |
| PH-017 | Limits: uploads per account per day, size per account | P0 | |

### 10.4 Moderation and rights

Breaks and stems from commercial records, uploaded by strangers, make the hub their host. Before uploads can be
published:

| ID | Requirement | Pri | Acceptance |
| --- | --- | --- | --- |
| PH-020 | Terms on upload: the uploader owns the audio or may share it | P0 | |
| PH-021 | Report button on every public pack (reason, contact); a reported pack stays up until reviewed, unless reports pile up (hidden at N) | P0 | |
| PH-022 | An admin view: hide, restore, delete, ban; a takedown contact on the site | P0 | A takedown done in minutes |
| PH-023 | No full tracks or stems stored: only packs (the destem fallback keeps nothing, PB-003) | P0 | |

### 10.6 The pack manager (24 Nov 2026)

`/hub/library`: **your packs** (open in the builder, publish from Account), **starred** packs and single breaks (☆
on a pack's page and beside each break), **your samples** (your own audio, private, never published: drop files,
name, tag, BPM, star, listen, take to the builder; 30 MB a file, 300 MB an account). Done; choppah-hub test/e2e.mjs.

### 10.7 Install from the hub (24 Nov 2026, D6)

| ID | Requirement | Priority | Status |
| --- | --- | --- | --- |
| PH-030 | **Install on FM-1** on every pack (its page, its card in the list, starred packs): Web MIDI with SysEx (Chrome, Edge), no builder in between | P0 | *Done; tests/fm1link.test.mjs, choppah-hub test/e2e.mjs* |
| PH-031 | Before writing, a dialog shows what is on the FM-1: its firmware (the screen reader's HELLO), the pack on it (name, author, theme, from bank 1's PACK and THEM; 1.7 on) or its own breaks, or only the built-in ones; and asks to replace it with this pack. The same pack already on it says so | P0 | *Done* |
| PH-032 | The FM-1 gets what its firmware takes: 1.7 on (INFO 3) the v2 banks (theme, LOOP settings, scenes, overlay); 1.6 the breaks alone (said in the dialog, with the installer's link); 1.5 and older: update first. Playing: stop first. A bank the pack doesn't need is emptied | P0 | *Done; `installImages` (chpk.js)* |
| PH-033 | The hub builds the banks (`GET /api/packs/:id/install?proto=`, the CHIM file: the bank images, nothing else) from the stored `.chpk`; installs are counted (`installs`) | P0 | *Done* |
| PH-034 | **Downloads off by default**: `GET /api/packs/:id/file` (the `.chpk`, and the builder's Open, and a starred break into the builder) for the owner, an admin, or when the owner switched on "Allow downloads" (Account, per pack). Listening uses the breaks alone (`/preview`). The audio still reaches the browser to be sent to the FM-1, so this keeps the pack file to its owner; it is not copy protection | P0 | *Done* |

The link is `app/site/fm1link.js` (MPL-2.0, no DOM: `FM1Link.open`, `status`, `install`; breaks.html's must /
sendBank rules), vendored into the hub with chpk.js.

### 10.8 The samples slicer and packs from samples (24 Nov 2026)

| ID | Requirement | Priority | Status |
| --- | --- | --- | --- |
| PH-040 | Every sample in the library shows the builder's slicer inline: the whole file with the two bars chosen, the break with its sixteen slices (drag to move, click to hear), tempo (×2 ÷2), offset, which bars, hits or grid, level, warp, loop and chop | P0 | *Done; choppah-hub test/library_ui.mjs* |
| PH-041 | A sample's edit **saves itself** (the sample's `edit`, a moment after the last change) and **Reset** goes back to what was heard (`auto`: tempo, first bar and best loop from beats.js, the waveform's peaks) | P0 | *Done* |
| PH-042 | **One plays at a time**: starting a slicer stops any other | P0 | *Done* |
| PH-043 | Select samples (several) → **New pack** from them, or **Add to** one of your packs; each becomes a break as the builder makes it (`breakcut.js` = breaks.html's level, slices, warp: tests/breakcut.test.mjs) | P0 | *Done* |
| PH-044 | Every pack of yours shows its **room on the FM-1**: the bytes of its banks (as 1.7 gets them, `installImages`) of the two banks' 344 KB, and its breaks of 16; adding refuses what doesn't fit, before anything is made | P0 | *Done (`packs.flash_used`)* |
| PH-045 | One header on every page of choppah.app (the site's pages too: the hub's navigation and the account link); the front page is a Three.js presentation: an FM-1 running CHOPPAH, turning | P1 | *Done* |

### 10.9 The MATHCORE editor, scene sets, and samples in packs (24 Nov 2026)

| ID | Requirement | Priority | Status |
| --- | --- | --- | --- |
| PH-050 | A **MATHCORE editor** in the browser (`/hub/mathcore`): all 16 scenes (name, phrase bars, RUN ON, ONCE) and each one's four lanes with every field the FM-1 keeps (on, role, the steps with hits and accents, length, rate, phase, probability, ratchet, pitch, mode, pick, gate, level, seed, rise, vary, reverse, free, swell); the **chain** (up to 64 scenes) and the **black keys'** scenes; copy, paste, clear, factory per scene | P0 | *Done; choppah-hub test/mathcore_ui.mjs* |
| PH-051 | It is heard as the FM-1 plays it: `mathcore.js` `Player` is brk.c's mc_tick (the same hash, picks, ratchets, VARY, RISE, DENSITY / MATH / SPEED / CHAOS), on a pack's break with its Slice Map (`autoRoles` = mc_auto_roles, the pack's roles set by hand over it); one scene round and round, or the chain | P0 | *Done; tests/mathcore.test.mjs* |
| PH-052 | A pack carries its **chain and black keys** (CHAN, 80 bytes, after SCNE; CHOPPAH 1.7 reads it in `mc_pack`, older ones skip it); the builder sends, exports, imports and pulls it with the scenes | P0 | *Done; tests/pack_v2.sh, tests/chpk.test.mjs* |
| PH-053 | Saved **to a pack** of yours (its SCNE and CHAN replaced, a new version) or **to your library** as a named **scene set**; opened from the factory's, a set of yours, or any pack you can see (its scenes: `/api/packs/:id/scenes`) | P0 | *Done* |
| PH-054 | Every sample shows **which packs it is in** (and which break), and how many: each pack break remembers the sample it was made from (library and builder), kept across a pack's new versions by the break's name and tempo | P1 | *Done* |

### 10.5 API (sketch)

```
POST /api/auth/email          {email}                → mails a link
POST /api/auth/link           {token}                → session; passkey registration options
POST /api/auth/passkey/*      register / login options and verify
GET  /api/packs?sort=&q=&tag=&bpm=                   public list
GET  /api/packs/:id           meta (+ private if owner)
GET  /api/packs/:id/file      .chpk (counted): the owner, an admin, or allow_download
GET  /api/packs/:id/preview   the breaks (a CHB1 bank) to listen to
GET  /api/packs/:id/install?proto=2|3   the FM-1's banks (CHIM; counted as an install)
POST /api/packs               upload (multipart .chpk) → private
PATCH /api/packs/:id          {visibility: public|private} | {allow_download: bool}
PUT  /api/packs/:id/rating    {stars: 1..5}
POST /api/packs/:id/report    {reason}
GET  /api/me/packs            the owner's packs
```

---

## 11. Open questions

| # | Question | Default if not decided |
| --- | --- | --- |
| Q1 | Theme switching mid-set: does a built-in break after a pack break bring amber back? | Yes, at the bar swap: the theme says whose break plays |
| Q2 | Which pads reach the overlay? | PO-004's default |
| Q3 | One pack on the FM-1 at a time, or several sharing the 16 slots (each slot its pack's id and theme)? | One pack (v1); several later if wanted |
| Q4 | Mail provider for the links | Resend (simple API, works from Workers) |
| Q5 | GPU provider for the destem fallback (Cloudflare has no Demucs runtime of its own that we know of: check) | Replicate or Modal behind the Worker |
| Q6 | Hub domain: the GitHub Pages site, or a subdomain on Cloudflare Pages? | A subdomain on Cloudflare, linked from the site |

---

## 12. Order of work

| Phase | Scope | Exit |
| --- | --- | --- |
| **P1 Pack v2** | Bank v2 + chunks, `.chpk`, slot settings and the loop window, scenes per pack, edits keyed by pack, GETSET, pull edits | PK-*, PS-001/002, PM-001/002, PE-001–004 |
| **P2 Builder: any WAV** | Beat tracking, downbeat, loop finder, then destemming in the browser | PB-001–008 |
| **P3 Hub: read** (hub repo) | Worker, D1, R2, browse, pack pages, Open in builder | PH-015/016 with seeded packs |
| **P4 Hub: accounts and uploads** (hub repo) | Passkeys, email links, private uploads, publish, ratings, reports, admin | PH-001–023 |
| **P5 Theme** | Runtime palette (fm1-base), theme editor and preview | PT-* |
| **P6 Overlay** | Streaming voice, MIXER fader, builder overlay from stems, server destem fallback | PO-*, PB-010–012, PB-003 |

Flash: P1 and P5 and P6 each need code on the FM-1 (~25 KB left in all). Measure each against the last release;
when it doesn't fit, something leaves first (Laurens's call).

---

## 13. Non-goals (v1)

- Hosting full tracks or stems; only packs.
- Comments, follows, messages on the hub (ratings only).
- Paid packs.
- Synthesised sounds in packs (CHOPPAH stays sample-only).
- Breaks longer than 2 bars (`CH_PCM_MAX`); longer material is the overlay's.
