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
/* a split for the tests: the harmonic / percussive measure stands in for Demucs (the hub's GPU), offline */
const B = await import("../app/site/beats.js");
const hp = async (v, r) => B.stems(v, r);
const lin = { split: hp, resample: async (s, a, b) => (a === b ? s : Float32Array.from({ length: Math.round(s.length * b / a) }, (_, i) => s[Math.min(s.length - 1, Math.round(i * a / b))])) };
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

/* the rate: slow breaks at the rate that fits CH_PCM_MAX, and the bank takes it (the FM-1 plays a slot at any rate) */
const { buildBank, readBank } = await import("../app/site/chpk.js");
const r99 = C.breakRate(99.22);
assert.ok(r99 > 22000 && r99 < 22050 && Math.round(2 * 240 / 99.22 * r99) <= C.MAX_LEN, "99 BPM: just under 22.05 kHz");
const slow99 = await C.makeBreak(hits(Math.round(rate * 240 * 2 / 99.22), rate, 99.22), rate, { bpm: 99.22 }, lin);
assert.equal(readBank(buildBank([slow99])).slots[0].rate, r99, "a 99 BPM break's bank reads back");

/* the drums alone (the stems): the same length and slices; a steady tone under the hits mostly gone */
const tone = Float32Array.from(loop, (v, i) => v * 0.5 + 0.5 * Math.sin(2 * Math.PI * 220 * i / rate));
const both = await C.makeBreak(tone, rate, { bpm, level: false }, lin), drums = await C.makeBreak(tone, rate, { bpm, level: false, drums: true }, lin);
assert.equal(drums.pcm.length, both.pcm.length); assert.equal(drums.rate, both.rate); assert.equal(drums.slices.length, 17);
const at220 = (y, r) => { let re = 0, im = 0; for (let i = 0; i < y.length; i++) { re += y[i] * Math.cos(2 * Math.PI * 220 * i / r); im += y[i] * Math.sin(2 * Math.PI * 220 * i / r); } return Math.hypot(re, im) / y.length; };
const rms = (y) => Math.sqrt(y.reduce((a, v) => a + v * v, 0) / y.length);
assert.ok(at220(drums.pcm, drums.rate) / rms(drums.pcm) < 0.5 * at220(both.pcm, both.rate) / rms(both.pcm), "the tone is mostly out of the drums");
const once = await C.makeBreak(tone, rate, { bpm, once: true, drums: true }, lin);
assert.equal(once.pcm.length, both.pcm.length, "one bar twice, from the drums");

/* the overlay: bars on the grid, the rest or the mix, the best rate that fits, the reasons it can't */
const track = Float32Array.from({ length: rate * 20 }, (_, i) => 0.3 * Math.sin(2 * Math.PI * 110 * i / rate) + (i % (rate / 2) < 200 ? 0.8 * rnd() : 0));
let ov = await C.makeOverlay(track, rate, { bpm: 120, offset: 0.25, start: 1, bars: 4, take: "mix" }, { resample: lin.resample, split: hp });
assert.equal(ov.rate, 22050); assert.equal(ov.len, 4 * 2 * 22050); assert.equal(ov.pcm.length, ov.len); assert.equal(ov.bars, 4); assert.equal(ov.bpm, 120);
ov = await C.makeOverlay(track, rate, { bpm: 120, start: 0, bars: 4 }, { resample: lin.resample, split: hp, fits: (len, r) => r < 22050 });
assert.equal(ov.rate, 16000, "the first rate that fits"); assert.equal(ov.len, 4 * 2 * 16000);
await assert.rejects(C.makeOverlay(track, rate, { bpm: 120, start: 7, bars: 4 }, lin), /ends before bar 12/);
await assert.rejects(C.makeOverlay(track, rate, { bpm: 120, bars: 4 }, { resample: lin.resample, split: hp, fits: () => false }), /no room/);
await assert.rejects(C.makeOverlay(track, rate, { bpm: 120, bars: 33 }, lin), /1 to 32/);

/* a first edit: a loop file from its length, a track from its first bar and best loop */
/* another split (the hub's Demucs on a GPU): makeBreak and makeOverlay take it, the drums or the rest from it */
const calls = [];
const silent = async (v, r) => { calls.push([v.length, r]); return { drums: new Float32Array(v.length).fill(0.25), rest: new Float32Array(v.length).fill(-0.125) }; };
const own = await C.makeBreak(tone, rate, { bpm, level: false, drums: true }, { ...lin, split: silent });
assert.equal(calls.length, 1); assert.equal(calls[0][1], own.rate, "split at the break's rate");
assert.ok(own.pcm.every((v) => Math.abs(v - own.pcm[0]) < 1e-6), "the drums are the split's");
const ovs = await C.makeOverlay(tone, rate, { bpm, bars: 1, take: "rest" }, { ...lin, split: silent });
assert.equal(calls.length, 2); assert.equal(calls[1][1], 22050, "the overlay split at 22.05 kHz");
await C.makeOverlay(tone, rate, { bpm, bars: 1, take: "mix" }, { ...lin, split: silent });
assert.equal(calls.length, 2, "the mix: no split");
assert.ok(ovs.pcm.length > 0);

/* only Demucs: with no hub to ask (here, node), the drums aren't made from anything else: it says why */
await assert.rejects(C.makeBreak(tone, rate, { bpm, drums: true }, { resample: lin.resample }), (e) => e instanceof B.DestemError && /choppah\.app/.test(e.message));
await assert.rejects(C.makeOverlay(tone, rate, { bpm, bars: 1, take: "rest" }, { resample: lin.resample }), B.DestemError);
assert.ok((await C.makeOverlay(tone, rate, { bpm, bars: 1, take: "mix" }, { resample: lin.resample })).pcm.length, "the mix needs no split");
/* splitServer: a WAV up (x-hub, the session's cookie), the drums back, the rest the mix less them; 401 says sign in */
{ const seen = [];
  const fake = (status) => async (url, init) => { seen.push({ url, h: init.headers, cred: init.credentials });
    if (status !== 200) return new Response("{}", { status });
    const w = B.monoWav(init.body); return new Response(B.floatWav(w.x.map((v) => v * 0.5), w.rate)); };
  const x = Float32Array.from({ length: 4410 }, (_, i) => Math.sin(i / 7));
  const st = await B.splitServer({ fetchFn: fake(200) })(x, 22050);
  assert.equal(seen[0].url, "/api/destem"); assert.equal(seen[0].h["x-hub"], "1"); assert.equal(seen[0].cred, "same-origin");
  assert.ok(Math.abs(st.drums[100] - x[100] / 2) < 1e-6 && Math.abs(st.rest[100] - x[100] / 2) < 1e-6);
  const p16 = await B.splitServer({ fetchFn: fake(200), pcm16: true })(x, 22050);
  assert.ok(Math.abs(p16.drums[100] - x[100] / 2) < 1e-3, "16-bit up");
  const sp = B.splitServer({ fetchFn: fake(200) }), n0 = seen.length;
  const a1 = await sp(x, 22050), a2 = await sp(x, 22050);
  assert.equal(seen.length, n0 + 1, "the same audio again: kept, not asked twice");
  a1.drums[0] = 9; assert.notEqual(a2.drums[0], 9, "each answer its own copy");
  await sp(x.map((v) => v * 0.9), 22050); assert.equal(seen.length, n0 + 2, "other audio: asked");
  await assert.rejects(B.splitServer({ fetchFn: fake(401) })(x, 22050), /sign in/);
  const f403 = async () => new Response(JSON.stringify({ error: "sign in first" }), { status: 403 });
  await assert.rejects(B.splitServer({ fetchFn: f403 })(x, 22050), /choppah\.app's GPU: sign in/);
  await assert.rejects(B.splitServer({ fetchFn: fake(404) })(x, 22050), /choppah\.app/); }

let e = C.autoEdit("think break.wav", 4.2);
assert.equal(e.name, "THINK BREAK"); assert.ok(Math.abs(e.bpm - 114.29) < 0.01); assert.equal(e.once, false);
e = C.autoEdit("track", 150, { bpm: 100, downbeat: 3.1, loops: [{ start: 3.1 + 2.4 * 7, bars: 2 }] });
assert.equal(e.bpm, 100); assert.ok(Math.abs(e.offset - 0.7) < 1e-6); assert.equal(e.start, 8);

console.log("breakcut: the builder's level, slices and warp, sample for sample; the cut, the rate (slow ones read back), marks, warp, the drums alone, the overlay, a split of its own, only Demucs (splitServer), the first edit");
