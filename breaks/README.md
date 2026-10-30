# Breaks

60 Amen loops, the source of the breaks built into CHOPPAH, all of them by Dmitriy "Cyberworm" Vasilyev
(rhythm-lab.com), who offers them royalty free (`huge/info.txt`):

- `cw/`: 20 loops, `cw_amen01_175.wav` … `cw_amen20_164.wav`, tempo in the name
- `huge/`: 40 loops from the "Huge Amen Break Loops Collection", `cw_amen_apache.wav` … `cw_amen_zinc.wav`

16-bit 44.1 kHz WAV, mono or stereo, as published.

[`breaks.txt`](breaks.txt) lists every WAV with its tempo, its bars and a name, and marks with `*` the ones that go into
the firmware (12 at 2 bars fill ~395 KB of the 400 KB the firmware has for breaks beside Bluetooth MIDI). `./build.sh` turns those into
`app/bank_data.h` (generated, git-ignored): mono, 22.05 kHz, IMA ADPCM, 16 slices a break with the markers on the
drum hits. To change the set, move the `*`s and build; `./build.sh bank` makes the bank on its own and says whether
it fits. All paths here are relative to this folder.

Uploading breaks to the FM-1 without rebuilding (the flash's data area, ~356 KB more) is milestone M5.
