// node tests/mathcore.test.mjs: mathcore.js (the hub's MATHCORE editor) is the FM-1's: the factory scenes' table, the
// names, rates, modes and MATH lengths read out of app.c and brk.c; the hash as brk.c's mc_hash; scenes and the chain
// to bytes and back; the Slice Map heard; the player's lanes. (tests/pack_v2.sh holds the factory scenes and chain to
// the host simulator's own bytes.)
import * as MC from "../app/site/mathcore.js";
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
  for (let s = 0; s < steps; s++) { p.step(s); for (let k = 0; k < MC.TICKS; k++) { const h = p.tick(s * MC.TICKS + k); if (h) out.push([s * MC.TICKS + k, h.lane, h.slice, +h.gain.toFixed(3), h.extra]); } }
  return out;
};
const a = run(1, 32), a2 = run(1, 32);
assert.deepEqual(a, a2, "the same scene, the same hits");
assert.ok(a.every(([t]) => t % 12 === 0), "AMEN on its 16ths");
assert.ok(a.some(([, l, s]) => l === 0 && [0, 4, 8, 12].includes(s)) && a.some(([, l, s]) => l === 1 && [2, 6, 10].includes(s)), "kicks on kick slices, snares on snare slices");
const roll = run(6, 16);                         /* G TRIPLET ROLL: ratchets x3 on its 8ths */
assert.ok(roll.some(([, l, , , x]) => l === 1 && x === 2), "the triplet roll's ratchets");
const dense = run(1, 32, { density: 100 }), sparse = run(1, 32, { density: 0 });
assert.ok(dense.length > a.length && sparse.length < a.length, `DENSITY: ${sparse.length} < ${a.length} < ${dense.length}`);
const p = new MC.Player(MC.factoryScenes(), r16); p.next = () => 0; p.start(10);   /* K DROP: 1 bar, then A */
let seen = new Set(); for (let s = 0; s < 40; s++) seen.add(p.step(s));
assert.deepEqual([...seen], [10, 0], "a phrase done: the next scene");

console.log("mathcore: app.c's presets, names, rates, modes, MATH; brk.c's hash; scenes and chain to bytes and back; the Slice Map; the lanes played");

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
