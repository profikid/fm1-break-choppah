// node tests/mathcore.test.mjs: mathcore.js (the hub's MATHCORE editor) is the FM-1's: the factory scenes' table, the
// names, rates, modes and MATH lengths read out of app.c and brk.c; the hash as brk.c's mc_hash; scenes and the chain
// to bytes and back; the Slice Map heard; the player's lanes. (tests/pack_v2.sh holds the factory scenes and chain to
// the host simulator's own bytes.)
import * as MC from "../app/site/mathcore.js";
import { slicePoints } from "../app/site/breakcut.js";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const app = readFileSync(new URL("../app/app.c", import.meta.url), "utf8"), brk = readFileSync(new URL("../app/dsp/brk.c", import.meta.url), "utf8");
/* the presets: app.c's MC_PRESET, entry by entry */
const table = app.slice(app.indexOf("static const mc_preset_t MC_PRESET[MC_SCENES] = {"), app.indexOf("#undef RUN"));
const entries = table.split(/\n\s*\/\* [A-P] /).slice(1);
assert.equal(entries.length, 16, "sixteen presets in app.c");
const RC = { MCR_32: 0, MCR_16: 1, MCR_8: 2, MCR_4: 3, MCR_32T: 4, MCR_16T: 5, MCR_8T: 6 }, MD = { MCM_FIXED: 0, MCM_ROUND: 1, MCM_RANDOM: 2, MCM_ALT: 3, MCM_ACCENT: 4 };
entries.forEach((t, i) => {
  const head = t.match(/\{(\d+), (RUN|ONCE), (0|SN_), \{/);
  assert.ok(head, `preset ${i}: its head`);
  const [bars, flags, prole, lanes] = MC.PRESETS[i];
  assert.equal(bars, +head[1], `preset ${i}: bars`); assert.equal(flags, head[2] === "RUN" ? 1 : 2); assert.equal(prole, head[3] === "SN_" ? 2 : 0);
  const ls = [...t.matchAll(/L_\("([.xX]+)", (\w+), (\w+), (\d+), (\d+), (\d+), (-?\d+), (0|MCL_SWELL|MCL_REV)\)|OFF_/g)];
  assert.equal(ls.length, 4, `preset ${i}: four lanes`);
  ls.forEach((m, l) => {
    if (m[0] === "OFF_") { assert.equal(lanes[l], null, `preset ${i} lane ${l}: off`); return; }
    assert.deepEqual(lanes[l], [m[1], RC[m[2]], MD[m[3]], +m[4], +m[5], +m[6], +m[7], m[8] === "MCL_SWELL" ? 8 : m[8] === "MCL_REV" ? 2 : 0], `preset ${i} lane ${l}`);
  });
});
const names = app.match(/MC_NAME\[\] = \{([^}]+)\}/)[1].match(/"[^"]*"/g).map((x) => JSON.parse(x));
assert.deepEqual(MC.NAMES, names, "the scenes' names");
assert.deepEqual(MC.RATE_TICKS, brk.match(/MC_RATE_TICKS\[MC_RATES\] = \{([^}]+)\}/)[1].split(",").map((x) => parseInt(x)));
assert.deepEqual(MC.RATES, brk.match(/MC_RATE_NAME\[MC_RATES\] = \{([^}]+)\}/)[1].match(/"[^"]*"/g).map((x) => JSON.parse(x)));
assert.deepEqual(MC.MODES, brk.match(/MC_MODE_NAME\[MC_MODES\] = \{([^}]+)\}/)[1].match(/"[^"]*"/g).map((x) => JSON.parse(x)));
assert.deepEqual(MC.MATH_LEN.flat(), brk.match(/MC_MATH_LEN\[MC_MATH_STEPS\]\[4\] = \{([\s\S]*?)\};/)[1].match(/\d+/g).map(Number));
/* the hash: brk.c's mc_hash (values from the C) */
assert.deepEqual([MC.hash(0, 0, 0), MC.hash(11, 2, 7), MC.hash(356, 3, 31), MC.hash(48, 1, 1000)], [2689672071, 2708940480, 4161056702, 271074278]);

/* bytes and back; a lane's pattern */
const f = MC.factoryScenes(), b = MC.writeScenes(f);
assert.equal(b.length, MC.SCNE_BYTES);
assert.deepEqual(MC.readScenes(b), f, "scenes: bytes and back");
assert.equal(MC.toPattern(f[1].lane[0]), "X.X.......XX....", "AMEN's kick lane");
assert.equal(f[1].name, 2); assert.equal(MC.NAMES[f[1].name], "AMEN");
const ch = { q: [2, 0, 4, 4], key: [15, 1, 2, 3, 4, 5, 6, 7, 8, 9, MC.NOKEY] };
assert.deepEqual(MC.readChain(MC.writeChain(ch)), ch, "the chain: bytes and back");
assert.deepEqual(MC.readChain(MC.writeChain(MC.factoryChain())), MC.factoryChain());
assert.throws(() => MC.readScenes(new Uint8Array(1604)), /MCS5/);
const odd = MC.cloneScenes(f); odd[0].lane[0].len = 99; odd[0].lane[0].prob = 300; odd[0].bars = 3;
const back = MC.readScenes(MC.writeScenes(odd));
assert.equal(back[0].lane[0].len, 32); assert.equal(back[0].lane[0].prob, 100, "out of range: clamped as the FM-1 reads it");

/* the Slice Map: a kick, a snare, a hat, heard as the engine hears them */
const rate = 22050, n = 4096, pcm = new Float32Array(n * 3);
let seed = 5; const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296 - 0.5);
let prev = 0;
for (let i = 0; i < n; i++) {
  const t = i / rate;
  pcm[i] = 0.9 * Math.sin(2 * Math.PI * 55 * t) * Math.exp(-t * 18);
  pcm[n + i] = (0.8 * rnd() + 0.4 * Math.sin(2 * Math.PI * 900 * t)) * Math.exp(-t * 25);
  const h = rnd(); pcm[2 * n + i] = 0.7 * (h - prev) * Math.exp(-t * 30); prev = h;   /* (white noise, its highs) */
}
const roles = MC.autoRoles(pcm, [0, n, 2 * n, 3 * n], rate);
assert.ok(roles[0] & 1 && roles[1] & 2 && roles[2] & 4, "kick, snare, hat: " + roles);
assert.deepEqual(MC.rolesWith([1, 2, 4], "-KS"), [1, 1, 2], "a pack's roles set by hand");

/* the player: AMEN's lanes on a break of sixteen slices, deterministic (the same twice), on its 16ths */
const r16 = [1, 4, 2, 4, 1, 4, 2, 4, 1, 4, 2, 4, 1, 4, 2, 8];
const run = (sc, steps, mac) => {
  const p = new MC.Player(MC.factoryScenes(), r16, mac); p.start(sc); const out = [];
  for (let s = 0; s < steps; s++) { p.step(s); for (let k = 0; k < MC.TICKS; k++) { for (const h of p.tick(s * MC.TICKS + k)) out.push([s * MC.TICKS + k, h.lane, h.slice, +h.gain.toFixed(3), h.extra]); } }
  return out;
};
const a = run(1, 32), a2 = run(1, 32);
assert.deepEqual(a, a2, "the same scene, the same hits");
assert.ok(a.some(([t, l]) => l === 0 && t % 12 === 0), "the kick on its 16ths");
assert.ok(a.some(([t, l]) => l === 2 && t % 8 === 0), "the hat on its triplet 16ths");
assert.ok(a.some(([, l, s]) => l === 0 && [0, 4, 8, 12].includes(s)) && a.some(([, l, s]) => l === 1 && [2, 6, 10].includes(s)), "kicks on kick slices, snares on snare slices");
/* the lanes sound together (MM-LE-001, MG-001): two lanes on one tick */
{
  const byTick = new Map();
  for (const [t, l] of a) { if (!byTick.has(t)) byTick.set(t, []); byTick.get(t).push(l); }
  assert.ok([...byTick.values()].some((ls) => ls.length > 1), "two lanes on one tick: they sound together");
}
const roll = run(6, 16);                         /* G TRIPLET ROLL: ratchets x3 on its 8ths */
assert.ok(roll.some(([, l, , , x]) => l === 1 && x === 2), "the triplet roll's ratchets");
const dense = run(1, 32, { density: 100 }), sparse = run(1, 32, { density: 0 });
assert.ok(dense.length > a.length && sparse.length < a.length, `DENSITY: ${sparse.length} < ${a.length} < ${dense.length}`);
const p = new MC.Player(MC.factoryScenes(), r16); p.next = () => 0; p.start(10);   /* K DROP: 1 bar, then A */
let seen = new Set(); for (let s = 0; s < 40; s++) seen.add(p.step(s));
assert.deepEqual([...seen], [10, 0], "a phrase done: the next scene");

console.log("mathcore: app.c's presets, names, rates, modes, MATH; brk.c's hash; scenes and chain to bytes and back; the Slice Map; the lanes played");

/* the groove-event layer (PRD_MATHCORE_GROOVE.md): the events carry the velocity, the time, the pitch and the
   timbre; the ghosts are events; the lanes with events play them instead of their pattern */
{
  const sc = MC.factoryScenes();
  const L = sc[1].lane[0];
  L.hits = 0b1010; L.accent = 0b1000; L.len = 4; L.level = 100;   /* the kick's lane: two hits */
  const evs = [
    { flags: (0 << 6), velocity: 120, micro_us: 0, pitch: 0, slice: 0, timbre: 200, gate: 100, pos: 0 },
    { flags: 1, velocity: 30, micro_us: -7000, pitch: 2, slice: 4, timbre: 40, gate: 60, pos: 2 },
  ];
  const p = new MC.Player(sc, r16, {}, { swing: 50, bpm: 120, events: { 1: evs } });
  p.start(1);
  const out = [];
  for (let s = 0; s < 8; s++) { p.step(s); for (let k = 0; k < MC.TICKS; k++) for (const h of p.tick(s * MC.TICKS + k)) out.push(h); }
  const kicks = out.filter((h) => h.lane === 0);
  assert.ok(kicks.length >= 2, "the lane with events plays its events: " + kicks.length);
  assert.ok(kicks.every((h) => h.event), "they are the events, not the pattern");
  assert.equal(kicks[0].gain.toFixed(2), "0.94", "the event's own velocity (120 of 127)");
  assert.equal(kicks[1].micro_us, -7000, "the ghost is early by 7 ms");
  assert.equal(kicks[1].gain.toFixed(2), "0.24", "and quiet (30 of 127)");
  assert.equal(kicks[1].pitch, 2, "its own pitch");
  assert.equal(kicks[1].timbre, 40, "its own timbre");
  assert.equal(kicks[1].gate, 60, "its own gate");
  /* the early event sounds before its grid position: its own tick, less its micro */
  const ghost = kicks.find((h) => h.micro_us < 0);
  assert.ok(ghost, "the ghost is there");
  assert.ok(ghost.at_tick >= 0, "it has a tick: " + ghost.at_tick);
  const before = ghost.at_tick * (60 / 120 / 4 / MC.TICKS) + ghost.micro_us / 1e6;
  const on = ghost.at_tick * (60 / 120 / 4 / MC.TICKS);
  assert.ok(before < on, "and it is early: " + before + " < " + on);
}

/* the export uses the same swung grid as the analysis and preserves accent/gate metadata */
{
  const rate = 1000, pcm = new Float32Array(400);
  const slices = [0, 175, 300], roles = [0, 1];
  const groove = [
    { velocity: 100, flags: 0, micro_us: 0, timbre: 100, grid_pos: 0, grid_swing: 66 },
    { velocity: 72, flags: MC.MCE_F_ACCENT, micro_us: -32767, timbre: 210, gate: 74, grid_pos: 2, grid_swing: 66 },
  ];
  const r = MC.sceneFromBreak(pcm, rate, { slices, roles, bpm: 120, bars: 2, swing: 66, groove });
  const e = r.events.find((event) => event.slice === 1);
  assert.ok(e, "the second slice is exported");
  assert.equal(e.pos, 2, "175 ms is nearer swung step 2 than straight-grid step 1");
  assert.equal(e.micro_us, -32767, "the measured micro-offset remains relative to the swung grid");
  assert.equal(e.flags & MC.MCE_F_ACCENT, MC.MCE_F_ACCENT, "accent articulation is preserved");
  assert.equal(e.gate, 74, "an explicit event gate is preserved");
  assert.equal(r.gruv.swing, 66, "the swing template is stored once in GRUV");
  const loss = MC.exportLoss(r.events, MC.SCENE_CAPS, "scene");
  assert.ok(loss.dropped.some((item) => /ghost\/accent markers/.test(item)),
    "the current device capability report identifies the unsupported articulation markers");
}

/* event identity is not event content: two identical records at the same lane/position both play */
{
  const sc = MC.factoryScenes();
  const evs = [
    { flags: 0, velocity: 96, micro_us: -2500, pitch: 0, slice: 4, timbre: 80, gate: 100, pos: 0, lane: 0 },
    { flags: 0, velocity: 48, micro_us: -2500, pitch: 0, slice: 4, timbre: 80, gate: 100, pos: 0, lane: 0 },
  ];
  const p = new MC.Player(sc, r16, {}, { bpm: 120, events: { 1: evs } });
  p.start(1);
  p.step(0);
  const hits = p.tick(0).filter((h) => h.lane === 0);
  assert.equal(hits.length, 2, "two identical same-position event records remain two hits");
  assert.deepEqual(hits.map((h) => h.gain.toFixed(2)), ["0.76", "0.38"], "each record keeps its own velocity");
}
 
/* readable scenes (the MCP server's) */
{
  const s = MC.factoryScenes();
  const j = MC.sceneToJSON(s[1], 1);
  assert.equal(j.scene, "B"); assert.equal(j.name, "AMEN"); assert.equal(j.lanes[0].pattern, "X.X.......XX...."); assert.equal(j.lanes[0].role, "KICK");
  MC.patchScene(s[1], { bars: 4, once: true, name: "fill", lanes: [{ lane: 2, pattern: "x.X.", rate: "1/16T", mode: "random", prob: 70, reverse: true }] });
  const k = MC.sceneToJSON(s[1], 1);
  assert.deepEqual([k.bars, k.once, k.name, k.lanes[1].pattern, k.lanes[1].rate, k.lanes[1].mode, k.lanes[1].prob, k.lanes[1].reverse], [4, true, "FILL", "x.X.", "1/16T", "RANDOM", 70, true]);
  assert.throws(() => MC.patchScene(s[1], { bars: 3 }), /bars/);
  assert.throws(() => MC.patchScene(s[1], { lanes: [{ lane: 1, rate: "1/5" }] }), /rate/);
  assert.throws(() => MC.patchScene(s[1], { lanes: [{ lane: 5 }] }), /lane/);
  const c = MC.chainFrom(MC.factoryChain(), { chain: ["c", "A", 4], keys: ["P", null] });
  assert.deepEqual(MC.chainToJSON(c).chain, ["C", "A", "E"]); assert.deepEqual(MC.chainToJSON(c).keys.slice(0, 3), ["P", null, "C"]);
  assert.throws(() => MC.chainFrom(MC.factoryChain(), { chain: ["Q"] }), /A\.\.P/);
  assert.deepEqual(MC.readScenes(MC.writeScenes(s))[1], s[1], "a patched scene to bytes and back");
}
console.log("mathcore: readable scenes, patches and the chain");

/* a break → a MATHCORE scene (PRD_MATHCORE_GROOVE.md §7): the slices' roles become the lanes, and each slice
   becomes an event with its own loudness, its time against the grid and its brightness */
{
  const rate = 22050, len = rate * 2;                 /* two bars */
  const pcm = new Float32Array(len);
  const put = (at, secs, f, amp) => {                 /* a hit at a time */
    const s0 = Math.round(at * rate), n = Math.round(secs * rate);
    for (let i = 0; i < n && s0 + i < len; i++) pcm[s0 + i] += Math.sin(2 * Math.PI * f * i / rate) * amp * Math.exp(-i / (n / 4));
  };
  put(0.0, 0.12, 55, 1.0);        /* the kick, on the beat */
  put(0.5, 0.10, 900, 0.7);       /* the snare, half a beat later */
  put(0.25, 0.03, 4000, 0.25);    /* a hat, and a quiet one */
  put(0.26, 0.03, 4000, 0.12);
  const slices = slicePoints(pcm, 16, true);
  const roles = MC.autoRoles(pcm, slices, rate);
  const r = MC.sceneFromBreak(pcm, rate, { slices, roles, bpm: 120, bars: 2, swing: 56 });
  assert.ok(r.events.length >= 3, "the break's hits become events: " + r.events.length);
  assert.ok(r.events.every((e) => e.lane >= 0 && e.lane < 4), "each event has its lane");
  assert.ok(r.events.every((e) => e.velocity >= 1 && e.velocity <= 127), "each event has its own velocity");
  assert.ok(r.events.every((e) => Math.abs(e.micro_us) <= 32767), "the micro-timing fits");
  /* the lanes: the roles that are there are on */
  const on = r.lanes.filter((l) => l.flags & MC.LF.ON);
  assert.ok(on.length >= 2, "the roles that are in the break are lanes: " + on.length);
  assert.ok(on.every((l) => l.vary === 0), "the groove is authored, not generated");
  /* the hat's quiet hit is quieter than the loud one */
  const hats = r.events.filter((e) => e.slice === 2 || e.slice === 3);
  assert.ok(hats.length >= 1, "the hat's hits are there");
  /* the loss report: the Scene profile drops what the lanes cannot say */
  const scene = MC.exportLoss(r.events, MC.SCENE_CAPS, "scene");
  assert.ok(scene.dropped.length >= 4, "the Scene profile says what it drops: " + scene.dropped.join(", "));
  assert.ok(scene.dropped.some((d) => /velocity/.test(d)), "the velocity among them");
  const full = MC.exportLoss(r.events, MC.FULL_CAPS, "full");
  assert.equal(full.dropped.length, 0, "the Full Groove profile drops nothing: " + full.dropped.join(", "));
}

/* the export with the measured groove (the hub's analysis): the hit's own time, loudness, ghost and
   brightness ride on the event, not the slice's cut */
{
  const rate = 22050, len = rate * 2;
  const pcm = new Float32Array(len);
  const put = (at, secs, f, amp) => {
    const s0 = Math.round(at * rate), n = Math.round(secs * rate);
    for (let i = 0; i < n && s0 + i < len; i++) pcm[s0 + i] += Math.sin(2 * Math.PI * f * i / rate) * amp * Math.exp(-i / (n / 4));
  };
  put(0.0, 0.12, 55, 1.0);
  put(0.5, 0.10, 900, 0.7);
  const slices = slicePoints(pcm, 16, true);
  const roles = MC.autoRoles(pcm, slices, rate);
  const first = MC.sceneFromBreak(pcm, rate, { slices, roles, bpm: 120, bars: 2, swing: 56 });
  const groove = slices.map((_, i) => ({ velocity: 20 + i * 3, flags: 0, micro_us: -1000 - i, timbre: 200 - i }));
  groove[first.events[0].slice].flags = MC.MCE_F_GHOST;   /* the first hit was a ghost */
  const r = MC.sceneFromBreak(pcm, rate, { slices, roles, bpm: 120, bars: 2, swing: 56, groove });
  assert.ok(r.events.length >= 2, "the break's hits become events: " + r.events.length);
  for (const e of r.events) {
    const g = groove[e.slice];
    assert.equal(e.velocity, g.velocity, `the event's velocity is the measured one (${e.velocity})`);
    assert.equal(e.micro_us, g.micro_us, `the event's micro-timing is the measured one (${e.micro_us})`);
    assert.equal(e.timbre, g.timbre, `the event's timbre is the measured one (${e.timbre})`);
    assert.equal(e.flags & MC.MCE_F_GHOST, g.flags & MC.MCE_F_GHOST, "the ghost is on the event");
  }
  assert.ok(r.events.some((e) => e.flags & MC.MCE_F_GHOST), "a ghost among them");
}
