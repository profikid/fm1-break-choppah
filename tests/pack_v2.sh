#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Packs v2 (docs/PRD_PACKS.md P1) in the host simulator: GETSET reads the scenes back; a v2 pack (app/site/chpk.js
# deviceBanks, as the builder sends it to CHOPPAH 1.7) brings its own scenes (scene A's phrase changed), each break's
# settings (a 1/2-bar window from step 8, -6 dB, +3 semitones, swing 62, the first four slices kicks; the second break's
# tempo over its slot's; its theme while its breaks play; the window holds 5 of THINK's 16 slices, 4 times: 20) and an overlay in bank 2 alone; they hold through a reboot, and deleting the pack brings the
# factory scenes back. Needs ./build.sh --host first.
#   tests/pack_v2.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/pack_v2
mkdir -p "$O"
C=breaks/classics
H=app/build/host/choppah_host
python3 tools/cu_frames.py --out "$O/v1.bin" $C/lyn_collins_think_about_it.wav 113.78 "P THINK" $C/the_winstons_amen_brother.wav 141.82 "P AMEN" > /dev/null
gs() {  # GETSET frames: the scenes and the chain, 256 bytes a frame
python3 - <<'P'
import sys; sys.path.insert(0, 'tools'); import cu_frames as c
for off in range(0, 1700 + 640, 256):
    print("sysex " + c.hexs(c.frame(7, off.to_bytes(4, 'little') + (256).to_bytes(4, 'little'))))
    print("wait 30")
P
}
blob() {  # the GETSET answers in a log -> the bytes
python3 - "$1" "$2" <<'P'
import sys
raw, out = open(sys.argv[1], "rb").read(), bytearray(2048)
for f in raw.split(b"\xf0")[1:]:
    f = f.split(b"\xf7")[0]
    if f[:4] != bytes([0x7D, 0x43, 0x48, 0x47]) or f[4] != 0:
        continue
    p, u = f[5:], bytearray()
    for i in range(0, len(p), 8):
        u += bytes(p[i + j] | ((p[i] >> (j - 1)) & 1) << 7 for j in range(1, min(8, len(p) - i)))
    off = int.from_bytes(u[:4], "little"); out[off:off + len(u) - 4] = u[4:]
open(sys.argv[2], "wb").write(out[:1692 + 640])
P
}
# 1. the factory's scenes, read back
{ printf 'wait 2600\ntap HOME\nwait 300\nexpect mq_tag 0\nsysex_log %s/gs1.syx\nsysex_out\n' "$PWD/$O"; gs; printf 'wait 100\n'; } > "$O/s1.txt"
$H "$O/s1.txt" app/build/scenarios > "$O/run1.log" 2>&1 || { grep -E "FAIL|host:" "$O/run1.log"; exit 1; }
blob "$O/gs1.syx" "$O/gs1.bin"
# 2. the v2 pack: scene A's phrase 8 bars, its breaks' settings, an overlay
node --input-type=module - "$O" <<'N'
const P = await import(process.cwd() + "/app/site/chpk.js");
import { readFileSync, writeFileSync } from "node:fs";
const o = process.argv[2], gs = readFileSync(`${o}/gs1.bin`), scenes = new Uint8Array(gs.subarray(0, 1604));
if (new DataView(scenes.buffer).getUint32(0, true) !== 0x3553434D) throw new Error("GETSET: no MCS5 scenes");
const bars0 = scenes[4 + 96]; scenes[4 + 96] = bars0 === 8 ? 4 : 8;
writeFileSync(`${o}/sc0.txt`, String(scenes[4 + 96]));
const bk = P.readBank(new Uint8Array(readFileSync(`${o}/v1.bin`)));
const meta = { id: P.uuid(), name: "V2 TEST", author: "CI", theme: { bg: "#101820", hi: "#33ffcc" }, slots: [
  { loop_steps: 8, loop_start: 8, gain_db: -6, pitch: 3, swing: 62, roles: "KKKK" }, { bpm: 150 }] };
const ovLen = Math.round(4 * 240 / 113.78 * 11025);
const ov = { rate: 11025, len: ovLen, bpm: 113.78, bars: 4, data: P.adpcmEncode(Float32Array.from({ length: ovLen }, (_, i) => 0.5 * Math.sin(2 * Math.PI * 110 * i / 11025))) };
const [b1, b2] = P.deviceBanks(bk.slots, 2, meta, { scenes, overlay: ov });
writeFileSync(`${o}/b1.bin`, b1); writeFileSync(`${o}/b2.bin`, b2);
N
up() {  # a bank image -> its upload frames
python3 - "$1" "$2" <<'P'
import sys; sys.path.insert(0, 'tools'); import cu_frames as c
img = open(sys.argv[1], "rb").read()
for kind, f in c.frames(img, int(sys.argv[2])):
    print("sysex " + c.hexs(f)); print("wait 20" if kind == "data" else "wait 200")
P
}
SC0=$(cat "$O/sc0.txt")
{
printf 'wait 2600\ntap HOME\nwait 300\n'
up "$O/b1.bin" 1; up "$O/b2.bin" 2
cat <<S
expect n_user 2
expect pk_tag 1
expect mq_tag 1
expect sc0_bars $SC0
expect brk 0
expect tile 8
expect nslices 20
expect slot_pitch 3
expect slot_swing 62
expect role0 1
expect pal_hi 14329
expect th_bg 4292
expect ov_have 1
expect ov_loud 0
tap PLAY
wait 2000
expect ov_loud 1
tap LFO
wait 200
turn K2 -50
wait 400
expect ov_loud 0
turn K2 50
wait 300

tap HOME
wait 200
tap PLAY
wait 600
turn ALGO 1
wait 300
expect brk 1
expect brk_bpm 15000
expect slot_pitch 0
expect tile 32
reboot
wait 2600
tap HOME
wait 300
expect pk_tag 1
expect sc0_bars $SC0
expect tile 8
tap LFO
wait 150
tap HOME
wait 150
tap HOME
wait 150
tap HOME
wait 150
tap HOME
wait 300
expect page 7
expect page 7
turn K1 -1
wait 300
expect tile 16
sysex_log $PWD/$O/gs2.syx
sysex_out
S
gs
cat <<S
wait 100
sysex_log off
tap SAVE
wait 500
reboot
wait 2600
tap HOME
wait 300
expect tile 16
tap LFO
wait 150
tap HOME
wait 150
tap HOME
wait 150
tap HOME
wait 150
tap HOME
wait 300
expect page 7
press SEL
turn K1 1
release SEL
wait 300
expect tile 8
tap HOME
wait 300
expect pal_hi 14329
turn ALGO 2
wait 300
expect brk 2
expect pal_hi 64968
tap PLAY
wait 2000
expect ov_loud 0
tap PLAY
wait 300
expect th_bg 0
sysex F0 7D 43 48 06 F7
wait 300
expect n_user 0
expect pk_tag 0
expect mq_tag 0
S
} > "$O/s2.txt"
$H "$O/s2.txt" app/build/scenarios > "$O/run2.log" 2>&1 || { grep -E "FAIL|host:" "$O/run2.log"; exit 1; }
grep -q "EXPECTATIONS FAILED" "$O/run2.log" && { grep FAIL "$O/run2.log"; exit 1; }
blob "$O/gs2.syx" "$O/gs2.bin"
python3 - "$O/gs2.bin" <<'P'
import sys
b = open(sys.argv[1], "rb").read()
ss = b[1692:1692 + 80]   # GETSET past the scenes (1604) and the chain (88): break 1's and 2's LOOP settings
assert ss[0] == 16 and ss[1] == 8 and ss[2] == 3 and ss[3] == 256 - 6, f"break 1 as LOOP left it: {list(ss[:8])}"
assert int.from_bytes(ss[40 + 4:40 + 6], "little") == 15000, "break 2's tempo"
P
echo "pack_v2: GETSET read the scenes; a v2 pack's scenes (scene A $SC0 bars), a 1/2-bar window, -6 dB, +3, swing 62, kicks set by hand, a tempo over the slot's and an overlay alone in bank 2 read, the pack's colours and its overlay heard under its breaks, its fader, silent under a built-in one (amber again there); kept at power-on; HOME's LOOP lengthens the window (GETSET reads it back where the builder looks), SAVE keeps it through a reboot, SEL + a knob brings the pack's back; deleted: the factory's again"
