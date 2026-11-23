#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The break manager page (app/site/breaks.html) makes a break exactly as tools/prep_breaks.py does: its own code run
# in node on a real break against the Python, step by step. The bank image must be the same byte for byte for the
# same samples (a bank of one, and a pack's bank of three), the "AT" nudge as tools/cu_frames.py's; the level within
# float rounding. Needs node.
#   tests/page_parity.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/parity
mkdir -p "$O"
python3 - "$O" <<'P'
import argparse, json, sys
sys.path.insert(0, "tools")
import prep_breaks as pb
o = sys.argv[1]
x, rate = pb.read_wav("breaks/classics/rotary_connection_life_could.wav")
bpm = 99.81
x = x[:int(round(rate * 240.0 / bpm * 2))]
lv, _, _ = pb.level(x, rate, -16.0)
args = argparse.Namespace(bars=2, slices=16, break_slices=16, rate=22050, transients=True, loudness=-16.0, name_prefix="")
s = pb.prepare(f"breaks/classics/rotary_connection_life_could.wav:{bpm}:2:LIFE COULD", args)
img = pb.build_bank([s])
import cu_frames
three = [pb.prepare(f"breaks/classics/{w}:{b}:2:{n}", args) for w, b, n in
         [("lyn_collins_think_about_it.wav", 113.78, "P THINK"), ("the_winstons_amen_brother.wav", 141.82, "P AMEN"),
          ("jimmy_mcgriff_the_worm.wav", 109.79, "P WORM")]]
multi = pb.build_bank(three)
json.dump({"x": x[::1], "rate": rate, "level": lv, "pcm": s["pcm"], "slices": s["slices"], "bpm": bpm,
           "hex": img.hex(), "multi": [{"name": t["name"], "bpm": t["bpm"], "rate": t["rate"], "pcm": t["pcm"],
                                        "slices": t["slices"]} for t in three],
           "mhex": multi.hex(), "athex": cu_frames.no_at(multi).hex()}, open(f"{o}/py.json", "w"))
P
node - "$O" <<'N'
const fs = require("fs"), o = process.argv[2];
const page = fs.readFileSync("app/site/breaks.html", "utf8");
const code = page.split("the break, as tools/prep_breaks.py makes it */")[1].split("/* ----------------------------------------------------------------")[0];
const api = new Function(code + "; return {level, slicePoints, buildBank, readBank, adpcmDecode, slotsOf, bankSize, noAt};")();
const py = JSON.parse(fs.readFileSync(o + "/py.json"));
let worst = 0;
const lv = api.level(Float32Array.from(py.x), py.rate);
for (let i = 0; i < lv.length; i++) worst = Math.max(worst, Math.abs(lv[i] - py.level[i]));
if (worst > 2e-4) throw new Error("level differs by " + worst);
const pcm = Float32Array.from(py.pcm);
const sl = api.slicePoints(pcm, 16, true);
if (JSON.stringify(sl) !== JSON.stringify(py.slices)) throw new Error("slices differ:\n" + sl + "\n" + py.slices);
const img = api.buildBank({ name: "LIFE COULD", bpm: py.bpm, rate: 22050, pcm, slices: sl });
const hex = Buffer.from(img).toString("hex");
if (hex !== py.hex) {
  let i = 0; while (hex[i] === py.hex[i]) i++;
  throw new Error(`bank differs at byte ${i >> 1} of ${py.hex.length >> 1}`);
}
const fn = (name) => page.match(new RegExp("function " + name + "\\([^]*?\\n}\\n"))[0];
const { pack7, unpack7 } = new Function(fn("pack7") + fn("unpack7") + "return {pack7, unpack7};")();
const { execSync } = require("child_process");
const probe = Uint8Array.from({ length: 516 }, (_, i) => (i * 37 + 200) & 255);
const pyPacked = execSync(`python3 -c "import sys; sys.path.insert(0,'tools'); import cu_frames as c; print(c.pack7(bytes((i*37+200)&255 for i in range(516))).hex())"`).toString().trim();
if (Buffer.from(pack7(probe)).toString("hex") !== pyPacked) throw new Error("pack7 differs from cu_frames.py's");
if (Buffer.compare(Buffer.from(unpack7(Uint8Array.from(pack7(probe)))), Buffer.from(probe))) throw new Error("unpack7(pack7) is not the same");
const back = api.readBank(img);
if (!back || back.name !== "LIFE COULD" || back.slices.length !== 17) throw new Error("readBank");
const three = py.multi.map((t) => ({ ...t, pcm: Float32Array.from(t.pcm) }));
const multi = api.buildBank(three), mhex = Buffer.from(multi).toString("hex");
if (mhex !== py.mhex) { let i = 0; while (mhex[i] === py.mhex[i]) i++; throw new Error(`the bank of three differs at byte ${i >> 1}`); }
if (api.bankSize(three.map((t) => t.pcm.length)) !== multi.length) throw new Error("bankSize");
const slots = api.slotsOf(multi);
if (!slots || slots.map((b) => b.name).join() !== "P THINK,P AMEN,P WORM") throw new Error("slotsOf");
if (Buffer.compare(Buffer.from(api.buildBank(slots)), Buffer.from(multi))) throw new Error("the bank built again from its slots' ADPCM differs");
let at = Uint8Array.from(multi);
for (let q = 0xF06; q + 1 < at.length; q += 4096 * 5) { at[q] = 0x41; at[q + 1] = 0x54; }
const pyAt = execSync(`python3 -c "import sys; sys.path.insert(0,'tools'); import cu_frames as c; sys.stdout.write(c.no_at(bytes.fromhex(sys.stdin.read())).hex())"`, { input: Buffer.from(at).toString("hex"), maxBuffer: 1 << 24 }).toString();
if (Buffer.from(api.noAt(at)).toString("hex") !== pyAt || !api.slotsOf(at)) throw new Error("noAt differs from cu_frames.no_at");
console.log(`page_parity: level within ${worst.toExponential(1)}, slices the same, the bank the same (${img.length} B), read back; a bank of three the same (${multi.length} B), its size, its slots, "AT" nudged as cu_frames.py; the frames packed as cu_frames.py`);
N
