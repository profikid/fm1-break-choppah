# CHOPPAH on the FM-1 Firmware Hub

Everything the hub asks for (<https://thefirmwarehub.com/submit/>, <https://thefirmwarehub.com/kb/>), ready to send.

The hub takes a firmware by **mail**, not by pull request: `zven@thefirmwarehub.com`. Its form ("Mail me your
firmware" on the submit page) opens the same subject and the same questions. The hub's own instructions for this,
written for an assistant, are at <https://thefirmwarehub.com/fmhub.md>.

**The quickest way to send it:** the button on the submit page fills in the questions. Fill in the answers below and
send it from your own address.

## The mail

**The ready-to-send text is in [`MAIL.txt`](MAIL.txt).** Copy it, or use the button on the submit page
(<https://thefirmwarehub.com/submit/>), which fills in the same questions.

The hub's own rules (`fmhub.md`, section 5, and the submit page) are explicit about what goes in the mail:
**the names, the repository with its release, the identity, the licence, and that it was tested on a real FM-1
and you can leave it again.** What it does, the picture, the cheat sheet and the browser version do **not** go in
the mail — they come from `fmhub.json`, and the maker can change them there at any time without mailing again.

The mail as it stands (v0.79):

> **Subject:** The Firmware Hub: my firmware
>
> Hi Sven,
>
> Here is CHOPPAH, a firmware for the M-VAVE FM-1.
>
> Firmware name: CHOPPAH
> Maker: Laurens Profittlich
>
> Repository: https://github.com/profikid/fm1-break-choppah
> Release: v0.79
> Package: choppah-0.79.fwsc
> Identity: FM-1_86
> Licence: MPL-2.0, the complete source is in the repository
>
> What it does: it is in the fmhub.json in the repository root, as the hub prefers.
>
> Tested on a real FM-1: yes. Installed from the official V15 and from another firmware, and another firmware
> installs again afterwards. It keeps its own store (the settings, the panel, the Bluetooth keys and its 16
> patterns) and does not read other firmwares' projects or samples. The USB port shows up as "CHOPPAH FM-1".
>
> The browser version, the manual and the picture are in the fmhub.json too, and the card's text can be changed
> there at any time.
>
> Thanks,
> Laurens Profittlich

**Send it after the tests below have passed.** Links only, no attachments: big mails do not always arrive.

## The card

- **Name:** CHOPPAH
- **By:** Laurens Profittlich
- **Kind:** `drums`
- **Text** (`fmhub.json`, 297 characters): An Amen break chopper: eight classic breaks cut into 16 slices on the
  white keys, hold a key to loop it. Black keys are momentary pads: rolls, reverse, freeze, tape stop, gate, filter
  and dub throws. A generator rearranges the break live; a four-slot FX chain and a DJ filter sit on the output.
- **Picture:** `docs/img/card.png` (240 × 240 PNG, 9 KB): CHOPPAH's own card picture, the FM-1's screen in its face.
- **Play in the browser:** <https://profikid.github.io/fm1-break-choppah/emu/>
- **Manual:** <https://profikid.github.io/fm1-break-choppah/manual/> (source: `app/docs/MANUAL.md`)

## The package

- **File:** CHOPPAH 0.79, the installer's `choppah-0.79.fwsc`; the release v0.79 attaches the same file.
- **Release:** GitHub releases, one per merge. The hub takes every later release by itself, within three hours.
- **Identity:** `FM-1_86`. Every release keeps the prefix: `FM-1_86007900` for 0.79. The hub's own list of identities
  has no `FM-1_86*`, so the prefix is free and the hub can tell exactly which firmware is on a connected FM-1.
- **Update loader:** felucca-bsp's, the platform's loader (fm1-base's `base/bsp/loader/`), shipped in the package.
- **USB:** a MIDI device named "CHOPPAH FM-1".
- **What it keeps:** its own store in the data area (`APP_STORE_MAP`: the settings, the panel, the Bluetooth keys and
  the 16 patterns); it does not read other firmwares' projects or samples.

## The six requirements, checked

The hub lists a firmware only when all six are met (`fmhub.md`, section 1). Five can be checked from here:

| # | Requirement | CHOPPAH |
|---|---|---|
| 1 | Open source, the complete source in the repository | MPL-2.0, `LICENSE` in the root, the source at every tag |
| 2 | A GitHub release with the `.fwsc` attached | every release attaches it; the hub takes each one by itself |
| 3 | Its own `FM-1_` identity, free on the hub | `FM-1_86` (`FM-1_86007900` in 0.79); no `FM-1_86*` on the hub |
| 4 | The update loader in the package | felucca-bsp's loader, shipped in every package |
| 5 | Tested on a real FM-1, leaving it again works | **the test below: yours to run** |
| 6 | An `fmhub.json` in the repository root | `fmhub.json`, on `main`; the hub reads it every three hours |

The card's file also has to hold to the hub's rules: only the seven fields (`what`, `author`, `kinds`, `play`, `editor`,
`cheat`, `image`), the text in English without links or code, the picture square between 240 × 240 and 1024 × 1024 and
at most 300 KB. `docs/img/card.png` is 240 × 240 PNG, 9 KB: the best the rules ask for (a capture of the FM-1's own
screen at exactly 240 × 240).

To see what the hub will make of the file: <https://thefirmwarehub.com/kb/#fmhub> ("Check your file").

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

1. The release on GitHub with the file (done: v0.79).
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
`FM1B_FLASH_JEDEC_OK` (both ids). CHOPPAH's `base/` is pinned to that commit
(`e75b072`), so the released packages already accept both parts: **a player on a UC25HQ80 can install CHOPPAH and
leave it again.**

The fix is still an open pull request upstream (fm1-base #35) and an open issue upstream (fm1-base #36), so it is not
yet in fm1-base's own `main`. CHOPPAH pins the commit directly, which is the same thing the pin always does; when
upstream merges it, the pin needs no change.

The loader itself (felucca-bsp's) has no flash-id check and writes the package on either part. Two other places
accept only the Puya and are in felucca-bsp (Leo's repo), so they are left alone for now: `bsp/tools/fm1_rescue.py`
(the rescue script refuses a UC25HQ80 unit) and `bsp/examples/hwtest/hwtest.c` (an example).

## The other directories

- **`fmhub.json`** in the repository root: the card's text, the name, the picture and the manual. The hub reads it
  every three hours; a change is picked up by itself, no mail.
- **awesome-fm-1** (<https://github.com/cicloid/awesome-fm-1>): the CHOPPAH entry is in it
  (<https://github.com/cicloid/awesome-fm-1/pull/15> fixed the break count).
- **fm1-editor.com/firmware/**: CHOPPAH's card is there
  (<https://github.com/benny-sparra/fm1-dx7-patch-importer/pull/451> brought it up to date and gave it a picture).
- **Mooring** (the signed firmware list in `cicloid/awesome-fm-1`, `mooring/catalog.json`): published by Gustavo
  Barron from the same repository, revision by revision. A release with a `.fwsc` and a known loader is what it
  indexes, so it follows.
