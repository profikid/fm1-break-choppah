/* SPDX-License-Identifier: MPL-2.0 */
/* The pack builder's ears (docs/PRD_PACKS.md PB-002..006, PB-010): from any track to breaks and an overlay.
 * No DOM, no libraries: runs in the page, in a Web Worker (beats_worker.js) and in plain `node` (tests/beats.test.mjs).
 *
 *   stems(x, rate)            harmonic / percussive separation (median filtering of the spectrogram, Fitzgerald 2010):
 *                             {drums, rest}, the drums for the breaks, the rest for the overlay
 *   analyse(x, rate)          tempo (BPM to 0.01), the beat grid, the downbeats (bar 1 = a kick, 2 and 4 the snare)
 *   findLoops(x, rate, a, n)  the best 1-, 2- and 4-bar windows to cut: drum energy, how well a bar repeats, little else
 *
 * Every function takes mono Float32 samples at any rate; work happens at ANALYSIS_RATE. */

export const ANALYSIS_RATE = 11025;
const HOP = 128;                          /* 11.6 ms at 11025 */
const FRAME = 1024;

/* ---------------------------------------------------------------- small DSP */
export function resample(x, from, to) {   /* linear, with a box pre-filter when going down: good enough to listen for beats */
  if (from === to) return x;
  const r = from / to, n = Math.floor(x.length / r), y = new Float32Array(n), w = Math.max(1, Math.floor(r));
  for (let i = 0; i < n; i++) {
    const p = i * r, j = Math.floor(p);
    let s = 0;
    for (let k = 0; k < w; k++) s += x[Math.min(x.length - 1, j + k)];
    y[i] = s / w;
  }
  return y;
}
const hann = (n) => Float32Array.from({ length: n }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / n));

/* in-place radix-2 FFT on re[], im[] (length a power of two) */
export function fft(re, im, inverse = false) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (inverse ? 2 : -2) * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2, tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
  if (inverse) for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

/* ---------------------------------------------------------------- onsets */
/* -> {all, low, mid, high}: onset strength per hop (spectral flux of log magnitude), whole and in three bands
 *    (low < 150 Hz: kicks; 150 Hz-2 kHz: snare bodies; > 2 kHz: snares' crack, hats) */
export function onsets(x, rate) {
  const y = resample(x, rate, ANALYSIS_RATE), win = hann(FRAME), nb = FRAME / 2;
  const nf = Math.max(0, Math.floor((y.length - FRAME) / HOP) + 1);
  const bandOf = (k) => { const f = k * ANALYSIS_RATE / FRAME; return f < 150 ? 0 : f < 2000 ? 1 : 2; };
  const band = Uint8Array.from({ length: nb }, (_, k) => bandOf(k));
  const all = new Float32Array(nf), low = new Float32Array(nf), mid = new Float32Array(nf), high = new Float32Array(nf);
  let prev = new Float32Array(nb), cur = new Float32Array(nb);
  const re = new Float32Array(FRAME), im = new Float32Array(FRAME);
  for (let f = 0; f < nf; f++) {
    for (let i = 0; i < FRAME; i++) { re[i] = y[f * HOP + i] * win[i]; im[i] = 0; }
    fft(re, im);
    let a = 0, l = 0, m = 0, h = 0;
    for (let k = 1; k < nb; k++) {
      cur[k] = Math.log1p(100 * Math.hypot(re[k], im[k]));
      const d = cur[k] - prev[k];
      if (d > 0) { a += d; if (band[k] === 0) l += d; else if (band[k] === 1) m += d; else h += d; }
    }
    all[f] = a; low[f] = l; mid[f] = m; high[f] = h;
    [prev, cur] = [cur, prev];
  }
  for (const e of [all, low, mid, high]) {              /* minus a centred moving mean (half a beat at 120), floored at 0 */
    const w = 11, c = Float32Array.from(e);
    for (let i = 0; i < e.length; i++) {
      let s = 0, n = 0;
      for (let j = Math.max(0, i - w); j <= Math.min(e.length - 1, i + w); j++) { s += c[j]; n++; }
      e[i] = Math.max(0, c[i] - s / n);
    }
  }
  /* each value's time: the hop where the new frame's attack sits, the frame's centre (FRAME / 2 after its start) */
  return { all, low, mid, high, hop: HOP / ANALYSIS_RATE, lead: FRAME / 2 / ANALYSIS_RATE };
}

/* the onset strength at a time (seconds), linearly between hops */
let LEAD = FRAME / 2 / ANALYSIS_RATE;
const at = (env, t, hop) => { const p = (t - LEAD) / hop, i = Math.floor(p); return i < 0 || i + 1 >= env.length ? 0 : env[i] + (env[i + 1] - env[i]) * (p - i); };

/* how well a beat period fits: the onsets on the best phase of the grid (a comb), per beat */
function combScore(env, hop, period, steps = 48) {
  const beats = Math.floor(env.length * hop / period);
  if (beats < 4) return { score: 0, phase: 0 };
  let best = -1, phase = 0;
  for (let s = 0; s < steps; s++) {
    const ph = period * s / steps;
    let sum = 0;
    for (let b = 0; b < beats; b++) sum += at(env, ph + b * period, hop) + 0.5 * at(env, ph + (b + 0.5) * period, hop);
    if (sum > best) { best = sum; phase = ph; }
  }
  return { score: best / beats, phase };
}

/* ---------------------------------------------------------------- tempo, beats, bars */
/* -> {bpm, beat: seconds of the first beat, downbeat: seconds of the first bar's 1, beats: [s], bars: [s], confidence}
 *    known: {bars} when the file is a loop of that many bars (its length then sets the tempo exactly) */
export function analyse(x, rate, { min = 80, max = 180, known } = {}) {
  const o = onsets(x, rate), env = o.all, hop = o.hop, dur = x.length / rate;
  /* 1. candidates from the autocorrelation of the onsets, 60..240 BPM */
  const lagMin = Math.floor(60 / 240 / hop), lagMax = Math.ceil(60 / 60 / hop);
  const ac = new Float32Array(lagMax + 1);
  for (let L = lagMin; L <= lagMax; L++) { let s = 0; for (let i = 0; i + L < env.length; i++) s += env[i] * env[i + L]; ac[L] = s / (env.length - L); }
  const peaks = [];
  for (let L = lagMin + 1; L < lagMax; L++) if (ac[L] > ac[L - 1] && ac[L] >= ac[L + 1]) peaks.push({ bpm: 60 / (L * hop), v: ac[L] });
  peaks.sort((a, b) => b.v - a.v);
  /* 2. each candidate and its octaves folded into [min, max], refined by the comb to 0.01 BPM */
  const tried = new Map();
  const consider = (bpm) => {
    while (bpm < min) bpm *= 2;
    while (bpm > max) bpm /= 2;
    if (bpm < min) return;
    const key = Math.round(bpm * 2);
    if (tried.has(key)) return;
    let b = bpm, best = combScore(env, hop, 60 / b);
    for (const step of [0.5, 0.1, 0.02, 0.005]) {     /* hill-climb the tempo */
      for (let moved = true, n = 0; moved && n < 40; n++) {
        moved = false;
        for (const d of [-step, step]) { const c = combScore(env, hop, 60 / (b + d)); if (c.score > best.score) { best = c; b += d; moved = true; } }
      }
    }
    tried.set(key, { bpm: b, ...best });
  };
  for (const p of peaks.slice(0, 6)) consider(p.bpm);
  if (!tried.size) consider(120);
  /* a mild preference for the tempos breaks have (a 3:4 or 2:3 mistake is the usual one) */
  const prior = (bpm) => Math.exp(-0.5 * (Math.log2(bpm / 122) / 0.75) ** 2);
  const cands = [...tried.values()].map((c) => ({ ...c, w: c.score * prior(c.bpm) })).sort((a, b) => b.w - a.w);
  let pick = cands[0];
  /* a loop file: its length is a whole number of bars (1, 2, 4, 8, 16), so the tempo is exact; the onsets choose */
  if (known && known.bars) pick = { ...pick, bpm: 240 * known.bars / dur, ...combScore(env, hop, 60 * dur / (240 * known.bars)) };
  else if (dur < 24) {
    let best = null;
    for (const bars of [1, 2, 4, 8, 16]) {
      const exact = 240 * bars / dur;
      if (exact < min || exact > max) continue;
      const c = combScore(env, hop, 60 / exact), w = c.score * prior(exact);
      if (!best || w > best.w) best = { bpm: exact, ...c, w, bars };
    }
    if (best) pick = best;
  }
  const period = 60 / pick.bpm;
  /* 3. the beat grid: the comb's phase, each beat nudged to its onset (±6 %) for a drummer who drifts */
  const beats = [];
  let t0 = pick.phase;
  while (t0 - period > -0.06) t0 -= period;
  for (let t = t0; t < dur - 0.01; t += period) beats.push(Math.max(0, t));
  const loopFile = (known && known.bars) || (dur < 24 && pick.bars);
  /* 4. the downbeat: which of the four beats is 1 (kicks on 1 and 3, snares on 2 and 4) */
  let bestShift = 0, bestBar = -Infinity;
  for (let s = 0; s < 4; s++) {
    let v = 0;
    for (let i = s; i + 3 < beats.length; i += 4) {
      const L = (k) => at(o.low, beats[i + k], hop), S = (k) => at(o.mid, beats[i + k], hop) + at(o.high, beats[i + k], hop);
      v += 1.0 * (L(0) - L(1)) + 0.6 * (L(2) - L(3)) + (S(1) + S(3)) - (S(0) + S(2)) * 0.5;
    }
    if (v > bestBar) { bestBar = v; bestShift = s; }
  }
  if (loopFile && beats[0] < 0.06) bestShift = 0;          /* a loop starts on its 1 */
  const bars = [];
  for (let i = bestShift; i < beats.length; i += 4) bars.push(beats[i]);
  const second = cands[1] ? cands[1].score : 0;
  return { bpm: Math.round(pick.bpm * 100) / 100, beat: beats[0] ?? 0, downbeat: bars[0] ?? 0, beats, bars,
    confidence: Math.max(0, Math.min(1, (pick.score - second) / (pick.score || 1) + 0.25)), onsets: o };
}

/* ---------------------------------------------------------------- loops */
/* -> up to n windows [{start, end (seconds), bars, score, drums, repeat}], best first, not overlapping.
 *    a: analyse()'s result; drums: the drum stem (optional: then the mix is judged by its onsets alone) */
export function findLoops(x, rate, a, { n = 8, lengths = [2, 1, 4], drums = null } = {}) {
  const o = a.onsets, hop = o.hop, barLen = 240 / a.bpm, out = [];
  const energy = (sig, s0, s1) => { let e = 0; const i0 = Math.floor(s0 * rate), i1 = Math.min(sig.length, Math.floor(s1 * rate)); for (let i = i0; i < i1; i += 4) e += sig[i] * sig[i]; return e / Math.max(1, (i1 - i0) / 4); };
  const pattern = (t0) => { const v = new Float32Array(32); for (let k = 0; k < 32; k++) v[k] = at(o.all, t0 + k * barLen / 16, hop) + at(o.low, t0 + k * barLen / 16, hop); return v; };
  const cos = (p, q) => { let a2 = 0, b2 = 0, ab = 0; for (let i = 0; i < p.length; i++) { ab += p[i] * q[i]; a2 += p[i] * p[i]; b2 += q[i] * q[i]; } return a2 && b2 ? ab / Math.sqrt(a2 * b2) : 0; };
  let maxOn = 1e-9;
  for (const v of o.all) maxOn = Math.max(maxOn, v);
  for (const bars of lengths) {
    for (let i = 0; i < a.bars.length; i++) {
      const s = a.bars[i], e = s + bars * barLen;
      if (e > x.length / rate) break;
      let density = 0;
      for (let k = 0; k < bars * 16; k++) density += Math.min(1, at(o.all, s + k * barLen / 16, hop) / maxOn);
      density /= bars * 16;
      const nextBar = a.bars[i + bars] !== undefined ? cos(pattern(s), pattern(a.bars[i + bars])) : 0.5;   /* does it loop? */
      const drumShare = drums ? Math.min(1, energy(drums, s, e) / (energy(x, s, e) + 1e-12)) : 0.6;     /* mostly drums? */
      const score = density * 0.45 + nextBar * 0.3 + drumShare * 0.25 - (bars === 2 ? 0 : 0.03);
      out.push({ start: s, end: e, bars, score, density, repeat: nextBar, drums: drumShare });
    }
  }
  out.sort((p, q) => q.score - p.score);
  const keep = [];
  for (const w of out) {
    if (keep.length >= n) break;
    if (keep.some((k) => w.start < k.end - 0.05 && k.start < w.end - 0.05)) continue;
    keep.push(w);
  }
  return keep;
}

/* ---------------------------------------------------------------- stems */
/* -> {drums, rest} at rate, the same length as x: soft masks from median-filtered magnitudes (percussive = vertical,
 *    harmonic = horizontal), done in overlapping chunks so a whole track fits in memory. onProgress(0..1) */
export function stems(x, rate, { frame = 2048, hop = 512, kernel = 17, power = 2, onProgress } = {}) {
  const win = hann(frame), nb = frame / 2 + 1, half = kernel >> 1;
  const nf = Math.max(1, Math.ceil((x.length + frame) / hop));
  const drums = new Float32Array(x.length), rest = new Float32Array(x.length), norm = new Float32Array(x.length);
  const CH = 256;                                          /* frames per chunk, plus `half` of context each side */
  const re = new Float32Array(frame), im = new Float32Array(frame);
  const med = (arr, n) => { const a = arr.slice(0, n).sort(); return a[n >> 1]; };
  const tmp = new Float32Array(kernel);
  for (let c0 = 0; c0 < nf; c0 += CH) {
    const f0 = Math.max(0, c0 - half), f1 = Math.min(nf, c0 + CH + half), F = f1 - f0;
    const R = new Float32Array(F * nb), I = new Float32Array(F * nb), M = new Float32Array(F * nb);
    for (let f = 0; f < F; f++) {
      const base = (f0 + f) * hop - frame;
      for (let i = 0; i < frame; i++) { const j = base + i; re[i] = j >= 0 && j < x.length ? x[j] * win[i] : 0; im[i] = 0; }
      fft(re, im);
      for (let k = 0; k < nb; k++) { R[f * nb + k] = re[k]; I[f * nb + k] = im[k]; M[f * nb + k] = Math.hypot(re[k], im[k]); }
    }
    for (let f = c0 - f0; f < Math.min(F, c0 - f0 + CH); f++) {
      for (let k = 0; k < nb; k++) {
        let n = 0;                                         /* harmonic: across time */
        for (let d = -half; d <= half; d++) { const g = f + d; if (g >= 0 && g < F) tmp[n++] = M[g * nb + k]; }
        const H = med(tmp, n);
        n = 0;                                             /* percussive: across frequency */
        for (let d = -half; d <= half; d++) { const q = k + d; if (q >= 0 && q < nb) tmp[n++] = M[f * nb + q]; }
        const P = med(tmp, n);
        const hp = H ** power, pp = P ** power, mask = pp / (hp + pp + 1e-12);
        re[k] = R[f * nb + k] * mask; im[k] = I[f * nb + k] * mask;
        if (k > 0 && k < nb - 1) { re[frame - k] = re[k]; im[frame - k] = -im[k]; }
      }
      fft(re, im, true);
      const base = (f0 + f) * hop - frame;
      for (let i = 0; i < frame; i++) {
        const j = base + i;
        if (j < 0 || j >= x.length) continue;
        drums[j] += re[i] * win[i]; norm[j] += win[i] * win[i];
      }
    }
    if (onProgress) onProgress(Math.min(1, (c0 + CH) / nf));
  }
  for (let j = 0; j < x.length; j++) { drums[j] = norm[j] > 1e-6 ? drums[j] / norm[j] : 0; rest[j] = x[j] - drums[j]; }
  return { drums, rest };
}

/* a loop's seam made smooth: the last `ms` crossfaded into the start's lead-in (the loop's own audio before it) */
export function seamless(x, rate, start, end, ms = 12) {
  const s = Math.round(start * rate), e = Math.round(end * rate), n = Math.max(1, Math.round(ms * rate / 1000));
  const y = x.slice(s, e);
  for (let i = 0; i < n && s - n + i >= 0; i++) {
    const g = i / n, k = y.length - n + i;
    if (k >= 0) y[k] = y[k] * (1 - g) + x[s - n + i] * g;
  }
  return y;
}
