# CHOPPAH

CHOPPAH is an Amen chopper for the M-VAVE FM-1. A break is cut into sixteen slices, one on each white key, and
plays in time while you rearrange it, roll it, reverse it, stop it like a tape and push it through effects.

![CHOPPAH's slicer view while playing](img/screen-slicer.png "The slicer view")

---

## Contents

1. Install
2. The screens
3. Playing
4. The pads
5. Pages and knobs
6. Saving
7. Troubleshooting

---

## 1. Install

**Try it first:** the emulator runs the same code in the browser, with sound, played with the mouse.

**Install:** open the install page in Chrome or Edge, connect the FM-1 by USB and press Install. If the FM-1 runs
M-VAVE's FM1 Open firmware, put M-VAVE's original firmware back first (M-VAVE's workbench, *Restore original*), then
install CHOPPAH.

## 2. The screens

At power-on CHOPPAH shows its mark, then the start screen: the loaded break drops in slice by slice. After two
seconds, or at once on any key, the **slicer view** opens. PLAY on the start screen also starts the break.

![The start screen](img/screen-start.png "The start screen")

The slicer view, top to bottom:

- **Header:** play or stop, the break's name, the tempo.
- **The break:** its waveform, the slice markers, the slice sounding lit, the playhead.
- **Slice blocks:** sixteen blocks, one per white key, each with its slice's waveform. The slice sounding is lit;
  a key held is outlined.
- **Pads:** the eleven black-key pads, lit while held.
- **Knobs:** the page's four knobs and their values.

## 3. Playing

| Control | Does |
| --- | --- |
| PLAY | Play / stop. HOME + PLAY: back to bar 1 |
| White keys | Slices 1–16. Playing: at the next 16th. Stopped: at once. Held: that slice at every 16th |
| SELECT | Tempo, 1 BPM a detent (0.1 with SEL held). Tap SEL four times: tap tempo |
| OCT− / OCT+ | Pitch in semitones (hold to go faster). Both: back to 0 |
| ALGORITHM | The next or previous break |
| MASTER | Volume |

![A slice held](img/screen-held.png "Slice 10 held")

## 4. The pads

The black keys, left to right, act while held and combine:

| Pad | Does |
| --- | --- |
| R8 · R16 · R32 | Roll at 1/8, 1/16, 1/32 from the next 16th. R32 with R8 or R16: triplets |
| REV | Reverse |
| FRZ | Freeze: the slice sounding loops |
| HLF | Half speed |
| STP | Tape stop over a beat; let go and it spins back up |
| GAT | Gate: the first half of every 16th |
| RND | A random slice at every 16th |
| FLT | Throws the DJ filter to low-pass |
| DUB | (coming) dub delay throw |

![A roll](img/screen-roll.png "ROLL 1/16 held")

## 5. Pages and knobs

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| HOME (perform) | DJ filter (left low-pass, right high-pass) | Swing | Complexity | Crunch |
| ARP (generator) | Complexity | Anchor | Roll | Fill |
| FX | the focused effect's settings and mix (ALGORITHM picks the effect, FX again turns it on or off) | | | |

The generator rearranges the break as it plays: **complexity** is how often it jumps, **anchor** keeps the kick and
snare on the beats, **roll** repeats a slice or takes the one next to it, **fill** breaks the rules in the last bar
of a phrase. SEL + ARP writes a random pattern.

## 6. Saving

SAVE keeps the tempo, pitch, swing, generator, effects and the break chosen; SAVE is lit when something changed.

## 7. Troubleshooting

- **No sound:** turn MASTER up; press PLAY.
- **A crash screen:** press a key to restart. Two failed starts in a row start safe mode, which the installer can
  still reach.
- **The install stops:** press Install again; it finishes a write that was cut off.
