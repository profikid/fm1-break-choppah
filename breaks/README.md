# Breaks

The source of the breaks built into CHOPPAH, and which of them go in: [`breaks.txt`](breaks.txt) lists every WAV here
with its tempo, its bars and a name, and marks with `*` the ones in the firmware, in the order ALGORITHM steps
through them (the first is the break at power-on). All paths are relative to this folder.

## classics/: the firmware's breaks

Ten breaks from the records, 4 bars each, 32-bit float WAV, 44.1 kHz (from a shared "10 breaks" folder; the tempos
here come from their lengths, a hair off the folder's labels; the Amen was labelled at half its tempo, 71). The
firmware has the first two bars of eight of them:

| Name | Record | BPM | In the firmware |
| --- | --- | --- | --- |
| AMEN | The Winstons, "Amen, Brother" | 141.82 | yes (the first) |
| DRUMMER | James Brown, "Funky Drummer" | 99.81 | yes |
| THE WORM | Jimmy McGriff, "The Worm" | 109.79 | yes |
| THINK | Lyn Collins, "Think (About It)" | 113.78 | yes |
| COLD SWEAT | James Brown, "Cold Sweat" | 114.00 | yes |
| HOT PANTS | Bobby Byrd, "Hot Pants" | 114.78 | yes |
| APACHE | Incredible Bongo Band, "Apache" | 114.78 | yes |
| SPORT | Lightnin' Rod, "Sport" | 120.69 | yes |
| LIFE COULD | Rotary Connection, "Life Could" | 99.81 | no (room) |
| NEW DAY | Skull Snaps, "It's a New Day" | 94.81 | no (room) |

These are commercial recordings: their rights belong to the labels and artists. They are here because the project's
owner chose to publish them; anyone building on this should make their own call.

## cw/, huge/: Cyberworm's Amens

60 Amen loops by Dmitriy "Cyberworm" Vasilyev (rhythm-lab.com), who offers them royalty free (`huge/info.txt`): 20 in
`cw/` (tempo in the name), 40 from the "Huge Amen Break Loops Collection" in `huge/`. 16-bit 44.1 kHz WAV. They were
the firmware's breaks until 1 November 2026; none is marked now.

## How the bank is made

`./build.sh` turns the marked lines into `app/bank_data.h` (generated, git-ignored) with `tools/prep_breaks.py`:
mono, 22.05 kHz, IMA ADPCM, 16 slices a break with the markers on the drum hits. The firmware has 400 KB for breaks
(8 classics at 2 bars: ~360 KB) and a break may be at most 106,496 samples (2 bars at 99.4 BPM and up); `./build.sh
bank` builds the bank on its own and says whether it fits. The tool reads integer PCM (8–32 bit) and float WAVs.

Uploading breaks to the FM-1 without rebuilding (the flash's data area, ~356 KB more) is milestone M5.
