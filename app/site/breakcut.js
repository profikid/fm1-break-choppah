// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Laurens (profikid), CHOPPAH
//
// breakcut.js: a sample -> a CHOPPAH break, as the pack builder (breaks.html) makes it, no DOM: two bars cut from it
// (a loop file's, or a track's from its first bar), levelled as the built-in breaks (or peak), resampled to the rate
// the break fits at, sixteen slices on the hits (or the grid, or placed by hand), warped onto the grid if asked. The
// level, slice and warp functions are breaks.html's, copied unchanged (tests/breakcut.test.mjs holds them equal).
// The hub's library slicer uses it (PRD_PACKS §10.8).
import { resample as resampleLinear } from "./beats.js";

export const RATE = 22050, MAX_LEN = 106496, NSLICES = 16;

/* a sample's edit (what the library keeps for it): how its break is cut */
export const EDIT_DEFAULTS = { name: "", bpm: 120, offset: 0, start: 0, once: false, level: true, snap: true, warp: false, marks: null };

const clamp = (v, lo, hi) => v < lo ? lo : v > hi ? hi : v;

export function loudnessDb(x, rate) {             /* the RMS of 50 ms windows, the quiet ones left out */
  const w = Math.max(1, Math.floor(rate * 0.05)), win = [];
  for (let i = 0; i < Math.max(1, x.length - w + 1); i += w) {
    let e = 0;
    for (let k = i; k < i + w && k < x.length; k++) e += x[k] * x[k];
    win.push(e / w);
  }
  const mean = win.reduce((a, b) => a + b, 0) / win.length;
  let loud = win.filter((e) => e > mean * 0.01);
  if (!loud.length) loud = win;
  return 10 * Math.log10(Math.max(1e-12, loud.reduce((a, b) => a + b, 0) / loud.length));
}
export function limit(y, rate, ceiling) {         /* look-ahead (2 ms) limiter, 40 ms release: -> [samples, most gain off] */
  const n = y.length, la = Math.max(1, Math.floor(rate * 0.002));
  const req = new Float32Array(n), ahead = new Float32Array(n), held = new Float32Array(n), out = new Float32Array(n);
  for (let i = 0; i < n; i++) req[i] = y[i] ? Math.min(1, ceiling / Math.abs(y[i])) : 1;
  const q = new Int32Array(n);
  let qh = 0, qt = 0;
  for (let i = n - 1; i >= 0; i--) {
    while (qt > qh && req[q[qt - 1]] >= req[i]) qt--;
    q[qt++] = i;
    while (q[qh] > i + la - 1) qh++;
    ahead[i] = req[q[qh]];
  }
  const rel = 1 - Math.exp(-1 / (rate * 0.04));
  let env = 1;
  for (let i = 0; i < n; i++) { env = ahead[i] < env ? ahead[i] : env + (1 - env) * rel; held[i] = env; }
  let acc = la, most = 1;
  for (let i = 0; i < n; i++) {
    acc += held[i] - (i >= la ? held[i - la] : 1);
    const g = Math.min(acc / la, req[i]);
    if (g < most) most = g;
    out[i] = y[i] * g;
  }
  return [out, most];
}
export function level(x, rate, target = -16, ceiling = 0.98) {
  let g = Math.pow(10, (target - loudnessDb(x, rate)) / 20), out = x;
  for (let t = 0; t < 5; t++) {
    const y = new Float32Array(x.length);
    for (let i = 0; i < x.length; i++) y[i] = x[i] * g;
    out = limit(y, rate, ceiling)[0];
    const err = target - loudnessDb(out, rate);
    if (Math.abs(err) < 0.2) break;
    g *= Math.pow(10, err / 20);
  }
  return out;
}
export function peakNorm(x) {
  let p = 1e-9;
  for (const v of x) p = Math.max(p, Math.abs(v));
  return x.map((v) => v * 0.98 / p);
}
export function slicePoints(x, n, hits) {         /* sixteen, moved to the strongest onset within a third of a 16th (2 bars: 32) */
  const len = x.length;
  let pts = Array.from({ length: n + 1 }, (_, i) => Math.floor(len * i / n));
  if (!hits) return pts;
  const hop = 256, env = [];
  let prev = 0;
  for (let i = 0; i < len - hop; i += hop) {
    let e = 0;
    for (let k = i; k < i + hop; k++) e += x[k] * x[k];
    env.push(Math.max(0, e - prev));
    prev = e;
  }
  const reach = Math.floor(Math.floor(len / 32) / 3);
  for (let i = 1; i < n; i++) {
    const lo = Math.max(0, Math.floor((pts[i] - reach) / hop)), hi = Math.min(env.length - 1, Math.floor((pts[i] + reach) / hop));
    if (hi > lo) {
      let best = lo;
      for (let j = lo; j <= hi; j++) if (env[j] > env[best]) best = j;
      pts[i] = best * hop;
    }
  }
  const s = [...new Set(pts)].sort((a, b) => a - b);
  return s.length === n + 1 ? s : Array.from({ length: n + 1 }, (_, i) => Math.floor(len * i / n));
}

/* warp: each slice's start lands on its grid line, the sound between stretched or squeezed (WSOLA), pitch kept */
const HANN = new Float32Array(512).map((_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / 512));
export function wsola(x, outLen) {
  const N = 512, Hs = N >> 1, tol = 128;
  if (x.length < N + 2 * tol || outLen < N) {   /* too short for grains: read faster or slower (a tail: inaudible) */
    const y = new Float32Array(outLen), r = x.length / Math.max(1, outLen);
    for (let i = 0; i < outLen; i++) { const p = i * r, k = Math.floor(p), f = p - k; y[i] = (x[k] || 0) * (1 - f) + (x[k + 1] || 0) * f; }
    return y;
  }
  const y = new Float32Array(outLen + N), w = new Float32Array(outLen + N);
  const frames = Math.ceil((outLen - N) / Hs) + 1, Ha = (x.length - N) / Math.max(1, frames - 1);
  let prev = 0;
  for (let k = 0; k < frames; k++) {
    const o = k * Hs, nominal = Math.round(k * Ha);
    let best = clamp(nominal, 0, x.length - N);
    if (k) {
      const nat = prev + Hs;               /* where the last grain would go on: the best match to it, near nominal */
      let bc = -Infinity;
      for (let d = -tol; d <= tol; d += 2) {
        const p = nominal + d;
        if (p < 0 || p + N > x.length || nat + Hs > x.length) continue;
        let c = 0;
        for (let j = 0; j < Hs; j += 2) c += x[p + j] * x[nat + j];
        if (c > bc) { bc = c; best = p; }
      }
    }
    for (let j = 0; j < N; j++) { y[o + j] += x[best + j] * HANN[j]; w[o + j] += HANN[j]; }
    prev = best;
  }
  const out = new Float32Array(outLen);
  for (let i = 0; i < outLen; i++) out[i] = w[i] > 1e-3 ? y[i] / w[i] : y[i];
  return out;
}
export function warp(x, marks, rate) {
  const n = marks.length - 1, L = x.length, y = new Float32Array(L);
  const A = Math.round(rate * 0.008), F = Math.round(rate * 0.002);
  for (let i = 0; i < n; i++) {
    const g0 = Math.round(i * L / n), slot = Math.round((i + 1) * L / n) - g0;
    const seg = x.subarray(marks[i], marks[i + 1]), a = Math.min(A, seg.length, slot);
    if (seg.length === slot) { y.set(seg, g0); continue; }   /* (already on the grid: as it is) */
    y.set(seg.subarray(0, a), g0);
    y.set(wsola(seg.subarray(a), slot - a), g0 + a);
    for (let j = 0; j < F && j < slot; j++) y[g0 + slot - 1 - j] *= j / F;
  }
  return y;
}


/* a loop file's bars and tempo from its length (breaks.html's guessTempo): the bars that give the likeliest tempo */
export function guessTempo(dur) {
  let best = 2, err = 1e9;
  for (const b of [1, 2, 4, 8, 16]) { const t = 240 * b / dur, e = t < 80 ? 80 - t : t > 180 ? t - 180 : Math.abs(t - 130) / 100; if (e < err) { err = e; best = b; } }
  let bpm = 240 * best / dur;
  while (bpm < 95 && best * 2 <= 32) { best *= 2; bpm *= 2; }
  return { bars: best, bpm };
}
export const cleanName = (v) => (v || "MY BREAK").toUpperCase().replace(/[^A-Z0-9 ]/g, " ").slice(0, 11);
export const barsIn = (dur, e) => Math.max(1, Math.floor((dur - (e.offset || 0)) / (240 / e.bpm) + 0.02));   /* whole bars after the offset */
/* the rate a break of this tempo fits at (slow ones lower, to fit CH_PCM_MAX) */
export const breakRate = (bpm) => Math.min(RATE, Math.floor(MAX_LEN / (2 * 240 / bpm)));

/* the two bars, at the sample's rate: from offset + start bars; once: one bar, twice (a one-bar loop) */
export function cut(x, rate, e) {
  const barLen = rate * 240 / e.bpm, from = Math.round((e.offset || 0) * rate + (e.once ? 0 : e.start || 0) * barLen);
  let y;
  if (e.once) {
    const one = x.subarray(from, Math.min(x.length, from + Math.round(barLen)));
    y = new Float32Array(one.length * 2); y.set(one); y.set(one, one.length);
  } else y = x.slice(from, Math.min(x.length, from + Math.round(2 * barLen)));
  const want = Math.round(2 * barLen);
  if (y.length < want - 2) { const z = new Float32Array(want); z.set(y); y = z; }
  return y;
}

/* resampling: the browser's (as the builder), else linear */
async function resampleBest(x, from, to) {
  if (from === to) return x;
  if (typeof OfflineAudioContext !== "undefined") {
    const oc = new OfflineAudioContext(1, Math.max(1, Math.round(x.length * to / from)), to);
    const ab = oc.createBuffer(1, x.length, from); ab.copyToChannel(x, 0);
    const node = oc.createBufferSource(); node.buffer = ab; node.connect(oc.destination); node.start();
    return (await oc.startRendering()).getChannelData(0);
  }
  return resampleLinear(x, from, to);
}

/* the sample (mono x at rate) and its edit -> {name, bpm, rate, pcm, slices, auto}: the break as the FM-1 gets it
   (auto: the slices it would have without marks, for the slicer). marks: 17 sample positions in the break, or null */
export async function makeBreak(x, rate, edit, { resample = resampleBest } = {}) {
  const e = { ...EDIT_DEFAULTS, ...edit };
  let y = cut(x, rate, e);
  y = e.level ? level(y, rate) : peakNorm(y);
  const r = breakRate(e.bpm);
  const pcm = (await resample(y, rate, r)).slice(0, MAX_LEN);
  const auto = slicePoints(pcm, NSLICES, e.snap);
  const marks = Array.isArray(e.marks) && e.marks.length === NSLICES + 1 && e.marks[NSLICES] <= pcm.length ? e.marks : null;
  let slices = marks || auto, out = pcm;
  if (e.warp) { out = warp(pcm, slices, r); slices = slices.map((_, i) => Math.round(i * out.length / NSLICES)); }
  return { name: cleanName(e.name), bpm: e.bpm, rate: r, pcm: out, slices, auto, cutLen: pcm.length };
}

/* a first edit for a sample from what was heard (beats.js analyse, or a loop's length): loop files from their start,
   tracks from their first bar, the best loop (findLoops) chosen */
export function autoEdit(name, dur, heard = null) {
  if (!heard || dur <= 20) {
    const g = guessTempo(dur);
    return { ...EDIT_DEFAULTS, name: cleanName(name), bpm: +g.bpm.toFixed(2), offset: 0, start: 0, once: g.bars === 1 };
  }
  const barLen = 240 / heard.bpm, offset = ((heard.downbeat % barLen) + barLen) % barLen;
  const best = heard.loops && heard.loops[0];
  const start = best ? Math.max(0, Math.round((best.start - offset) / barLen)) : 0;
  return { ...EDIT_DEFAULTS, name: cleanName(name), bpm: +heard.bpm.toFixed(2), offset: +offset.toFixed(4), start, once: false };
}
