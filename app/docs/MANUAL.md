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
5. Patterns and the sequencer
6. The FX chain
7. GLOBAL: MIDI, Bluetooth, pitch, Bluetooth audio, reset
8. The drums: kick, snare, hat
9. The mixer
10. Pages and knobs
11. The control map
12. Saving
13. Troubleshooting

---

## 1. Install

**Try it first:** the emulator runs the same code in the browser, with sound, played with the mouse.

**Install:** open the install page in Chrome or Edge, connect the FM-1 by USB and press Install. While it writes,
the FM-1 says UPDATING and DO NOT UNPLUG: CHOPPAH's slice mark fills in amber with the bar. When it is done, the page
throws confetti. If the FM-1 runs M-VAVE's FM1 Open firmware, put M-VAVE's original firmware back first (M-VAVE's workbench, *Restore original*), then
install CHOPPAH.

![Updating: CHOPPAH's slice mark and the bar filling in amber, about a third written](img/screen-updating.png "UPDATING")

## 2. The screens

At power-on CHOPPAH shows its mark, then the start screen: the loaded break drops in slice by slice. After two
seconds, or at once on any key, the **slicer view** opens. PLAY on the start screen also starts the break.

![The start screen](img/screen-start.png "The start screen")

The slicer view, top to bottom:

- **Header:** play or stop, the break's name, the pattern (P1–P16; "P2>" while P2 waits for the loop's end), REC
  while recording, a level meter (left and right, what leaves after MASTER: amber from
  −8 dB, white near full scale), the tempo.
- **Tabs:** the button you pressed (as printed on the panel), then the page's parts as tabs, the one you are on lit:
  the button again (or ALGORITHM) goes to the next. On EDIT and FX the part edited shows at the right, "< SUB FX >".
  Opening a page, or turning to another part, says the same in the header for a moment: the button, then the part
  ("GLO PITCH", "EDIT KICK SUB FX", "FX SLOT 2 TREMOLO", "SEQ HAT"). The tabs sit right under the header on every
  page, tap tempo's too.
- **The break:** its waveform, the slice markers, the slice sounding lit, the playhead.
- **Slice blocks:** sixteen blocks, one per white key, each with its slice's waveform. The slice sounding is lit;
  a key held is outlined; a dot marks a key with a loop of its own (§3, key loops).
- **Pads:** the eleven black-key pads, lit while held.
- **Knobs:** the page's four knobs and their values.

**The rules.** ALGORITHM picks what you edit on the page (an FX slot, a drum's part, a SEQ lane, GLOBAL's part); on HOME (both its parts) and the MIXER, where there is nothing to pick, it is the break. PRESETS picks what goes
in (an effect, a kick, a snare or hat sound); elsewhere it is the pattern. **SEQ is where everything is sequenced**
(the break's steps and the kick, snare and hat lanes), **EDIT where the drums' sounds are made** (kick, snare, hat);
the white keys play slices everywhere but SEQ, ENV (tap tempo) and ARP (fills). **SEL + ALGORITHM is the break and SEL +
PRESETS the pattern on every page.** A page's button pressed again is its next part. **Press SEL** alone and the strip
lists its combinations (SEL again, or any other button, key or knob, closes it). The control map (§11) shows all of it at once.

![SEL pressed: its combinations on the strip; the HOME knobs](img/screen-selhelp.png "SEL pressed")

## 3. Playing

| Control | Does |
| --- | --- |
| PLAY | Play / stop. HOME + PLAY: back to bar 1 |
| White keys | Slices 1–16. Playing: at the next snap point (GLOBAL: 1 slice, an 8th on the 2-bar breaks, by default; OFF, 1/8 … 4 slices). Stopped: at once. Held: the key loops (below), playing or stopped |
| SELECT | Tempo, 1 BPM a detent (0.1 with SEL held) |
| ENV | **Tap tempo**, a page of its own (below) |
| SEL (pressed alone) | The combinations card: every SEL combination on the strip. SEL used with something else (SEL + a knob, SEL + ALGORITHM, SEL + REC…) does that instead |
| SEL + ALGORITHM · SEL + PRESETS | The break · the pattern, from any page |
| SEL + SAVE | **Undo** the last pattern wipe (SEL + ARP, SEL + REC, a drum lane's FILL, CLR / 4X4 or COPY); again: redo |
| OCT− / OCT+ | Pitch in semitones (hold to go faster). Both: back to 0 |
| PRESETS | The pattern, P1–P16 (§5); while playing the new one starts at the end of the loop |
| SEQ · REC | The sequencer; recording the keys into the pattern (§5). SEL + REC: clear the pattern (SEL + SAVE takes it back) |
| MIDI notes | Notes 48–63 (C3–D#4) play slices 1–16, held like the white keys; 36 the kick, 38 the snare, 42 / 44 / 46 the hat (§8); any channel |
| ALGORITHM | (HOME, MIXER; SEL held: any page) The next or previous break: its name shows at once (amber in the header) and, while playing, it comes in at the next bar (the old one goes quiet for the bar's last 16th meanwhile); stopped, at once. AMEN (The Winstons), DRUMMER (James Brown, Funky Drummer), THE WORM (Jimmy McGriff), THINK (Lyn Collins), COLD SWEAT (James Brown), HOT PANTS (Bobby Byrd), APACHE (Incredible Bongo Band), SPORT (Lightnin' Rod): 2 bars each, played at the tempo you set |
| MASTER | Volume |

### Tap tempo

**ENV** opens TAP TEMPO, which takes the whole screen: the tempo on an LED display, a metronome swinging in time (its
bob lights on each beat, white on the bar's first; BEAT counts 1–4 while playing), this run's taps beside the display. Tap **ENV again** in time, or **any white key** (the keys flash on the beat). The tempo follows
from the second tap and settles as you go on (the average of the last eight taps); each tap flashes the display and
sends a ripple out from the metronome.
Stop for 2 seconds to start a new count; a tap far off the pace starts a new count from the tap before it. **K1** sets
the tempo a BPM at a time, **K2** a tenth, **K3** halves it (left) or doubles it (right), **K4** is the
metronome's **CLICK** level, OFF (the start) up to 100 %: a short tick on every beat while playing, higher on the
bar's first, dry over everything (not through the effects), kept with SAVE. Following an outside clock,
it says EXT CLOCK and waits.

![TAP TEMPO: 160.0 BPM on the LED display, six taps, the metronome on the second beat, a tap's ripple](img/screen-tap.png "TAP TEMPO")

![A slice held](img/screen-held.png "Slice 10 held")

### Key loops

Hold a white key and it loops: a window that starts in its slice plays again and again in time, until you let go
(playing, the break carries on from there; stopped, it ends with the window). Held for a moment, the slice blocks
give way to **the key's view**: its window zoomed to the screen's width, the part this repeat has played lit (from
the right when it runs backwards), the playhead, the slice lines inside the window, this repeat's progress and a dot
for each repeat. A quick tap plays the slice once and keeps the blocks.

While the key is held, **K1–K4 set its loop**, for that key only (SAVE keeps them; a key with a loop of its own has a
dot on its block):

| Knob | Sets | Range |
| --- | --- | --- |
| K1 LENGTH | How long the window is, shown as music for this break | 1/8 of a slice … 16 slices (2-bar breaks: 1/64 … 2 BAR) |
| K2 PITCH | The key's own pitch, on top of OCT | −12 … +12 semitones |
| K3 START | Where in the slice the window starts | 0 … 94 % of the slice |
| K4 MODE | LOOP · PING-PONG (every other repeat backwards) · REVERSE · ONE SHOT (once, then quiet while held) | |

SEL while a key is held puts its loop back to one slice, no pitch, from the start, LOOP. With several keys held the
last one pressed loops; let it go and the lowest still held takes over. Pads still work on top: REV turns the loop
round, ROLL and FREEZE take it over while held.

![A key held: a half-bar ping-pong loop](img/screen-keyloop.png "Key 5 held, 1/2 bar, ping-pong")

### Slice effects

Every slice can have an effect of its own, there whenever that slice plays: in the loop, from its key, in a roll.
Hold the key and press **FX**: K1–K4 turn from its loop to its slice's effect (FX again turns them back; the knobs
stay on the layer you left them on for the next key). A block whose slice has an effect shows its tag (LP, HP, CR,
DR, RM, EC, VB; LV for a level alone). SEL while held resets the layer shown.

| Knob | Sets |
| --- | --- |
| K1 EFFECT | OFF · LO-PASS · HI-PASS · CRUNCH · DRIVE · RING MOD · ECHO (throws the slice into the FX chain's delay) · VERB (throws it into the reverb) |
| K2 AMOUNT | How much: darker, thinner, crunchier, harder, the ring's pitch, the throw's level |
| K3 TONE | The filters' resonance, RING MOD's mix |
| K4 LEVEL | The slice's own level: accents, or ghost notes of single hits |

ECHO and VERB throw only that slice: put a DELAY (or a REVERB) in the FX chain with its SEND (MIX) low, and only the
slices you marked ring out, the classic dub throw. (Without a DELAY slot, ECHO goes into the delay after the chain.)

![Key 5 held, its slice effect: LO-PASS at 80 %, level 80 %](img/screen-slicefx.png "A slice effect")

## 4. The pads

The black keys, left to right, act while held and combine. While a pad is held, the slice blocks give way to an
animation of what it does, and **K1–K4 tune that pad** (the foot shows its settings; SAVE keeps them):

| Pad | Does | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- |
| R8 · R16 · R32 | Roll from the next 16th | Rate (1/4 … 1/64, triplets) | Decay: DOWN (each repeat quieter) or UP (a swell up to full) | Pitch a repeat | Level of the repeats (5–100 %: low for ghost notes) |
| REV | Reverse | Speed | | | |
| FRZ | Freeze: the slice (or part of it) loops | Length (slice, 1/2, 1/4, 1/8) | Pitch | | |
| HLF | Half speed | Speed | | | |
| STP | Tape stop; let go and it spins back up | Stop time (beats) | Spin-up (16ths) | | |
| GAT | Gate | Rate (1/8, 1/16, 1/32) | Length | | |
| RND | Random slices | Amount | Random pitch | | |
| FLT | Throws the DJ filter | Target (LP … HP) | Resonance | Speed | |
| DUB | Throws the break into the delay (stereo ping-pong); the echoes ring on. With a DELAY in the FX chain it throws into that one, and K1–K4 are that DELAY's | Time (1/8, 3/16, 1/4, 3/8; the DELAY's all eight) | Feedback | Tone | Mix (the DELAY's: Send) |

![ROLL held: the caught slice repeating, each copy quieter, RATE / DECAY / PITCH on K1-K3](img/pad-roll8.png "ROLL held")
![TAPE STOP held: two reels slowing down](img/pad-stop.png "TAPE STOP held") ![DUB held: echoes marching out](img/pad-dub.png "DUB held")

## 5. Patterns and the sequencer

CHOPPAH holds **16 patterns**. Each is the break's 32 steps (2 bars), each step playing the loop's own slice (LOOP,
where the generator can move it), a slice of its own, or nothing; and three drum lanes of 16 steps (kick, snare, hat)
that repeat every bar. **PRESETS** picks the pattern on most pages; while playing, the next one starts when the loop
comes round (the header shows "P2>").

**SEQ** opens the sequencer. **ALGORITHM** (or **SEQ again**) picks what the white keys edit:

![SEQ: pattern 1, steps 1-16, step 9 a slice of its own being reversed; the kick, snare and hat lanes below](img/screen-seq.png "The SEQ page")

| Lane | White keys | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- |
| STEPS 1-16, STEPS 17-32 | Pick a step to edit (outlined) | SLICE: OFF · LOOP · 1–16 | PITCH: −12 … +12 | HITS: ×1–×4 (a ratchet in the step) | REVERSE |
| KICK, SNARE, HAT | That lane's 16 steps, on and off | FILL: 0–16 hits spread evenly from the downbeat (Euclidean) | ROTATE: the lane a step later (right) or earlier | Left CLEAR, right four on the floor | COPY the previous pattern's lane |

The upper strip shows the half's 16 steps (a number: its own slice; a dot: LOOP; a dash: off; a mark under it: hits
or reverse), the step playing lit; the lower strip all three drum lanes.

**REC** records: while it is lit and the break plays, every white key you press (on HOME and the other pages where
the keys play slices) is written into the pattern at the step it lands on, and so are MIDI notes: 48–63 as slices,
36 / 38 / 42 into the kick, snare and hat lanes. REC again stops. **SEL + REC** clears: on SEQ the lane shown, on any
other page the whole pattern. SEL + ARP (a random pattern) writes into the pattern playing. **SEL + SAVE undoes**
either (and FILL, CLR / 4X4 and COPY on a drum lane); press it again to redo. SAVE keeps all 16.

### Fills

**ARP** opens FILL: eight fills to throw, each a few steps of the loop played another way, then the pattern again.

![FILL: KICK thrown at once (lit), ROLL picked (outlined), ROLL's steps drawn below](img/screen-fill.png "FILL")

| White key | Fill | What it plays |
| --- | --- | --- |
| 1 / 9 | ROLL | The slice it began on, again and again, faster towards its end |
| 2 / 10 | SNARE | A snare rush: the break's snare (its beat 2) faster and faster |
| 3 / 11 | KICK | A kick rush: the break's first hit faster and faster |
| 4 / 12 | RANDOM | Random slices at random pitches (DENSITY doubles some) |
| 5 / 13 | REVERSE | The pattern's own slices, backwards |
| 6 / 14 | STOP | A tape stop that comes to rest on the fill's last step |
| 7 / 15 | DROP | Nothing: the break drops out (the drums play on) |
| 8 / 16 | HALF | The pattern at half speed |

Keys **1–8** throw a fill so that it ends on the next downbeat (a 2-bar fill: on the loop's end); keys **9–16** throw
it at once. While it plays its card and keys light; a fill waiting for its moment is outlined amber.

| Knob | Sets |
| --- | --- |
| K1 LENGTH | 1/4 bar · 1/2 bar (the start) · 1 bar · 2 bars |
| K2 PITCH | −12 … +12 semitones, reached by the fill's last step: a riser up, a drop down (RANDOM: how far its pitches go) |
| K3 DENSITY | ×1 … ×4 hits a step to start (ROLL, SNARE, KICK speed up to three times that) |
| K4 AUTO | OFF · every 2, 4, 8 or 16 bars: the fill picked plays by itself at the end of each phrase |
| ALGORITHM | The fill picked: the auto fill's, and the one drawn step by step under the cards |

SAVE keeps the settings.

## 6. The FX chain

**FX** opens the master effects chain: four slots in series, the break (through its DJ filter, with the kick when it
goes through the chain) into slot 1, then 2, 3 and 4, then on to the drums when they go around the chain, the DUB
pad's delay and the limiter. Each slot holds any effect (two of one are fine), with
three settings, a mix and a bypass. The tiles show the chain in order (lit: on), the one you tune outlined; below,
its settings as bars over K1–K4.

![The FX chain: CRUNCH, then TREMOLO, then DRIVE, RING MOD bypassed](img/screen-fxchain.png "The FX chain")

| Control | Does |
| --- | --- |
| ALGORITHM · FX again | The slot to tune (1–4) |
| PRESETS | The effect in that slot, through the library (EMPTY first); a new effect comes in on, at its own starting settings |
| K1–K3 · K4 | Its settings · its mix |
| SEL + FX | That slot on / bypassed |
| FX held + ALGORITHM | Moves that slot along the chain (the order is the sound: DRIVE before a FILTER is not a FILTER before DRIVE) |
| MIXER K4 FX | The whole chain against the dry sound: fade every effect in or out at once (the FX page shows it, bottom right) |

| Effect | K1 | K2 | K3 |
| --- | --- | --- | --- |
| FILTER | Cutoff (30 Hz – 16 kHz) | Resonance | Mode: LP · BP · HP |
| CRUNCH | Bits (16 → 2) | Rate (44.1 kHz → 1 kHz) | Drive |
| DRIVE | Gain (×1 – ×30) | Tone (dark → bright) | Level |
| PUNCH | Amount (a drum-bus compressor; low MIX for parallel squash) | Attack (0.1 – 30 ms) | Release (20 – 500 ms) |
| FLANGER | Rate (0.02 – 10 Hz) | Depth | Feedback (−90 … +90 %) |
| RING MOD | Frequency (20 Hz – 2 kHz) | Spread (the right side higher) | |
| TREMOLO | Rate on the tempo (1/2 … 1/32, 1/8T) | Depth | Pan (0 tremolo … 100 % auto-pan) |
| REVERB | Size (short room … long hall) | Tone (bright … dark) | Pre-delay (0 – 60 ms) |
| DELAY | Time on the tempo (1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 1/8T, 1/4T) | Feedback | Tone (dark … bright repeats) |
| EQ | Low (a shelf at 120 Hz) | Mid (a bell; K4 is its frequency, 200 Hz – 5 kHz) | High (a shelf at 8 kHz) |
| TAPE | Drive (0 – 24 dB into a tape curve: denser, warmer, even harmonics; the peaks kept level) | Warmth (the low head bump, the top rolled off) | Wow (and flutter: the pitch wanders like a worn machine) |
| MB COMP | Low (below 200 Hz) | Mid | High (above 2.5 kHz) |
| CLIP | Drive (0 – +24 dB into it) | Knee (soft … hard) | Ceiling (0 – −12 dB) |

**EQ**'s bands go −12 … +12 dB, flat in the middle; its K4 is not a mix but the **mid's frequency**. **TAPE** is a
saturator after a tape machine: put it last in the chain to glue the break, or before CRUNCH for a dusty sampler.

**MB COMP** is a 3-band (multiband) compressor. Each knob is how hard its band is pressed, shown as the band's ratio
(OFF … 8:1; the threshold drops with it, down to −30 dB, and half the reduction is made back up). The low band is
slow, the high band fast; at OFF the bands add back up to the sound untouched. Its MIX is a parallel mix: the
pressed sound against the dry. Amber bars under K1–K3 show each band's reduction as it happens. **CLIP** squares the
peaks off at its ceiling: soft rounds them, hard cuts them; below the ceiling it only adds its drive. MB COMP then
CLIP last in the chain is a loud master; CLIP before a FILTER is a fizzy one.

![MB COMP in slot 1: the low band pressed 7.3:1, its reduction in amber under K1](img/screen-mbcomp.png "MB COMP")

**DELAY** is a stereo ping-pong: the echoes bounce left, right, left, each darker. Its K4 is **SEND**, how much of the
sound reaching it goes in (the echoes come back at full), so with SEND low only throws ring out: the DUB pad, the
the slices' ECHO. There is one delay (0.74 s at most: 1/2 at 162 BPM and up; slower, it holds the
longest it can).

**REVERB** is a plate (after Dattorro's design: diffusers, then a modulated stereo tank): its MIX is how much reverb
goes over the dry sound. The tail rings on after STOP. There is one: a second REVERB slot passes the sound on. Put it
late in the chain to reverb the effects before it, or early to crunch or drive the reverb itself.

The chain starts as CRUNCH → FILTER → FLANGER → RING MOD, all bypassed: SEL + FX switches the focused one on.
CRUNCH used to be HOME's K4; it is slot 1 now.

STOP fades the break out at once (a white key held keeps looping); the reverb's and the dub delay's tails ring on.

## 7. GLOBAL: MIDI, Bluetooth, pitch, Bluetooth audio, reset

**GLO** opens the GLOBAL page: the slice blocks give way to the state of MIDI, and K1–K4 set it (SAVE keeps it).
**GLO again** (or ALGORITHM) turns to its second part, the pitch; **a third time**, Bluetooth audio (both below).

![GLOBAL, following a 119 BPM clock over USB](img/screen-global.png "The GLOBAL page")

| Knob | Sets | |
| --- | --- | --- |
| K1 CLOCK IN | OFF · FOLLOW | Following, MIDI clock from USB, the TRS jack or Bluetooth sets the tempo (the header says EXT; SELECT and tap tempo wait), START plays from bar 1, STOP stops, and the loop eases onto the clock's beat every beat |
| K2 CLOCK OUT | OFF · ON | While playing, 24 clocks a beat (straight, whatever the swing); PLAY sends START, stopping sends STOP, HOME + PLAY sends START again |
| K3 BLUETOOTH | ON · OFF | Bluetooth MIDI: the FM-1 shows up as **CHOPPAH XXXX** in a computer's, phone's or tablet's Bluetooth MIDI settings. Off ends the link and stops advertising; at power-on it starts as saved |
| K4 MIDI TO | USB+BT · USB · BT | Where the clock goes |

The panel shows Bluetooth's state (ready to pair, connected), the clock coming in (its tempo, where from, running or
stopped), the clock going out, and a light for MIDI in and out on each port. The FM-1 has no MIDI out jack: MIDI goes
out over USB and Bluetooth only. On an iPhone or iPad, Bluetooth MIDI devices never show in Settings → Bluetooth:
connect from a MIDI app's Bluetooth MIDI page (GarageBand: a song's Settings → Advanced → Bluetooth MIDI Devices).

**Pitch and snap (GLO twice):**

| Knob | Sets | |
| --- | --- | --- |
| K1 PITCH | KEEP LENGTH (the default) · SPEED | KEEP LENGTH changes the pitch only: each slice keeps its length (a granular pitch shift), so a key pitched +7 still fills its loop. SPEED is the classic repitch: up plays faster and shorter, down slower and longer (K1 left) |
| K2 GRAIN | 10 … 80 ms | KEEP LENGTH's grains: shorter keeps hits tight, longer sounds smoother on held sounds |
| K3 SNAP | OFF · 1/8 · 1/4 · 1/2 · 1 (the default) · 2 · 4 slices | Where a white key pressed while playing lands: at once (OFF), or at the next point of a grid that many slices apart, counted from the top of the pattern. The panel says what it is in this break (on the 2-bar breaks 1 slice = 1/8, 1/2 slice = 1/16, 2 slices = 1/4). Stopped, keys always play at once |
| K4 RESET ALL | Turn once ("RESET ALL?"), then again within 3 seconds | Clears everything back to how it came: tempo, generator, FX chain, kick, snare and hat, mixer, keys, slices, GLOBAL, Bluetooth audio, and all 16 patterns; the first break is chosen and it is saved at once. Bluetooth pairings are kept. The screen says so: LOBOTOMIZED, for 3 seconds (a key or button ends it sooner) |

It covers every pitch in semitones: OCT, a key's pitch, the roll's pitch a repeat, the freeze's pitch, steps. The
tempo's own repitch, HALF, REVERSE's speed and the tape stop stay speed effects.

![RESET ALL done: the lobotomy screen](img/screen-lobotomized.png "LOBOTOMIZED")

**Bluetooth audio (GLO three times):** CHOPPAH plays through Bluetooth headphones or a Bluetooth speaker.

![Bluetooth audio, streaming to a speaker](img/screen-btaudio.png "Bluetooth audio")

| Knob | Does |
| --- | --- |
| K1 BT AUDIO | OFF · ON. On at power-on as saved (SAVE), and then it connects the last device again by itself |
| K2 DEVICE | Picks from the list: the devices it remembers (LAST, KNOWN), then the ones a scan found (NEW) |
| K3 SCAN | Any turn looks for headphones and speakers for about 10 seconds. Put the device in pairing mode first |
| K4 CONNECT | Right: connect the device chosen (a new one pairs and is remembered; four are kept). Left: hang up. SEL held + left: forget the remembered device chosen |

While it streams, the FM-1's own speaker and headphone output go quiet and MASTER still sets the level. It is stereo,
as the FM-1's own output: the ping-pong delay, the reverb and the flanger spread left and right. **K3 while streaming
is TEST L/R:** a low beep on the left, then a high beep on the right, twice; if you hear both beeps on one side, the
headphones or speaker are playing one channel. **K2 while streaming is CODEC:** JOINT (joint stereo, the default) or
STEREO (plain stereo). If only one side plays, switch it and run the test again: some speakers decode one better than
the other. Bluetooth
audio runs late: 150–300 ms between a key and your ear, so it is for listening and showing, not for playing in time
(use the FM-1's own output or USB for that). Bluetooth MIDI keeps working beside it on the same radio. Turning
Bluetooth audio on starts the radio even when Bluetooth MIDI is off.

## 8. The drums: kick, snare, hat

Three synthesised drums play under the break, in time with it (swing included), on the pattern's drum lanes (§5):
their steps are SEQ's KICK, SNARE and HAT lanes; their sounds are made on **EDIT**. They go through the FX chain or
around it together (MIXER GROOVE K2).

**EDIT** opens the drum edited last; **EDIT again** goes to the next: KICK → SNARE → HAT → KICK (the tabs show which).
**ALGORITHM** turns through the drum's parts, **PRESETS** loads a built-in sound. The lower strip shows the three
drums side by side, each with its waveform (one hit, drawn again as you change the sound), the one edited outlined,
lit on its hits. The white keys keep playing slices.

### The kick

A synthesised kick for gabber, hardcore and techno. ALGORITHM turns through its parts: MIX → SUB → SUB FX → PUNCH →
PUNCH FX → GRIT → GRIT FX → CLICK → CLICK FX → MACRO → OUT (each layer's part is its LAYER knobs, its FX part its
LAYER FX knobs).

![EDIT on the kick: its part's settings as bars, the layers' levels, the three drums below](img/screen-kick.png "The kick on EDIT")

The kick is four layers summed and pushed into a clipper:

| Layer | Is | PITCH | DECAY |
| --- | --- | --- | --- |
| SUB | The low end: a sine dropping onto its pitch (TONE saturates it) | 30 – 90 Hz | 60 – 1500 ms |
| PUNCH | The hit and the body: a sine swept down fast (TONE: how far it sweeps) | 50 – 200 Hz | 20 – 400 ms |
| GRIT | The dirty mids: FM through a drive (TONE: more FM and noise) | 40 – 400 Hz | 20 – 800 ms |
| CLICK | The attack: a click and noise (TONE: from click to noise) | 1 – 12 kHz | 1 – 40 ms |

| Control | Does |
| --- | --- |
| ALGORITHM | The part: MIX, a layer (SUB, PUNCH, GRIT, CLICK), its FX, MACRO, OUT |
| PRESETS | A built-in kick: 909, TECHNO, EARLY HC, ROTTERDAM (the start), INDUSTRIAL, DISTORTO. The steps, routing and volume stay |
| MIDI note 36 (C1) | Plays the kick, from any channel, also while stopped |

| Part | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| MIX | SUB level | PUNCH level | GRIT level | CLICK level |
| SUB · PUNCH · GRIT · CLICK | Level | Pitch | Decay | Tone |
| … FX | Effect: OFF · DRIVE · CRUNCH · FILTER · RING MOD | A (gain · bits · cutoff · frequency) | B (tone · rate · resonance) | Wet |
| MACRO | SUB (level, tail, weight) | PUNCH (level, sweep, click) | GRIT (level, dirt, tail) | INTENSITY |
| OUT | Volume | | | |

**INTENSITY** is the drive into the clipper: low is a clean, round kick; past half the clip turns hard, the tail
flattens into the long, distorted gabber kick. The page is laid out as the snare's and the hat's: the part's four
settings as bars, under them the four layers' levels (the layer edited lit, amber with an effect on); the kick's
shape is its box in the strip below. Its steps: SEQ's KICK lane (empty to start).

### The snare and the hat

![EDIT on the hat: its six settings, the three drums below](img/screen-perc.png "The hat on EDIT")

**EDIT** again from the kick: the snare, then the hat. **ALGORITHM** turns between its SOUND and SHAPE knobs,
**PRESETS** picks a built-in sound. Their steps: SEQ's SNARE and HAT lanes.

| Voice | Is | Presets |
| --- | --- | --- |
| SNARE | Two tuned sines with a pitch drop (the body) and band-passed noise (the snares), with a crack on the hit | 909 · TIGHT · FAT · GABBER |
| HAT | Six square waves at the 808's ratios and noise, through two high-passes | CLOSED · 808 · OPEN · TRASH |

| Knobs | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| SOUND | Level | Tune | Decay | Tone (the hat: METAL / NOISE) |
| SHAPE | Snap (the hat: CUT, the high-pass) | Drive | | |

MIDI notes 38 (D1) play the snare, 42, 44 and 46 the hat, also while stopped. SAVE keeps the drums with the rest.
(ENV, which used to open SNARE / HAT, is tap tempo now: §3.)

## 9. The mixer

**LFO** opens the MIXER: four channels side by side, each a fader with a level meter (FX shows the chain's four
slots, lit: on). **LFO again** turns to GROOVE.

![The MIXER: the break, the kick, the snare and hat, the FX chain](img/screen-mixer.png "The MIXER")

| Half | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| LEVELS | BREAK: the break's level (its throws into the delay and the reverb too) | KICK: the kick's level (the KICK page's VOLUME) | PERC: the snare and the hat together | FX: the master FX chain against the dry sound |
| GROOVE | SWING (50–75 %) | DRUMS: through the FX chain or around it | | |

FX is how much of the chain you hear (the drums routed around it stay dry). Bring BREAK down to drop the break and
let the drums run alone, then up again for the drop. HOME keeps the gate, FX and COMPLEXITY to ride (K1, K3, K4) and the **DJ filter** (K2), which filters the
break alone: sweep it down and the drums stay full on top. The FLT pad throws it the same way.

## 10. Pages and knobs

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| HOME (perform) | Gate | DJ filter on the break (left low-pass, right high-pass) | FX (the MIXER's) | Complexity (the GENERATOR's) |
| LFO (mixer) | Break level | Kick level | Snare and hat level | FX (the chain, dry to wet) |
| LFO again (groove) | Swing | Drums: through the chain or around it | | |
| HOME again (generator) | Complexity | Anchor | Roll | Fill |
| FX (the chain) | the focused slot's effect: its three settings | | | Its mix |
| SEQ (patterns) | Slice | Pitch | Hits | Reverse |
| SEQ, a drum lane | Fill | Rotate | Clear / 4x4 | Copy |
| GLO (global) | Clock in | Clock out | Bluetooth | MIDI to |
| GLO again (pitch, snap) | Keep length / speed | Grain | Snap | Reset all (twice) |
| GLO a third time (BT audio) | On / off | Device | Scan | Connect / hang up |
| ARP (fill; white keys throw fills) | Length | Pitch | Density | Auto |
| ENV (tap tempo; ENV again or a white key: a tap) | Tempo | Fine (0.1) | Half / double | Click level (0: off) |
| EDIT (kick; ALGORITHM its part) | see §8: MIX, SUB … CLICK FX, MACRO, OUT | | | |
| EDIT again (snare, then hat) | Level · Snap / cut | Tune · Drive | Decay | Tone |

**Gate** (HOME K1) cuts every slice the loop plays short: at 50 % each slice sounds for half its length, then
quiet until the next; down to 5 % for clipped, staccato chops; 100 % is off. It follows the tempo and the pitch, and
the slice lit in the waveform shrinks with it. It covers the loop's slices, ratchets and the RANDOM pad; key loops
(their LENGTH) and rolls keep their own; FREEZE holds the slice whole.

The generator rearranges the break as it plays: **complexity** is how often it jumps, **anchor** keeps the kick and
snare on the beats, **roll** repeats a slice or takes the one next to it, **fill** breaks the rules in the last bar
of a phrase; it is HOME's second part (HOME again). SEL + ARP writes a random pattern (ARP alone opens the fills).

## 11. The control map

Every page, button, encoder and key in one place. On the [site's manual](https://profikid.github.io/fm1-break-choppah/manual/#controls)
this section is interactive: pick a page and the screen, the seven encoders and the keys show what they do there;
below it, each button with its parts, the whole map as one table, and which of a held key, a held pad or the page
owns K1–K4. Here, in short: §2 has the rules, §10 every page's knobs.

## 12. Saving

SAVE keeps the tempo, pitch, swing, generator, the FX chain, the break chosen, each key's loop, the drums, the
mixer, the GLOBAL settings and all 16 patterns; SAVE is lit when something changed. A new version keeps what an older
one saved: new settings start at their defaults. (Version 0.32 changed the layout: what 0.31 and older saved starts
over at the defaults once; Bluetooth pairings stay.) GLOBAL's RESET ALL clears it all on purpose (§7).

## 13. Troubleshooting

- **No sound:** turn MASTER up; press PLAY.
- **A crash screen:** press a key to restart. Two failed starts in a row start safe mode, which the installer can
  still reach.
- **The install stops:** press Install again; it finishes a write that was cut off.
