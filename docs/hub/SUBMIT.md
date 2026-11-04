# CHOPPAH on the FM-1 Firmware Hub

Everything the hub asks for (<https://thefirmwarehub.com/submit/>, <https://thefirmwarehub.com/kb/>), ready to send.

The hub takes a firmware by **mail**, not by pull request: `zven@thefirmwarehub.com`. Its form ("Mail me your
firmware" on the submit page) opens the same subject and the same questions.

## The mail

Links only, no attachments. Send it after the tests below have passed and the release is on GitHub.

> **Subject:** The Firmware Hub: my firmware
>
> Name of the firmware: CHOPPAH
> Your name (as it should appear): Laurens Profittlich
> Repository: https://github.com/profikid/fm1-break-choppah (release v0.65, choppah-0.65.fwsc attached)
> Identity (FM-1_...): FM-1_86
> Licence: MPL-2.0, the complete source in the repository
> What it does (two or three sentences): An Amen break chopper: eight classic breaks cut into 16 slices on the
> white keys, hold a key to loop it. Black keys are momentary pads: rolls, reverse, freeze, tape stop, gate,
> filter and dub throws. A generator rearranges the break live; a four-slot master FX chain and a DJ filter sit on
> the output. A step sequencer with 16 patterns, a synthesised kick, snare and hat on the patterns' drum lanes, and
> MIDI in and out.
> Picture (link or attached): https://raw.githubusercontent.com/profikid/fm1-break-choppah/main/docs/img/card.png
> Play in the browser / editor / video (optional): https://profikid.github.io/fm1-break-choppah/emu/ · the manual:
> https://profikid.github.io/fm1-break-choppah/manual/
> Tested on a real FM-1, leaving it again works: yes
>
> It keeps its own store and does not read other firmwares' projects or samples. USB port name: CHOPPAH FM-1.

**What it does** is also in the repository's `fmhub.json` (the hub reads that every three hours and keeps the card
up to date by itself). The mail repeats it so nothing is missing when the hub reads it.

## The card

- **Name:** CHOPPAH
- **By:** Laurens Profittlich
- **Kind:** `drums`
- **Text** (`fmhub.json`, 294 characters): An Amen break chopper: eight classic breaks cut into 16 slices on the
  white keys, hold a key to loop it. Black keys are momentary pads: rolls, reverse, freeze, tape stop, gate, filter
  and dub throws. A generator rearranges the break live; a four-slot FX chain and a DJ filter sit on the output.
- **Picture:** `docs/img/card.png` (240 × 240 PNG, 9 KB): CHOPPAH's own card picture, the FM-1's screen in its face.
- **Play in the browser:** <https://profikid.github.io/fm1-break-choppah/emu/>
- **Manual:** <https://profikid.github.io/fm1-break-choppah/manual/> (source: `app/docs/MANUAL.md`)

## The package

- **File:** CHOPPAH 0.65, the installer's `choppah-0.65.fwsc` (611,743 B, SHA-256
  `4ff23087442b2da631eb57f8e0a7b56464c263d1436cd63d199378c89f28c188`); the release v0.65 attaches the same file.
- **Release:** GitHub releases, one per merge. The hub takes every later release by itself, within three hours.
- **Identity:** `FM-1_86`. Every release keeps the prefix: `FM-1_86006500` for 0.65. The hub's own list of identities
  has no `FM-1_86*`, so the prefix is free and the hub can tell exactly which firmware is on a connected FM-1.
- **Update loader:** felucca-bsp's, the platform's loader (fm1-base's `base/bsp/loader/`), shipped in the package.
- **USB:** a MIDI device named "CHOPPAH FM-1".
- **What it keeps:** its own store in the data area (`APP_STORE_MAP`: the settings, the panel, the Bluetooth keys and
  the 16 patterns); it does not read other firmwares' projects or samples.

## Before sending: the hub's tests, on a real FM-1, with that file

1. **From the official firmware:** on <https://thefirmwarehub.com>, "Back to official V15"; then install CHOPPAH from
   <https://profikid.github.io/fm1-break-choppah/install/>. It starts, and after "Connect the FM-1" the hub shows
   FM-1_86.
2. **From another firmware:** install another one from the hub (Felucca, for one), then CHOPPAH again.
3. **Leave it:** with CHOPPAH running, install any firmware from the hub. The update must start: the most important
   test.
4. **USB:** on a Mac, Audio MIDI Setup; note the name the FM-1 shows (for the mail).
5. **An interrupted update:** unplug once during "Writing", plug in, press Install again: it must continue.
6. **Play it:** the patterns, the drums, saving with SAVE, a backup and a restore.

## Then

1. The release on GitHub with the file (done: v0.65).
2. The mail above, from your own address.
3. The hub puts it on its test site, then live; later releases appear by themselves.
4. Optional: "Built from source on GitHub" (the hub's shared workflow `zvenson/fm1-build`), once the build runs on
   GitHub's Ubuntu runner. CHOPPAH's own workflow (`pages.yml`) already builds the firmware, the emulator and the
   site on every push, so the release is built from the tagged source; the hub's shared workflow would additionally
   sign it.

## The flash part, and the units from autumn 2026

Units sold since autumn 2026 (mostly AliExpress) carry a UC25HQ80 flash (JEDEC `0xB36014`) instead of the stock Puya
P25Q80 (`0x856014`). fm1-base's boot only knew the Puya, so on those units CHOPPAH ran, but SAVE failed (the store's
save returns -9), the data area could not be written and the update loader was never started: **a firmware could not
be installed from them**.

Fixed in <https://github.com/charlesvestal/fm1-base/pull/35>: the parts the storage may use are one macro,
`FM1B_FLASH_JEDEC_OK` (both ids). **Merge that, or pin fm1-base to the commit with it, before the hub lists CHOPPAH:
a player on a UC25HQ80 must be able to install it and leave it again.**

The loader itself (felucca-bsp's) has no flash-id check and writes the package on either part.

## The other directories

- **`fmhub.json`** in the repository root: the card's text, the name, the picture and the manual. The hub reads it
  every three hours; a change is picked up by itself, no mail.
- **awesome-fm-1** (<https://github.com/cicloid/awesome-fm-1>): the CHOPPAH entry is in it
  (<https://github.com/cicloid/awesome-fm-1/pull/15> fixed the break count).
- **fm1-editor.com/firmware/**: CHOPPAH's card is there
  (<https://github.com/benny-sparra/fm1-dx7-patch-importer/pull/451> brought it to 0.65 and gave it a picture).
- **Mooring** (the signed firmware list in `cicloid/awesome-fm-1`, `mooring/catalog.json`): published by Gustavo
  Barron from the same repository, revision by revision. A release with a `.fwsc` and a known loader is what it
  indexes, so it follows.
