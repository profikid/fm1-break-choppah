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
export const MCE_F_GHOST = 1, MCE_F_ACCENT = 2;                   /* a groove event's flags (the ghost, the accent) */
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
  /* The reference player (PRD_MATHCORE_GROOVE.md §8): what the browser plays, the device plays.
     scenes: the 16 scenes; roles: the break's slice map; groove: {swing, bpm, events: {scene: [event]}}.
     tick() returns the hits of a tick — every lane's, not only the best one: the lanes sound together. */
  constructor(scenes, roles, macros = {}, groove = null) {
    this.scenes = scenes; this.roles = roles; this.mac = { density: 50, math: 0, speed: 50, chaos: 0, ...macros };
    this.cur = -1; this.go = -1; this.next = (s) => s; this.n = [0, 0, 0, 0];
    this.groove = groove || null;                 /* the groove-event layer */
    this.bpm = (groove && groove.bpm) || 120;     /* the master: the micro-timing is measured against it */
    this.said = new Set();                        /* the events already returned (the early ones) */
  }
  start(s) { this.go = s; }
  begin(s, at) {
    this.cur = s >= 0 && s < SCENES ? s : -1; this.steps = 0; this.base = at;
    this.n = this.n.map((v, l) => (this.cur >= 0 && this.scenes[this.cur].lane[l].flags & LF.FREE ? v : 0));
    this.hold = [0, 0, 0, 0]; this.score = [0, 0, 0, 0];
    this.said = new Set();
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
  /* the groove's events of a lane (the ones with a position are the lane's hits; the rest are the pattern's) */
  evs(l) {
    const g = this.groove && this.groove.events;
    if (!g) return null;
    const list = g[this.cur];
    if (!list) return null;
    const out = list.filter((e) => (((e.flags >> 6) & 3) === l));
    return out.length ? out : null;
  }
  /* the tick's length in seconds (the master: 12 ticks a 16th) */
  tickSec() { return 60 / (this.bpm || 120) / 4 / TICKS; }
  /* the micro-timing in microseconds, and the late (in 16ths) as microseconds too */
  usOf(ticks) { return Math.round(ticks * (60 / (this.bpm || 120) / 4) * 1e6); }
  tick(t) {
    const out = [];
    if (this.cur < 0) return out;
    const sc = this.scenes[this.cur], { density: dens, math, speed: spd, chaos } = this.mac;
    const mlen = MATH_LEN[Math.floor(Math.min(100, math) * (MATH_LEN.length - 1) / 100)];
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
      const pos = (e + L.phase) % len;
      const acc = L.accent >>> (pos % ll) & 1;
      const list = this.list(role);

      /* the groove's events: this lane's hits, with their own velocity, time, pitch and timbre */
      const evs = this.evs(l);
      if (evs) {
        for (let ei = 0; ei < evs.length; ei++) {
          const ev = evs[ei];
          if (ev.pos !== pos) continue;
          /* Event identity is its position in this lane's scene list, not its musical values: two identical hits
             are still two hits (e.g. a flam/drag with the same slice and micro offset). */
          const key = `${l}:${this.cur}:${ei}`;
          if (this.said.has(key)) continue;
          this.said.add(key);
          let g = Math.min(100, L.level) * 0.01 * (ev.velocity == null ? 1 : ev.velocity / 127);
          const pt = L.pitch + (ev.pitch || 0);
          const gate = Math.max(5, Math.min(100, (L.gate || 100) * (ev.gate == null ? 100 : ev.gate) / 100));
          out.push({ lane: l, slice: ev.slice, gain: g, pitch: pt, flags: L.flags, rat: 1, r, late: 0,
            micro_us: ev.micro_us || 0, at_tick: t, pos, gate, timbre: ev.timbre || 0, rev: !!(L.flags & LF.REV),
            runOn: !!(sc.flags & SF.BREAK), event: ev, extra: 0, every: 0 });
        }
        continue;                                   /* the lane's events are its hits */
      }

      /* the lane's pattern, as it always was */
      let kind = eventKind(L, l, pos, dens);
      let hit = kind === HIT || kind === ACCENT || kind === ADDED, ghost = false, late = 0;
      if (vary && this.steps > (barsOf(sc) - 1) * STEPS_PER_BAR) vary = Math.min(100, Math.floor(vary * 1.5) + 20);
      if (vary) {
        const h = hash(L.seed + 21, l, e) % 100, nb = ((L.hits >>> ((pos + 1) % ll)) | (L.hits >>> ((pos + ll - 1) % ll))) & 1;
        if (!hit && (role === 1 || role === 3) && (nb || pos & 1) && h < Math.floor(vary * (nb ? 40 : 12) / 100)) hit = ghost = true;
        else if (!hit && role === 0 && (L.hits >>> ((pos + 1) % ll) & 1) && h < Math.floor(vary * 15 / 100)) hit = ghost = true;
        else if (hit && role === 2 && !acc && pos & 1 && h < Math.floor(vary * 20 / 100)) hit = false;
      }
      if (!hit) continue;
      if (!ghost && L.prob < 100 && hash(L.seed, l, e) % 100 >= L.prob) continue;
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
        if (h < Math.floor(chaos * 0.6)) k = hash(L.seed + 11, l, e);
        if (c2 < Math.floor(chaos * 0.3)) rat = 2 + c2 % 2;
        if (h % 7 === 3 && c2 < Math.floor(chaos * 0.15)) fl ^= LF.REV;
      }
      const sl = list[k % list.length];
      this.n[l]++;
      let g = Math.min(100, L.level) * 0.01 * (L.accent && !acc ? 0.55 : 1);
      if (ghost) g *= role === 0 ? 0.6 : 0.3;
      if (vary) { g *= 1 - vary * 0.0035 * ((hash(L.seed + 41, l, e) & 1023) / 1023); if (role === 2 && (pos & 1)) g *= 1 - vary * 0.003; }
      let pt = L.pitch;
      if (L.rise) { const pr = (this.steps ? this.steps - 1 : 0) / (barsOf(sc) * STEPS_PER_BAR); pt += L.rise * pr; g *= 0.35 + 0.65 * pr; }
      const extra = rat > 1 ? Math.min(3, rat - 1) : 0;
      out.push({ lane: l, slice: sl, gain: g, pitch: pt, flags: fl, rat, r, late, micro_us: 0, at_tick: t, pos,
        gate: L.gate || 100, timbre: 0, rev: !!(fl & LF.REV), runOn: !!(sc.flags & SF.BREAK), extra, every: extra ? r / (extra + 1) : 0 });
    }
    return out;
  }
}

/* --- a break → a MATHCORE scene (PRD_MATHCORE_GROOVE.md §7) ---
   The break's own slices, heard for their roles (the Slice Map), become the four lanes; each slice's loudness,
   its time against the grid, its brightness and its place become an event. Nothing is invented: what the lanes
   cannot say is listed in the loss report. */
export const ROLE_LANE = [0, 1, 2, 3];              /* the lanes: kick, snare, hat, perc */

/* a slice's loudness (0..1) and its brightness (0..255, a coarse one: the crossings in it) */
export function sliceStats(pcm, a, b) {
  let peak = 0, zc = 0, last = 0, n = 0;
  const hi = Math.min(b, pcm.length);
  for (let i = Math.max(0, a); i < hi; i++) {
    const v = pcm[i], av = v < 0 ? -v : v;
    if (av > peak) peak = av;
    if (i > a && (v >= 0) !== (last >= 0)) zc++;
    last = v; n++;
  }
  return { peak, bright: n > 1 ? Math.min(255, Math.round(255 * Math.min(1, (zc / n) * 4))) : 0 };
}

/* what a device can do (MCCTL op 7): the export asks, and says what it could not carry */
export const FULL_CAPS = { velocity: true, micro: true, articulation: true, timbre: true, layering: true, early: true,
  voices: 4, max_events: 64, resolution_us: 100 };
export const SCENE_CAPS = { velocity: false, micro: false, articulation: false, timbre: false, layering: false, early: false,
  voices: 1, max_events: 32, resolution_us: 0 };

/* what the export could not carry, for the caps it was given */
export function exportLoss(events, caps = SCENE_CAPS, profile = "scene") {
  const dropped = [], kept = [];
  const has = (k) => !!(caps && caps[k]);
  if (events.length) {
    if (has("velocity")) kept.push("the per-event velocity"); else dropped.push("the per-event velocity (the lanes have one level)");
    if (has("micro")) kept.push("the micro-timing"); else dropped.push("the micro-timing (the events are on the grid)");
    if (events.some((e) => e.flags & (MCE_F_GHOST | MCE_F_ACCENT))) {
      if (has("articulation")) kept.push("the ghost/accent markers");
      else dropped.push("the ghost/accent markers (velocity still plays, but the device ignores these flags)");
    }
    if (has("timbre")) kept.push("the timbre"); else dropped.push("the timbre (one pitch and one filter a lane)");
    if (has("layering")) kept.push("the lanes together"); else dropped.push("the lanes together (one hit at a time)");
    if (has("early")) kept.push("the early events"); else dropped.push("the early events (everything lands on the grid or later)");
  }
  if (!has("layering") && events.length) dropped.push("only one hit at a time: the lanes take turns");
  if (caps && caps.max_events && events.length > caps.max_events)
    dropped.push(`${events.length - caps.max_events} events over the device's ${caps.max_events}`);
  return { profile, events: events.length, kept, dropped, voices: (caps && caps.voices) || 1 };
}

/* the break → the scene, the events and the context (GRUV)
   o: {pcm, rate, slices, roles, bpm, bars, swing, offset, name} */
export function sceneFromBreak(pcm, rate, o = {}) {
  const sl = o.slices, roles = o.roles;
  if (!sl || sl.length < 2) throw new Error("the break: its slices");
  if (!roles) throw new Error("the break: its roles (the Slice Map)");
  const bpm = o.bpm || 120, bars = o.bars || 2, offset = o.offset || 0;
  const swing = Math.max(50, Math.min(75, Number(o.swing) || 50));
  const sixteenth = 60 / bpm / 4, total = Math.min(EVENTS, bars * 4 * 16);   /* the 16ths in the phrase */
  const gridAt = (k) => Math.floor(k / 2) * 2 * sixteenth + (k % 2 ? 2 * sixteenth * (1 - swing / 100) : 0);
  /* the slices of each role, with their loudness and brightness */
  const perRole = [[], [], [], []], stats = [];
  for (let i = 0; i < sl.length - 1; i++) {
    const st = sliceStats(pcm, sl[i], sl[i + 1]);
    stats.push(st);
    for (let r = 0; r < 4; r++) if (roles[i] >> r & 1) perRole[r].push(i);
  }
  const loudest = Math.max(1e-6, ...stats.map((s) => s.peak));
  const events = [], lanes = [];
  for (let r = 0; r < 4; r++) {
    const L = laneDefaults(r);
    L.flags = 0; L.role = r; L.rate = 1; L.len = Math.max(1, total); L.level = 100; L.vary = 0; L.mode = 0;
    L.prob = 100; L.gate = 100; L.hits = 0; L.accent = 0;
    if (!perRole[r].length) { lanes.push(L); continue; }
    L.flags = LF.ON;
    for (const i of perRole[r]) {
      const at = (sl[i] - offset) / rate;                       /* the slice's time in the break */
      if (at < 0) continue;
      /* The event position is the nearest point on the *same swung grid* used by grooveOf. A straight-grid
         round() here can put a late drag on the wrong side of the midpoint between two swung steps. */
      const g = o.groove && o.groove[i] ? o.groove[i] : null;
      const measuredGrid = g && (g.grid_swing == null || g.grid_swing === swing);
      let pos = -1, nearest = Infinity;
      if (measuredGrid && Number.isInteger(g.grid_pos) && g.grid_pos >= 0 && g.grid_pos < total) pos = g.grid_pos;
      if (pos < 0) for (let k = 0; k < total; k++) {
        const d = Math.abs(at - gridAt(k));
        if (d < nearest) { nearest = d; pos = k; }
      }
      pos = Math.max(0, Math.min(total - 1, pos));
      const micro = Math.round((at - gridAt(pos)) * 1e6);
      const st = stats[i];
      const vel = Math.max(1, Math.min(127, Math.round(127 * st.peak / loudest)));
      /* The measured groove keeps both articulation bits: ghost and accent are not interchangeable. */
      L.hits |= 1 << pos;
      events.push({ flags: (r << 6) | (g ? (g.flags & (MCE_F_GHOST | MCE_F_ACCENT)) : 0),
        velocity: g ? Math.max(1, Math.min(127, g.velocity)) : vel,
        micro_us: measuredGrid ? Math.max(-32767, Math.min(32767, g.micro_us)) : Math.max(-32767, Math.min(32767, micro)),
        pitch: 0, slice: i, timbre: g ? Math.max(0, Math.min(255, g.timbre)) : st.bright,
        gate: g && Number.isFinite(g.gate) ? Math.max(1, Math.min(100, Math.round(g.gate))) : 100, pos, lane: r });
    }
    lanes.push(L);
  }
  const scene = { bars, flags: SF.BREAK, name: 0, rsv: 0, lane: lanes };
  const gruv = { ticks_per_16th: TICKS, swing, flags: 0, bpm,
    scenes: events.length ? [{ scene: 0, events: events.length, first: 0 }] : [] };
  return { scene, events, gruv, lanes, roles, slices: sl };
}

/* the scene, the events and the context, as a pack's chunks (and a scene set's) */
export function grooveChunks(gruv, events) {
  return { gruv, events };
}


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
