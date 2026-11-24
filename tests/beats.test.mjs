// node tests/beats.test.mjs: the pack builder's ears (app/site/beats.js) on the classic breaks in breaks/classics:
// each loop's tempo, a made-up track (a pad, then a break eight times over it) found and cut, the stems split.
import * as B from "../app/site/beats.js";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

function readWav(path) {                  /* mono Float32 from PCM 16/24 or float 32 */
  const b = readFileSync(path), dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let p = 12, fmt, data;
  while (p + 8 <= b.length) {
    const id = b.toString("ascii", p, p + 4), n = dv.getUint32(p + 4, true);
    if (id === "fmt ") fmt = { tag: dv.getUint16(p + 8, true), ch: dv.getUint16(p + 10, true), rate: dv.getUint32(p + 12, true), bits: dv.getUint16(p + 22, true) };
    if (id === "data") data = { at: p + 8, n };
    p += 8 + n + (n & 1);
  }
  const bps = fmt.bits / 8, frames = Math.floor(data.n / (bps * fmt.ch)), x = new Float32Array(frames);
  for (let i = 0; i < frames; i++) for (let c = 0; c < fmt.ch; c++) {
    const o = data.at + (i * fmt.ch + c) * bps;
    const v = fmt.tag === 3 ? dv.getFloat32(o, true) : bps === 2 ? dv.getInt16(o, true) / 32768 : bps === 3 ? ((dv.getUint8(o) | dv.getUint8(o + 1) << 8 | dv.getInt8(o + 2) << 16) / 8388608) : 0;
    x[i] += v / fmt.ch;
  }
  return { x, rate: fmt.rate };
}
const cat = readFileSync(new URL("../breaks/breaks.txt", import.meta.url), "utf8").split("\n")
  .filter((l) => /^[*-]\s+classics\//.test(l)).map((l) => { const f = l.trim().split(/\s+/); return { file: f[1], bpm: +f[2], of: +f[4] }; });

/* 1. each classic loop: the tempo (its length is whole bars) and the octave */
for (const c of cat) {
  const { x, rate } = readWav(new URL("../breaks/" + c.file, import.meta.url));
  const a = B.analyse(x, rate);
  const err = Math.abs(a.bpm - c.bpm) / c.bpm;
  assert.ok(err < 0.015, `${c.file}: ${a.bpm} BPM, the catalogue says ${c.bpm}`);
}
console.log(`beats: ${cat.length} classic loops, each tempo within 1.5 %`);

/* 2. a track: 3.37 s of pad, then FUNKY DRUMMER's 4 bars twice over the pad, then the pad alone */
{
  const { x: drum, rate } = readWav(new URL("../breaks/classics/james_brown_funky_drummer.wav", import.meta.url));
  const pad = (n, t0) => Float32Array.from({ length: n }, (_, i) => { const t = (i + t0) / rate; return 0.08 * (Math.sin(2 * Math.PI * 220 * t) + Math.sin(2 * Math.PI * 277.2 * t) + Math.sin(2 * Math.PI * 329.6 * t)); });
  const intro = Math.round(3.37 * rate), outro = Math.round(4 * rate), n = intro + 2 * drum.length + outro;
  const x = pad(n, 0);
  for (let k = 0; k < 2; k++) for (let i = 0; i < drum.length; i++) x[intro + k * drum.length + i] += drum[i];
  const t0 = Date.now();
  const a = B.analyse(x, rate);
  const want = 99.81, barLen = 240 / want;
  assert.ok(Math.abs(a.bpm - want) < 0.5, `track tempo ${a.bpm}, want ${want}`);
  const sinceIntro = a.bars.map((t) => ((t - 3.37) % barLen + barLen) % barLen).map((d) => Math.min(d, barLen - d));
  const onGrid = sinceIntro.filter((d) => d < 0.04).length / sinceIntro.length;
  assert.ok(onGrid > 0.6, `bar lines on the break's bar lines: ${(onGrid * 100).toFixed(0)} %`);
  const s = B.stems(x, rate);
  const loops = B.findLoops(x, rate, a, { drums: s.drums });
  const top = loops[0];
  assert.equal(top.bars, 2, "a 2-bar loop first");
  assert.ok(top.start > 3.2 && top.end < 3.37 + 8 * barLen + 0.1, `the loop inside the break: ${top.start.toFixed(2)}–${top.end.toFixed(2)} s`);
  const off = ((top.start - 3.37) % barLen + barLen) % barLen;
  assert.ok(Math.min(off, barLen - off) < 0.04, `cut on a bar line (${(Math.min(off, barLen - off) * 1000).toFixed(0)} ms off)`);
  /* 3. the stems: the pad alone goes to the rest, the break mostly to the drums */
  const e = (y, a0, a1) => { let s2 = 0; for (let i = Math.round(a0 * rate); i < Math.round(a1 * rate); i++) s2 += y[i] * y[i]; return s2; };
  const padRest = e(s.rest, 0.5, 3.0) / e(x, 0.5, 3.0);
  assert.ok(padRest > 0.9, `the pad in the rest: ${(padRest * 100).toFixed(0)} %`);
  const drumShare = e(s.drums, 3.5, 3.5 + 4 * barLen) / (e(s.drums, 3.5, 3.5 + 4 * barLen) + e(s.rest, 3.5, 3.5 + 4 * barLen));
  assert.ok(drumShare > 0.5, `the break in the drums: ${(drumShare * 100).toFixed(0)} %`);
  console.log(`beats: a ${(n / rate).toFixed(1)} s track: ${a.bpm} BPM, bars ${(onGrid * 100).toFixed(0)} % on the break's, top loop ${top.start.toFixed(2)}–${top.end.toFixed(2)} s, pad ${(padRest * 100).toFixed(0)} % to the rest, drums ${(drumShare * 100).toFixed(0)} % (${Date.now() - t0} ms)`);
}
/* 4. a seam: the loop's end eases into its own lead-in */
{
  const r = 22050, x = Float32Array.from({ length: r * 2 }, (_, i) => Math.sin(i / 7));
  const y = B.seamless(x, r, 0.5, 1.5);
  assert.equal(y.length, r);
  assert.ok(Math.abs(y[y.length - 1] - x[Math.round(0.5 * r) - 1]) < 0.05, "the end meets the start's lead-in");
}
console.log("beats: all good");
