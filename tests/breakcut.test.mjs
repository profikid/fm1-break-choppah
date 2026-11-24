// node tests/breakcut.test.mjs: breakcut.js (the hub's library slicer) makes a break as the pack builder does: its
// level, slice and warp functions give the same samples as breaks.html's own; the cut, the rate, the marks, warp
import * as C from "../app/site/breakcut.js";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

/* breaks.html's own functions, out of the page */
const page = readFileSync(new URL("../app/site/breaks.html", import.meta.url), "utf8");
const from = (a, b) => { const i = page.indexOf(a), j = page.indexOf(b, i); assert.ok(i > 0 && j > i, a); return page.slice(i, j); };
const src = "const clamp = (v, lo, hi) => v < lo ? lo : v > hi ? hi : v;\n" + from("function loudnessDb(x, rate)", "function adpcmEncode(x)")
  + from("const HANN = new Float32Array(512)", "\n/* ---------------------------------------------------------------- the slices by hand");
const P = new Function(src + "\nreturn { level, peakNorm, slicePoints, warp, wsola };")();

let seed = 3;
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296 - 0.5);
const hits = (n, rate, bpm) => {                 /* a drum-ish loop: a decaying hit on every 8th, noise and a tone */
  const x = new Float32Array(n), step = rate * 30 / bpm;
  for (let i = 0; i < n; i++) { const p = (i % step) / rate; x[i] = Math.exp(-p * 30) * (0.7 * rnd() + 0.3 * Math.sin(i / 7)) * (i % (4 * step) < step ? 1.4 : 0.8); }
  return x;
};
const same = (a, b, what) => { assert.equal(a.length, b.length, what); for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) assert.fail(`${what}: sample ${i} ${a[i]} != ${b[i]}`); };

const x = hits(88200, 44100, 120);
same(C.level(x, 44100), P.level(x, 44100), "level");
same(C.peakNorm(x), P.peakNorm(x), "peakNorm");
for (const snap of [true, false]) assert.deepEqual(C.slicePoints(x, 16, snap), P.slicePoints(x, 16, snap), "slicePoints");
const marks = C.slicePoints(x, 16, true);
same(C.warp(x, marks, 44100), P.warp(x, marks, 44100), "warp");

/* the cut: two bars from the offset and the start bar; a one-bar loop twice; short ones padded */
const rate = 44100, bpm = 120, bar = rate * 2;
const ramp = Float32Array.from({ length: bar * 8 }, (_, i) => i);
let y = C.cut(ramp, rate, { bpm, offset: 0.5, start: 2 });
assert.equal(y.length, 2 * bar); assert.equal(y[0], 0.5 * rate + 2 * bar);
y = C.cut(ramp, rate, { bpm, once: true, offset: 0 });
assert.equal(y.length, 2 * bar); assert.equal(y[bar], 0);
y = C.cut(ramp.subarray(0, bar * 1.5), rate, { bpm, start: 0 });
assert.equal(y.length, 2 * bar, "padded to two bars");
assert.equal(C.barsIn(16.1, { bpm: 120, offset: 0 }), 8);

/* the break: its rate, its length, sixteen slices; marks kept, warp on the grid */
const lin = { resample: async (s, a, b) => (a === b ? s : Float32Array.from({ length: Math.round(s.length * b / a) }, (_, i) => s[Math.min(s.length - 1, Math.round(i * a / b))])) };
const loop = hits(bar * 2, rate, bpm);
let b = await C.makeBreak(loop, rate, { name: "amen-ish!", bpm }, lin);
assert.equal(b.name, "AMEN ISH ");
assert.equal(b.rate, 22050); assert.equal(b.pcm.length, 88200); assert.equal(b.slices.length, 17); assert.equal(b.slices[16], 88200);
const slow = await C.makeBreak(hits(Math.round(rate * 240 * 2 / 70), rate, 70), rate, { bpm: 70 }, lin);
assert.ok(slow.rate < 22050 && slow.pcm.length <= C.MAX_LEN, "a slow break at a lower rate, under CH_PCM_MAX");
const hand = b.auto.slice(); hand[5] += 300;
b = await C.makeBreak(loop, rate, { bpm, marks: hand }, lin);
assert.deepEqual(b.slices, hand, "the slices placed by hand");
b = await C.makeBreak(loop, rate, { bpm, marks: hand, warp: true }, lin);
assert.deepEqual(b.slices, Array.from({ length: 17 }, (_, i) => Math.round(i * b.pcm.length / 16)), "warped: on the grid");
b = await C.makeBreak(loop, rate, { bpm, marks: [1, 2, 3] }, lin);
assert.deepEqual(b.slices, b.auto, "marks that don't fit: the auto slices");

/* a first edit: a loop file from its length, a track from its first bar and best loop */
let e = C.autoEdit("think break.wav", 4.2);
assert.equal(e.name, "THINK BREAK"); assert.ok(Math.abs(e.bpm - 114.29) < 0.01); assert.equal(e.once, false);
e = C.autoEdit("track", 150, { bpm: 100, downbeat: 3.1, loops: [{ start: 3.1 + 2.4 * 7, bars: 2 }] });
assert.equal(e.bpm, 100); assert.ok(Math.abs(e.offset - 0.7) < 1e-6); assert.equal(e.start, 8);

console.log("breakcut: the builder's level, slices and warp, sample for sample; the cut, the rate, marks, warp, the first edit");
