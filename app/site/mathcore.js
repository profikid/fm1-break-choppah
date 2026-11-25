// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Laurens (profikid), CHOPPAH
//
// mathcore.js: CHOPPAH's MATHCORE as the FM-1 has it, no DOM (docs/PRD_MATHCORE.md; PRD_PACKS §10.9): the 16 scenes
// (four lanes each) as OBJ_MC and a pack's SCNE hold them, the chain and the black keys as OBJ_MQ and a pack's CHAN
// hold them, the factory scenes (app.c MC_PRESET, mc_scene_defaults), the Slice Map heard (brk.c mc_auto_roles) and
// the lanes played (brk.c mc_tick: the same hash, the same hits, slices, ratchets and variations), so a browser can
// edit them and hear them. tests/mathcore.test.mjs holds the presets to app.c's and the factory scenes to the host
// simulator's, byte for byte.

export const LANES = 4, EVENTS = 32, SCENES = 16, TICKS = 12, STEPS_PER_BAR = 16, QUEUE = 64, KEYS = 11, NOKEY = 0xFF;
export const MCS_MAGIC = 0x3553434D;            /* "MCS5" */
export const SCNE_BYTES = 4 + SCENES * 100, CHAN_BYTES = 80, LANE_BYTES = 24;
export const ROLES = ["KICK", "SNARE", "HAT", "PERC"], ROLE_LETTER = "KSHP";
export const RATES = ["1/32", "1/16", "1/8", "1/4", "1/32T", "1/16T", "1/8T"], RATE_TICKS = [6, 12, 24, 48, 4, 8, 16];
export const MODES = ["FIXED", "ROUND", "RANDOM", "ALT", "ACCENT"];
export const LF = { ON: 1, REV: 2, FREE: 4, SWELL: 8 };          /* a lane's flags */
export const SF = { BREAK: 1, ONCE: 2 };                          /* a scene's: RUN ON (into the break), ONCE (a fill) */
/* app.c MC_NAME: a name is its index (append only) */
export const NAMES = ["", "ROLLER", "AMEN", "SNARE ROLLER", "SNARES", "MATH 5/7", "FILL", "TRIPLET ROLL",
  "SNARE RUSH", "CHOP FILL", "RISER", "DROP", "STEPPER", "DOUBLE", "7/8 BOUNCE", "BREAKDOWN", "TRIPLET ROLLER",
  "HEMIOLA", "POLY 3/2", "POLY 4/3", "POLY 5/4", "POLY 7/4", "POLY 5/3", "POLY 7/8", "AMEN 3/5", "PRIME MATH",
  "PHASE DRIFT", "GHOST ROLLS", "SHUFFLE VS 16", "3/4 IN 4/4", "6/8 STEPPER", "AMEN IN 5S", "HALFTIME 7",
  "TRIPLET HATS", "JUNGLE", "TWO STEP", "HALFTIME", "DOUBLE TIME", "SHUFFLE", "GHOST NOTES", "BREAKBEAT",
  "BROKEN BEAT", "FOUR TO FLOOR", "MINIMAL", "STUTTER", "GLITCH", "REVERSE", "CUT UP", "HAT ROLL", "KICK FILL",
  "SNARE FILL", "BUILD", "INTRO", "OUTRO"];
export const MATH_LEN = [[0, 0, 0, 0], [0, 0, 0, 12], [16, 0, 12, 10], [15, 0, 12, 7], [14, 16, 11, 7], [10, 14, 11, 13], [7, 12, 9, 5], [5, 7, 11, 13]];
export const sceneLetter = (i) => String.fromCharCode(65 + i);

/* --- the factory's scenes (app.c MC_PRESET: [bars, flags, prole, lanes]; a lane [pattern, rate, mode, prob,
   ratchet, level, rise, flags] or null: off) --- */
const R = { MCR_32: 0, MCR_16: 1, MCR_8: 2, MCR_4: 3, MCR_32T: 4, MCR_16T: 5, MCR_8T: 6 };
const M = { MCM_FIXED: 0, MCM_ROUND: 1, MCM_RANDOM: 2, MCM_ALT: 3, MCM_ACCENT: 4 };
const RUN = SF.BREAK, ONCE = SF.ONCE, SN = 2, SWELL = LF.SWELL, REV = LF.REV;
const L_ = (p, r, m, pr, ra, lv, ri, f) => [p, r, m, pr, ra, lv, ri, f];
export const PRESETS = [
  [2, RUN, 0, [L_("X.........X.....", 1, 0, 100, 1, 100, 0, 0), L_("....X.......X...", 1, 3, 100, 1, 100, 0, 0), L_("X.xxX.x.X.xxX.xx", 1, 1, 100, 1, 80, 0, 0), L_("..............x.", 1, 1, 60, 1, 70, 0, 0)]],
  [2, RUN, 0, [L_("X.X.......XX....", 1, 0, 100, 1, 100, 0, 0), L_("....X..x.x..X..x", 1, 4, 100, 1, 100, 0, 0), L_("XxxxXxxxXxxxXxXx", 1, 1, 90, 1, 75, 0, 0), null]],
  [2, RUN, SN, [L_("X.........X.....X.........X.x...", 1, 0, 100, 1, 100, 0, 0), L_("....X.......X...", 1, 3, 100, 1, 100, 0, 0), L_("X.xX.xX.xX.xX.xX.xX.xX.x", 5, 1, 100, 1, 70, 0, 0), L_("...........x..XX", 2, 1, 100, 3, 85, 0, 0)]],
  [2, RUN, SN, [L_("X.........X.....", 1, 0, 100, 1, 100, 0, 0), L_("..x.X.xx.xx.X.xx", 1, 1, 100, 1, 100, 0, 0), L_("X...x...X...x...", 1, 1, 100, 1, 60, 0, 0), L_("......xX", 2, 4, 100, 3, 90, 0, 0)]],
  [4, RUN, 0, [L_("X..x.", 1, 0, 100, 1, 100, 0, 0), L_("....X..", 1, 3, 100, 1, 100, 0, 0), L_("x.xx.x.xx.x", 0, 2, 100, 1, 65, 0, 0), L_("x..x..x..x.x.", 1, 1, 100, 1, 80, 0, 0)]],
  [1, ONCE, 0, [L_("X.......X.......", 1, 0, 100, 1, 100, 0, 0), L_("x..x..X..x.xx.xXxxXxXXXX", 5, 4, 100, 1, 100, 4, SWELL), null, L_("........x.x..x.x", 1, 1, 100, 1, 70, 0, 0)]],
  [1, ONCE, 0, [L_("X...............", 1, 0, 100, 1, 100, 0, 0), L_("xxxxXXXX", 2, 4, 100, 3, 100, 5, SWELL), null, null]],
  [1, ONCE, 0, [L_("X...............", 1, 0, 100, 1, 100, 0, 0), L_("X", 2, 1, 100, 1, 100, 7, 0), null, null]],
  [1, ONCE, 0, [L_("X..X..X..X..X.X.", 1, 0, 100, 1, 100, 0, 0), L_("..X..X..X..X.XXX", 1, 1, 100, 2, 100, 0, 0), L_("x.x.x.x.x.x.x.x.x.x.x.x.", 5, 2, 60, 1, 60, 0, 0), null]],
  [4, ONCE, 0, [L_("X...X...X.X.XXXX", 3, 0, 100, 1, 100, 0, 0), L_("X", 2, 1, 100, 1, 100, 12, 0), L_("x", 2, 1, 100, 1, 60, 7, 0), null]],
  [1, ONCE, 0, [L_("X...............", 1, 0, 100, 1, 100, 0, 0), L_("X...............", 1, 0, 100, 1, 100, 0, 0), null, L_("........X.......", 1, 0, 100, 1, 100, 0, REV)]],
  [2, RUN, 0, [L_("X.........X.....X.....X.........", 1, 0, 100, 1, 100, 0, 0), L_("........X...............X.......", 1, 0, 100, 1, 100, 0, 0), L_("X.x.X.xxX.x.X.xx", 1, 1, 100, 1, 75, 0, 0), L_("...x.......x..x.", 1, 2, 50, 1, 70, 0, 0)]],
  [2, RUN, 0, [L_("X..X..X.", 1, 0, 100, 1, 100, 0, 0), L_("....X..x....X.x.", 1, 3, 100, 1, 100, 0, 0), L_("XxxxXxxXxxXxXxxx", 0, 1, 100, 1, 70, 0, 0), L_("x.....", 5, 1, 100, 1, 75, 0, 0)]],
  [2, RUN, 0, [L_("X..X.X.", 1, 0, 100, 1, 100, 0, 0), L_("...X......X...", 1, 0, 100, 1, 100, 0, 0), L_("x.xx.xx.xx.x", 5, 2, 100, 1, 70, 0, 0), L_("x...x.x..", 1, 1, 70, 1, 75, 0, 0)]],
  [4, RUN, 0, [L_("X...............X...............", 2, 0, 100, 1, 100, 0, 0), null, L_("x..", 6, 1, 100, 1, 55, 0, 0), L_("x....", 2, 2, 100, 1, 60, 0, 0)]],
  [2, RUN, SN, [L_("X.....X..X..X.....X.X...", 6, 0, 100, 1, 100, 0, 0), L_("...X.....X..", 6, 3, 100, 1, 100, 0, 0), L_("X.xX.xX.xX.xX.xX.xX.xX.x", 5, 1, 100, 1, 70, 0, 0), L_("..............XX", 2, 4, 100, 3, 90, 0, 0)]],
];
export const PRESET_ENUMS = { R, M };          /* (for the test reading app.c) */

export function laneDefaults(role) {
  const V = [20, 60, 70, 45];
  return { hits: 0, accent: 0, len: 16, rate: 1, phase: 0, prob: 100, ratchet: 1, pitch: 0, flags: 0, mode: 0, pick: 0,
    gate: 100, level: 100, role, seed: (role * 37 + 11) & 255, rise: 0, vary: V[role % 4], rsv: 0 };
}
/* a lane's hits and accents from a pattern (x a hit, X accented, . none), its length the pattern's */
export function fromPattern(L, p) {
  L.hits = 0; L.accent = 0;
  let e = 0;
  for (; e < p.length && e < EVENTS; e++) { if (p[e] !== ".") L.hits |= 1 << e; if (p[e] === "X") L.accent |= 1 << e; }
  L.hits >>>= 0; L.accent >>>= 0; L.len = e || 1;
  return L;
}
export const toPattern = (L) => Array.from({ length: L.len }, (_, e) => (L.hits >>> e & 1 ? (L.accent >>> e & 1 ? "X" : "x") : ".")).join("");
export function sceneDefaults(i) {
  const [bars, flags, prole, lanes] = PRESETS[i % SCENES];
  const s = { bars, flags, name: i % SCENES + 1, rsv: 0, lane: [] };
  for (let l = 0; l < LANES; l++) {
    const L = laneDefaults(l === 3 && prole ? prole - 1 : l), p = lanes[l];
    s.lane.push(L);
    if (!p) continue;
    fromPattern(L, p[0]);
    L.flags = LF.ON | p[7]; L.rate = p[1]; L.mode = p[2]; L.prob = p[3]; L.ratchet = p[4]; L.level = p[5]; L.rise = p[6];
    L.seed = (L.seed + i * 13) & 255;
    if (L.rise || L.ratchet > 2) L.vary = Math.floor(L.vary / 3);
    else if (flags & ONCE) L.vary = Math.floor(L.vary / 2);
  }
  return s;
}
export const factoryScenes = () => Array.from({ length: SCENES }, (_, i) => sceneDefaults(i));
/* the chain empty, the black keys on the first eleven scenes (app.c mq_defaults) */
export const factoryChain = () => ({ q: [], key: Array.from({ length: KEYS }, (_, i) => i) });

/* --- bytes: SCNE (OBJ_MC: magic, 16 x 100) and CHAN (OBJ_MQ after its magic: n, rsv[3], q[64], key[11], rsv) --- */
export function readScenes(b) {
  if (!b || b.length !== SCNE_BYTES) throw new Error(`scenes: ${SCNE_BYTES} bytes`);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (dv.getUint32(0, true) !== MCS_MAGIC) throw new Error("scenes of another version (not MCS5)");
  return Array.from({ length: SCENES }, (_, i) => {
    const o = 4 + 100 * i, lane = [];
    for (let l = 0; l < LANES; l++) {
      const a = o + LANE_BYTES * l;
      lane.push({ hits: dv.getUint32(a, true), accent: dv.getUint32(a + 4, true), len: b[a + 8], rate: b[a + 9], phase: b[a + 10],
        prob: b[a + 11], ratchet: b[a + 12], pitch: dv.getInt8(a + 13), flags: b[a + 14], mode: b[a + 15], pick: b[a + 16],
        gate: b[a + 17], level: b[a + 18], role: b[a + 19], seed: b[a + 20], rise: dv.getInt8(a + 21), vary: b[a + 22], rsv: b[a + 23] });
    }
    return { lane, bars: b[o + 96], flags: b[o + 97], name: b[o + 98], rsv: b[o + 99] };
  });
}
const u8 = (v, lo, hi) => Math.max(lo, Math.min(hi, Math.round(+v || 0)));
export function writeScenes(scenes) {
  const b = new Uint8Array(SCNE_BYTES), dv = new DataView(b.buffer);
  dv.setUint32(0, MCS_MAGIC, true);
  scenes.forEach((s, i) => {
    const o = 4 + 100 * i;
    s.lane.forEach((L, l) => {
      const a = o + LANE_BYTES * l;
      dv.setUint32(a, L.hits >>> 0, true); dv.setUint32(a + 4, L.accent >>> 0, true);
      b[a + 8] = u8(L.len, 1, EVENTS); b[a + 9] = u8(L.rate, 0, RATES.length - 1); b[a + 10] = u8(L.phase, 0, EVENTS - 1);
      b[a + 11] = u8(L.prob, 0, 100); b[a + 12] = u8(L.ratchet, 1, 4); dv.setInt8(a + 13, u8(L.pitch, -12, 12)); b[a + 14] = u8(L.flags, 0, 15);
      b[a + 15] = u8(L.mode, 0, MODES.length - 1); b[a + 16] = u8(L.pick, 0, 31); b[a + 17] = u8(L.gate, 5, 100); b[a + 18] = u8(L.level, 0, 100);
      b[a + 19] = u8(L.role, 0, 3); b[a + 20] = u8(L.seed, 0, 255); dv.setInt8(a + 21, u8(L.rise, -24, 24)); b[a + 22] = u8(L.vary, 0, 100); b[a + 23] = L.rsv | 0;
    });
    b[o + 96] = [1, 2, 4, 8].includes(+s.bars) ? +s.bars : u8(s.bars, 1, 16); b[o + 97] = u8(s.flags, 0, 3); b[o + 98] = u8(s.name, 0, NAMES.length - 1); b[o + 99] = s.rsv | 0;
  });
  return b;
}
export function readChain(b) {
  if (!b || b.length !== CHAN_BYTES) throw new Error(`chain: ${CHAN_BYTES} bytes`);
  return { q: Array.from(b.subarray(4, 4 + Math.min(QUEUE, b[0])), (v) => (v < SCENES ? v : 0)), key: Array.from(b.subarray(68, 68 + KEYS), (v) => (v < SCENES ? v : NOKEY)) };
}
export function writeChain(c) {
  const b = new Uint8Array(CHAN_BYTES), q = (c.q || []).slice(0, QUEUE);
  b[0] = q.length; q.forEach((v, i) => { b[4 + i] = u8(v, 0, SCENES - 1); });
  for (let k = 0; k < KEYS; k++) { const v = c.key ? c.key[k] : k; b[68 + k] = v === NOKEY || v == null || v < 0 ? NOKEY : u8(v, 0, SCENES - 1); }
  return b;
}
export const cloneScenes = (s) => readScenes(writeScenes(s));

/* --- the Slice Map, heard (brk.c mc_auto_roles): pcm -1..1, slices (n + 1 offsets), rate -> a role byte a slice --- */
export function autoRoles(pcm, slices, rate) {
  const n = Math.min(32, slices.length - 1), roles = new Array(n).fill(0);
  if (!n || !rate) return roles;
  const op = (hz) => 1 - Math.pow(2, -9.0647 * hz / rate);
  const aL = op(120), aM1 = op(250), aM2 = op(4000), aH = op(7000);
  const lo = [], mid = [], hr = [], en = [];
  let mxl = 0, mxm = 0, mxh = 0, mxe = 0;
  for (let s = 0; s < n; s++) {
    let z = Math.min(slices[s + 1], slices[s] + Math.floor(rate * 3 / 50), pcm.length);
    let l1 = 0, m1 = 0, m2 = 0, h1 = 0, el = 0, em = 0, eh = 0, et = 0;
    for (let i = slices[s]; i < z; i++) {
      const x = pcm[i];
      l1 += (x - l1) * aL; m1 += (x - m1) * aM1; m2 += (x - m2) * aM2; h1 += (x - h1) * aH;
      const m = m2 - m1, h = x - h1;
      el += l1 * l1; em += m * m; eh += h * h; et += x * x;
    }
    lo[s] = el; mid[s] = em; en[s] = et; hr[s] = et > 1e-9 ? eh / et : 0;
    if (et > mxe) mxe = et;
  }
  for (let s = 0; s < n; s++) { if (en[s] < 0.015 * mxe) continue; mxl = Math.max(mxl, lo[s]); mxm = Math.max(mxm, mid[s]); mxh = Math.max(mxh, hr[s]); }
  if (mxe <= 1e-9 || mxl <= 0 || mxm <= 0 || mxh <= 0) return roles;
  const best = [0, 0, 0], bs = [-1, -1, -1];
  for (let s = 0; s < n; s++) {
    if (en[s] < 0.015 * mxe) continue;
    const nk = lo[s] / mxl, ns = mid[s] / mxm, nh = hr[s] / mxh, sc = [nk - 0.5 * ns, ns - 0.7 * nk, nh - 0.6 * Math.max(nk, ns)];
    for (let r = 0; r < 3; r++) if (sc[r] > bs[r]) { bs[r] = sc[r]; best[r] = s; }
    if (nk >= 0.45 && nk >= 0.8 * ns) roles[s] |= 1;
    if (ns >= 0.5 && ns > nk) roles[s] |= 2;
    if (!roles[s] && nh >= 0.6 && nk < 0.3 && ns < 0.5) roles[s] |= 4;
    if (!roles[s] && en[s] >= 0.08 * mxe) roles[s] |= 8;
  }
  for (let r = 0; r < 3; r++) if (bs[r] > -1) roles[best[r]] |= 1 << r;
  if (roles.some((v) => v & 8)) return roles;
  for (let s = 0; s < n; s++) if (roles[s] & 2 && s !== best[1]) roles[s] |= 8;
  return roles;
}
/* a pack break's roles (SSET: a role letter a slice, "-" or nothing: heard) over the heard ones */
export const rolesWith = (heard, set) => heard.map((v, i) => { const c = set && set[i]; return c && c !== "-" ? ({ K: 1, S: 2, H: 4, P: 8 })[c] || v : v; });

/* --- the lanes played (brk.c mc_step, mc_event_kind, mc_tick): the same integers, the same choices --- */
export function hash(seed, lane, e) {
  let x = (Math.imul((seed + 1) >>> 0, 0x9E3779B1) ^ Math.imul((lane + 1) >>> 0, 0x85EBCA6B) ^ Math.imul((e + 1) >>> 0, 0xC2B2AE35)) >>> 0;
  x = (x ^ (x >>> 15)) >>> 0; x = Math.imul(x, 0x2C1B3C6D) >>> 0; x = (x ^ (x >>> 12)) >>> 0; x = Math.imul(x, 0x297A2D39) >>> 0;
  return (x ^ (x >>> 15)) >>> 0;
}
export const HIT = 1, ACCENT = 2, ADDED = 3, DROPPED = 4;
export function eventKind(L, l, pos, dens) {
  const ll = L.len || 1, hit = L.hits >>> (pos % ll) & 1, acc = L.accent >>> (pos % ll) & 1;
  if (hit && dens < 50 && pos && !acc && hash((L.seed + 7) & 0xFFFFFFFF, l, pos) % 50 >= dens) return DROPPED;
  if (!hit && dens > 50 && hash(L.seed + 3, l, pos) % 100 < Math.floor((dens - 50) * (l ? 14 : 6) / 10)) return ADDED;
  return !hit ? 0 : acc ? ACCENT : HIT;
}
const barsOf = (sc) => Math.max(1, Math.min(16, sc.bars | 0));
const PRIO = [3, 4, 1, 2];

/* a scene player (the engine's MATHCORE, one voice): start(scene); then, every 16th n (0, 1, 2 ...), step(n) and, at each
   of its TICKS master ticks t = n * TICKS + k, tick(t): the hit heard {lane, slice, gain, rev, gate, pitch, extra
   (more hits in the event), every (their spacing, ticks), late (16ths), runOn} or null. next(scene): what follows
   its phrase (a scene, or -1: silence) */
export class Player {
  constructor(scenes, roles, macros = {}) {
    this.scenes = scenes; this.roles = roles; this.mac = { density: 50, math: 0, speed: 50, chaos: 0, ...macros };
    this.cur = -1; this.go = -1; this.next = (s) => s; this.n = [0, 0, 0, 0];
  }
  start(s) { this.go = s; }
  begin(s, at) {
    this.cur = s >= 0 && s < SCENES ? s : -1; this.steps = 0; this.base = at;
    this.n = this.n.map((v, l) => (this.cur >= 0 && this.scenes[this.cur].lane[l].flags & LF.FREE ? v : 0));
    this.hold = [0, 0, 0, 0]; this.score = [0, 0, 0, 0];
  }
  step(at) {                                      /* 16th at begins (mc_step): a start, or the phrase done */
    if (this.go !== -1) { this.begin(this.go, at); this.go = -1; }
    else if (this.cur >= 0 && this.steps >= barsOf(this.scenes[this.cur]) * STEPS_PER_BAR) {
      const nx = this.next(this.cur);
      if (nx < 0 || nx >= SCENES) this.cur = -1; else this.begin(nx, at);
    }
    if (this.cur >= 0) this.steps++;
    return this.cur;
  }
  list(role) { const out = []; this.roles.forEach((v, i) => { if (v >> role & 1) out.push(i); }); return out; }
  tick(t) {
    if (this.cur < 0) return null;
    const sc = this.scenes[this.cur], { density: dens, math, speed: spd, chaos } = this.mac;
    const mlen = MATH_LEN[Math.floor(Math.min(100, math) * (MATH_LEN.length - 1) / 100)];
    let win = -1, winScore = 0, w = null;
    for (let l = 0; l < LANES; l++) {
      const L = sc.lane[l];
      if (!(L.flags & LF.ON)) continue;
      const role = L.role % 4, ll = L.len || 1;
      let vary = Math.min(100, L.vary), r = RATE_TICKS[L.rate < RATES.length ? L.rate : 1], len = Math.max(1, Math.min(EVENTS, L.len));
      if (spd < 34) r *= 2; else if (spd > 66 && r > 4) r = r / 2;
      if (L.rise && !(L.flags & LF.SWELL)) { const q = Math.floor((this.steps ? this.steps - 1 : 0) * 4 / (barsOf(sc) * STEPS_PER_BAR)); r >>= Math.min(3, q); if (r < 3) r = 3; }
      const rel = L.flags & LF.FREE ? t : t - this.base * TICKS;
      if (rel % r) continue;
      const e = rel / r;
      if (mlen[l]) len = mlen[l];
      const pos = (e + L.phase) % len, acc = L.accent >>> (pos % ll) & 1, kind = eventKind(L, l, pos, dens);
      let hit = kind === HIT || kind === ACCENT || kind === ADDED, ghost = false, late = 0;
      if (vary && this.steps > (barsOf(sc) - 1) * STEPS_PER_BAR) vary = Math.min(100, Math.floor(vary * 3 / 2) + 20);
      if (vary) {
        const h = hash(L.seed + 21, l, e) % 100, nb = ((L.hits >>> ((pos + 1) % ll)) | (L.hits >>> ((pos + ll - 1) % ll))) & 1;
        if (!hit && (role === 1 || role === 3) && (nb || pos & 1) && h < Math.floor(vary * (nb ? 40 : 12) / 100)) hit = ghost = true;
        else if (!hit && role === 0 && (L.hits >>> ((pos + 1) % ll) & 1) && h < Math.floor(vary * 15 / 100)) hit = ghost = true;
        else if (hit && role === 2 && !acc && pos & 1 && h < Math.floor(vary * 20 / 100)) hit = false;
      }
      if (!hit) continue;
      if (!ghost && L.prob < 100 && hash(L.seed, l, e) % 100 >= L.prob) continue;
      const list = this.list(role);
      if (!list.length) continue;
      let k;
      switch (L.mode) {
        case 1: k = L.pick + this.n[l]; break;
        case 2: k = hash(L.seed + 101, l, e); break;
        case 3: k = L.pick + (this.n[l] & 1); break;
        case 4: k = L.pick + acc; break;
        default: k = L.pick;
      }
      let rat = L.ratchet, fl = L.flags;
      if (vary) {
        const h = hash(L.seed + 31, l, e) % 100, n = list.length;
        if (n > 1 && h < Math.floor(vary / 2)) k += 1 + h % (n - 1);
        if (role === 2 && !acc && rat < 2 && r >= 12 && h % 5 === 2 && h < Math.floor(vary * 30 / 100)) rat = 2;
        else if (role === 1 && acc && rat < 2 && h % 4 === 1 && h < Math.floor(vary * 25 / 100)) rat = 2;
        if (ghost || (role === 2 && pos & 1)) late = (hash(L.seed + 51, l, e) % 100) * vary * 1e-5;
      }
      if (chaos) {
        const h = hash(L.seed + 5, l, e) % 100, c2 = hash(L.seed + 9, l, e) % 100;
        if (h < Math.floor(chaos * 6 / 10)) k = hash(L.seed + 11, l, e);
        if (c2 < Math.floor(chaos * 3 / 10)) rat = 2 + c2 % 2;
        if (h % 7 === 3 && c2 < Math.floor(chaos * 15 / 100)) fl ^= LF.REV;
      }
      const s = list[k % list.length];
      this.n[l]++;
      let g = Math.min(100, L.level) * 0.01 * (L.accent && !acc ? 0.55 : 1);
      if (ghost) g *= role === 0 ? 0.6 : 0.3;
      if (vary) { g *= 1 - vary * 0.0035 * ((hash(L.seed + 41, l, e) & 1023) / 1023); if (role === 2 && pos & 1) g *= 1 - vary * 0.003; }
      let pt = L.pitch;
      if (L.rise) { const pr = (this.steps ? this.steps - 1 : 0) / (barsOf(sc) * STEPS_PER_BAR); pt += L.rise * pr; g *= 0.35 + 0.65 * pr; }
      const score = ghost ? 0 : (acc ? 16 : 0) + PRIO[role] * 2 + (rat > 1 ? 1 : 0);
      if (score > winScore || win < 0) { win = l; winScore = score; w = { lane: l, slice: s, gain: g, pitch: pt, flags: fl, rat, r, late, pos }; }
    }
    if (win < 0) return null;
    for (let k = 0; k < LANES; k++) if (this.hold[k] > t && this.score[k] > winScore) return null;
    this.score[win] = winScore;
    const extra = w.rat > 1 ? Math.min(3, w.rat - 1) : 0;
    this.hold = this.hold.map(() => 0); this.hold[win] = extra ? t + w.r : 0;
    const gate = sc.lane[win].gate || 100;
    return { lane: win, slice: w.slice, gain: w.gain, rev: !!(w.flags & LF.REV), gate, pitch: w.pitch, extra, every: extra ? w.r / (extra + 1) : 0,
      late: w.rat < 2 ? w.late : 0, runOn: !!(sc.flags & SF.BREAK), pos: w.pos };
  }
}

/* --- readable scenes (the MCP server's, PRD_PACKS 10.11): letters, names and patterns for the numbers --- */
const flag = (v, b) => !!(v & b);
export function laneToJSON(L, l) {
  return { lane: l + 1, on: flag(L.flags, LF.ON), role: ROLES[L.role % 4], pattern: toPattern(L), rate: RATES[L.rate] || "1/16",
    phase: L.phase, prob: L.prob, ratchet: L.ratchet, pitch: L.pitch, mode: MODES[L.mode] || "FIXED", pick: L.pick, gate: L.gate,
    level: L.level, seed: L.seed, rise: L.rise, vary: L.vary, reverse: flag(L.flags, LF.REV), free: flag(L.flags, LF.FREE), swell: flag(L.flags, LF.SWELL) };
}
export const sceneToJSON = (s, i) => ({ scene: sceneLetter(i), name: NAMES[s.name] || "", bars: s.bars, run_on: flag(s.flags, SF.BREAK), once: flag(s.flags, SF.ONCE),
  lanes: s.lane.map(laneToJSON) });
export const chainToJSON = (c) => ({ chain: c.q.map(sceneLetter), keys: c.key.map((k) => (k === NOKEY ? null : sceneLetter(k))) });
/* "C", "c", 2 -> 2; anything else throws */
export function sceneIndex(v) {
  const i = typeof v === "number" ? v : /^[A-Pa-p]$/.test(String(v)) ? String(v).toUpperCase().charCodeAt(0) - 65 : NaN;
  if (!(i >= 0 && i < SCENES)) throw new Error(`a scene is A..P: ${v}`);
  return i;
}
const pick = (list, v, what) => { const i = typeof v === "number" ? v : list.indexOf(String(v).toUpperCase()); if (!(i >= 0 && i < list.length)) throw new Error(`${what}: one of ${list.join(", ")}`); return i; };
const range = (v, lo, hi, what) => { const n = Math.round(+v); if (!Number.isFinite(n) || n < lo || n > hi) throw new Error(`${what}: ${lo}..${hi}`); return n; };
/* a lane changed by a readable patch (only the fields given) */
export function patchLane(L, p) {
  if (p.pattern !== undefined) { const t = String(p.pattern); if (!/^[xX.]{1,32}$/.test(t)) throw new Error("pattern: x a hit, X accented, . none (1..32)"); fromPattern(L, t); }
  if (p.events !== undefined) L.len = range(p.events, 1, EVENTS, "events");
  if (p.on !== undefined) L.flags = p.on ? L.flags | LF.ON : L.flags & ~LF.ON;
  if (p.reverse !== undefined) L.flags = p.reverse ? L.flags | LF.REV : L.flags & ~LF.REV;
  if (p.free !== undefined) L.flags = p.free ? L.flags | LF.FREE : L.flags & ~LF.FREE;
  if (p.swell !== undefined) L.flags = p.swell ? L.flags | LF.SWELL : L.flags & ~LF.SWELL;
  if (p.role !== undefined) L.role = pick(ROLES, p.role === "K" ? "KICK" : p.role === "S" ? "SNARE" : p.role === "H" ? "HAT" : p.role === "P" ? "PERC" : p.role, "role");
  if (p.rate !== undefined) L.rate = pick(RATES, p.rate, "rate");
  if (p.mode !== undefined) L.mode = pick(MODES, p.mode, "mode");
  for (const [k, lo, hi] of [["phase", 0, 31], ["prob", 0, 100], ["ratchet", 1, 4], ["pitch", -12, 12], ["pick", 0, 31], ["gate", 5, 100],
    ["level", 0, 100], ["seed", 0, 255], ["rise", -24, 24], ["vary", 0, 100]]) if (p[k] !== undefined) L[k] = range(p[k], lo, hi, k);
  return L;
}
/* a scene changed by a readable patch: name, bars, run_on, once, lanes [{lane: 1..4, ...patchLane's}] */
export function patchScene(s, p) {
  if (p.name !== undefined) { const i = NAMES.indexOf(String(p.name).toUpperCase()); if (i < 0) throw new Error(`name: one of ${NAMES.filter(Boolean).join(", ")} (or "")`); s.name = i; }
  if (p.bars !== undefined) { if (![1, 2, 4, 8].includes(+p.bars)) throw new Error("bars: 1, 2, 4 or 8"); s.bars = +p.bars; }
  if (p.run_on !== undefined) s.flags = p.run_on ? s.flags | SF.BREAK : s.flags & ~SF.BREAK;
  if (p.once !== undefined) s.flags = p.once ? s.flags | SF.ONCE : s.flags & ~SF.ONCE;
  for (const lp of p.lanes || []) { const l = range(lp.lane, 1, LANES, "lane") - 1; patchLane(s.lane[l], lp); }
  return s;
}
/* the chain and keys from readable ones: chain ["C", "A"], keys [ "P", null, ...] (11, null none) */
export function chainFrom(c, p) {
  const out = { q: c.q.slice(), key: c.key.slice() };
  if (p.chain !== undefined) { if (!Array.isArray(p.chain) || p.chain.length > QUEUE) throw new Error(`chain: up to ${QUEUE} scenes`); out.q = p.chain.map(sceneIndex); }
  if (p.keys !== undefined) { if (!Array.isArray(p.keys) || p.keys.length > KEYS) throw new Error(`keys: up to ${KEYS}`); p.keys.forEach((k, i) => { out.key[i] = k == null || k === "" ? NOKEY : sceneIndex(k); }); }
  return out;
}
