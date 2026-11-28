#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The layer (docs/PRD_LAYER.md) in the host simulator: a pack with two loops (the first the overlay's), three one-shots
# (one a CUT fill) and its own effect pads (app/site/chpk.js, as the hub sends it to CHOPPAH 1.8, INFO 5). On ARP the
# white keys fire the shots, the CUT one silences the break while it sounds; black key 1 tapped latches its loop at
# the bar, black key 2 held plays while held; ARP held on HOME fires a shot and stays on HOME; the pack's pads play
# while its breaks do (a built-in break: the FM-1's own); GLOBAL's LAYER sets the voices; a shot's setting edited on
# ARP survives SAVE and a reboot. Needs ./build.sh --host first.
#   tests/layer.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/layer
mkdir -p "$O"
C=breaks/classics
H=app/build/host/choppah_host
python3 tools/cu_frames.py --out "$O/v1.bin" $C/lyn_collins_think_about_it.wav 113.78 "P THINK" $C/the_winstons_amen_brother.wav 141.82 "P AMEN" > /dev/null
node --input-type=module - "$O" <<'N'
const P = await import(process.cwd() + "/app/site/chpk.js");
import { readFileSync, writeFileSync } from "node:fs";
const o = process.argv[2], bk = P.readBank(new Uint8Array(readFileSync(`${o}/v1.bin`)));
const tone = (n, rate, hz, a = 0.5) => P.adpcmEncode(Float32Array.from({ length: n }, (_, i) => a * Math.sin(2 * Math.PI * hz * i / rate)));
const ovLen = Math.round(4 * 240 / 113.78 * 11025), l2 = Math.round(2 * 240 / 113.78 * 11025);
const ov = { rate: 11025, len: ovLen, bpm: 113.78, bars: 4, data: tone(ovLen, 11025, 110) };
const loops = [{ name: "BASS", rate: 11025, len: ovLen, bpm: 113.78, bars: 4, quant: "bar" },
  { name: "PAD", rate: 11025, len: l2, bpm: 113.78, bars: 2, quant: "bar", data: tone(l2, 11025, 330) }];
const sh = (name, n, hz, more = {}) => ({ name, rate: 16000, len: n, data: tone(n, 16000, hz, 0.7), ...more });
const shots = [sh("BOMBA", 8000, 600, { choke: 1 }), sh("FILL", 30000, 200, { cut: true }), sh("GATED", 30000, 900, { mode: "gate" })];
const pads = P.factoryPads(); pads[0] = { ...P.padDefaults(8), mix: 0.6 };   /* pad 1: a REVERB of the pack's */
const meta = { id: P.uuid(), name: "LAYER TEST", author: "CI", slots: [{}, {}] };
const [b1, b2] = P.deviceBanks(bk.slots, 2, meta, { overlay: ov, loopChunk: P.loopChunk(loops, true),
  shotChunk: P.shotChunk(shots), padsChunk: P.padsChunk(pads) });
writeFileSync(`${o}/b1.bin`, b1); if (b2) writeFileSync(`${o}/b2.bin`, b2);
N
up() {  # a bank image -> its upload frames
python3 - "$1" "$2" <<'P'
import sys; sys.path.insert(0, 'tools'); import cu_frames as c
img = open(sys.argv[1], "rb").read()
for kind, f in c.frames(img, int(sys.argv[2])):
    print("sysex " + c.hexs(f)); print("wait 20" if kind == "data" else "wait 200")
P
}
{
printf 'wait 2600\ntap HOME\nwait 300\n'
up "$O/b1.bin" 1; [ -f "$O/b2.bin" ] && up "$O/b2.bin" 2
cat <<S
expect n_user 2
expect ly_nl 2
expect ly_ns 3
expect ly_pads 1
expect pfx0_type 8
expect ly_on 0
tap PLAY
wait 600
expect ly_on 0
tap GLO
wait 100
tap GLO
wait 100
tap GLO
wait 100
tap GLO
wait 200
expect gpage 3
turn K1 1
wait 100
tap ARP
wait 200
expect page 9
key w0 down
wait 50
expect ly_shots 1
expect arp_kind 2
key w0 up
wait 800
expect ly_shots 0
key w1 down
wait 60
expect ly_cut 1
key w1 up
wait 2400
expect ly_cut 0
key w2 down
wait 200
expect ly_shots 4
key w2 up
wait 100
expect ly_shots 0
key b0 down
wait 60
key b0 up
wait 30
expect ly_lat 1
wait 2300
expect ly_on 1
# LY-054: SAVE while a loop and a one-shot are active; fade both out, write, then restore the loop.
key w0 down
wait 30
key w0 up
wait 20
tap SAVE
wait 100
expect ly_shots 0
expect ly_on 1
shot layer_loop.png
peakreset
wait 400
expect ov_loud 1
key b1 down
wait 500
expect ly_on 3
key b1 up
wait 100
expect ly_on 1
# LY-054: stop transport, then receive the same pack while its loop source is still resident.
tap PLAY
wait 100
S
up "$O/b1.bin" 1 >> "$O/s.txt"
[ -f "$O/b2.bin" ] && up "$O/b2.bin" 2 >> "$O/s.txt"
cat <<S
expect ly_on 0
key b0 down
wait 60
key b0 up
wait 2300
expect ly_on 0
tap HOME
wait 200
press ARP
wait 50
key w0 down
wait 40
expect ly_shots 1
key w0 up
release ARP
wait 200
expect page 0
tap ARP
wait 200
key w2 down
wait 50
key w2 up
wait 100
expect arp_kind 2
shot layer_shot.png
turn K3 3
wait 100
expect ly_s2_decay 3
tap SAVE
wait 500
tap PLAY
wait 200
reboot
wait 2600
tap HOME
wait 300
expect ly_ns 3
expect ly_s2_decay 3
turn ALGO 2
wait 300
expect brk 2
expect ly_pads 0
expect pfx0_type 1
S
} > "$O/s.txt"
$H "$O/s.txt" app/build/scenarios > "$O/run.log" 2>&1 || { grep -E "FAIL|host:" "$O/run.log"; exit 1; }
grep -q "EXPECTATIONS FAILED" "$O/run.log" && { grep FAIL "$O/run.log"; exit 1; }
echo "layer: a pack with loops (the overlay as L1), one-shots and its own pads read; the shots on ARP's white keys, a CUT fill silencing the break, a GATE shot stopping on release; loop 1 tapped latched on the bar and off again, loop 2 held playing while held; ARP held on HOME fires a shot and stays on HOME; the pack's pads while its breaks play, the FM-1's under a built-in one"
