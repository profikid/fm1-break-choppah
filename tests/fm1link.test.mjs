// node tests/fm1link.test.mjs: the hub's install (fm1link.js + chpk.js installImages) against the stand-in FM-1
// (tests/page_send/fake_fm1.js): a pack over both banks arrives byte for byte on a 1.7 and on a 1.6, over a link that
// loses frames and answers and restarts once; what is on the FM-1 reads back (the firmware, the pack, its theme);
// a playing FM-1 is refused, a 1.5 can't take a pack, a smaller pack empties bank 2.
import * as P from "../app/site/chpk.js";
import { FM1Link, packOf } from "../app/site/fm1link.js";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const FAKE = readFileSync(new URL("./page_send/fake_fm1.js", import.meta.url), "utf8");
function fm1(faults) {                    /* a fresh stand-in: window.FM1 and navigator.requestMIDIAccess */
  globalThis.window = globalThis;
  globalThis.FAULTS = faults;
  new Function(FAKE)();
  return globalThis.FM1;
}

let seed = 7;
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const noise = (n) => Float32Array.from({ length: n }, (_, i) => 0.6 * Math.sin(i / (3 + (i >> 12) % 5)) * (0.3 + rnd()));
const brk = (name, n) => ({ name, bpm: 136.9, rate: 22050, pcm: noise(n), slices: Array.from({ length: 17 }, (_, i) => Math.floor(n * i / 16)) });
function pack(names, n, { overlay = true, name = "BIG PACK" } = {}) {
  const bank = P.buildBank(names.map((x) => brk(x, n)));
  const scenes = new Uint8Array(1604); new DataView(scenes.buffer).setUint32(0, 0x3553434D, true); scenes[4] = 2;
  const ov = overlay ? { rate: 22050, len: 80000, bpm: 136.9, bars: 4, data: P.adpcmEncode(noise(80000)) } : null;
  const meta = { id: P.uuid(), name, author: "Laurens", theme: { hi: "#00ffaa", bg: "#101820" },
    slots: names.map((_, i) => ({ loop_steps: i ? 32 : 16 })) };
  return P.parsePack(P.buildPack({ meta, bank, scenes, overlay: ov }));
}
const same = (flash, img) => !img || img.every((v, i) => flash[i] === v);
const t0 = Date.now();

/* the install file: the banks through the hub and back */
{
  const a = new Uint8Array([1, 2, 3]), b = new Uint8Array([9]);
  const u = P.unpackImages(P.packImages([a, null]));
  assert.deepEqual([...u[0]], [1, 2, 3]); assert.equal(u[1], null);
  assert.deepEqual([...P.unpackImages(P.packImages([a, b]))[1]], [9]);
  assert.throws(() => P.unpackImages(new Uint8Array(8)), /not an install file/);
}
/* "AT" at a sector's 0xF06 nudged, the CRC made again */
{
  const img = P.buildBank([brk("AT", 30000)]);
  img[0xF06] = 0x41; img[0xF07] = 0x54;
  const dv = new DataView(img.buffer);
  dv.setUint32(12, P.crc32(img.subarray(16, 16 + dv.getUint32(8, true))), true);
  P.noAt(img);
  assert.notEqual(img[0xF06] === 0x41 && img[0xF07] === 0x54, true);
  assert.equal(P.readBank(img).slots.length, 1, "the nudged bank still checks");
}

const big = pack(["AMEN", "THINK", "APACHE", "WORM", "SPORT", "COLD"], 100000);
const L = P.packLayout(big.slots.map((s) => s.len), P.BANK_CAPS, P.overlayBytes(big.overlay));
assert.ok(L[1] >= 1, "the test pack spans both banks");

for (const faults of [{ proto: 3, name: "CHOPPAH 1.7.0" },
  { proto: 3, name: "CHOPPAH 1.7.0", seed: 21, pIn: 0.03, pOut: 0.03, pSlow: 0.005, rebootAt: 150 }]) {
  const F = fm1(faults);
  const link = await FM1Link.open();
  assert.ok(link, "the FM-1 found");
  let st = await link.status();
  assert.equal(st.name, "CHOPPAH 1.7.0"); assert.equal(st.proto, 3); assert.equal(st.pack, null); assert.equal(st.breaks.length, 0);
  const imgs = P.installImages(big, st.proto, st.caps);
  let last = 0;
  await link.install(imgs, (p) => { if (!faults.rebootAt) assert.ok(p >= last - 1e-9, "progress only goes up on a good link"); last = p; });
  assert.ok(same(F.flash[0], imgs[0]) && same(F.flash[1], imgs[1]), "both banks byte for byte " + JSON.stringify(faults));
  {                                       /* BEGIN tells the upload screen the pack's name and where each bank is in it */
    const bg = F.st.begins, tot = imgs[0].length + imgs[1].length;
    assert.ok(bg && bg.length >= 2, "BEGIN seen");
    assert.deepEqual(bg[0], { k: 0, size: imgs[0].length, total: tot, before: 0, name: "BIG PACK" });
    assert.deepEqual(bg[bg.length - 1], { k: 1, size: imgs[1].length, total: tot, before: imgs[0].length, name: "BIG PACK" });
  }
  st = await link.status();
  assert.equal(st.pack.id, big.meta.id); assert.equal(st.pack.name, "BIG PACK"); assert.equal(st.pack.author, big.meta.author.slice(0, 16));
  assert.equal(st.pack.theme.hi, "#00ffad", "the theme as the FM-1 keeps it (RGB565)");
  assert.deepEqual(st.breaks.map((b) => b.name), ["AMEN", "THINK", "APACHE", "WORM", "SPORT", "COLD"]);
  if (faults.rebootAt) assert.equal(F.st.reboots, 1, "it restarted on the way");
  /* a smaller pack in its place: bank 2 emptied */
  const small = pack(["ONE"], 20000, { overlay: false, name: "SMALL" });
  const si = P.installImages(small, 3);
  assert.equal(si[1], null);
  const after = await link.install(si);
  assert.deepEqual([after.counts[0], after.counts[1]], [1, 0]);
  assert.equal((await link.status()).pack.name, "SMALL");
  link.close();
}

/* a 1.6: the breaks alone, v1 banks; no pack to read back */
{
  const F = fm1({ proto: 2, name: "CHOPPAH 1.6.0" });
  const link = await FM1Link.open();
  const imgs = P.installImages(big, 2);
  assert.equal(new DataView(imgs[0].buffer).getUint16(4, true), 1, "a v1 bank for 1.6");
  await link.install(imgs);
  assert.ok(same(F.flash[0], imgs[0]) && same(F.flash[1], imgs[1]));
  const st = await link.status();
  assert.equal(st.pack, null); assert.equal(st.breaks.length, 6);
  link.close();
}

/* playing: refused, nothing written */
{
  const F = fm1({ proto: 3, playing: true });
  const link = await FM1Link.open();
  const st = await link.status();
  assert.equal(st.playing, true);
  await assert.rejects(link.install(P.installImages(big, 3)), /playing/);
  assert.ok(F.flash[0].every((v) => v === 0xFF), "nothing written");
}

/* MATHCORE live (INFO 4): the scenes and the chain read and written, played, launched, the macros, a lane muted, stopped */
{
  const F = fm1({ proto: 4, name: "CHOPPAH 1.7.0", seed: 33, pIn: 0.05, pOut: 0.05 });
  const link = await FM1Link.open();
  await link.info();
  assert.equal(link.live, true);
  const mc = await link.mathcore();
  assert.equal(new DataView(mc.scenes.buffer).getUint32(0, true), 0x3553434D); assert.equal(mc.chain.length, 80);
  const scene = new Uint8Array(100); scene[96] = 8; scene[0] = 0x55;
  await link.putScene(3, scene);
  assert.deepEqual([...F.gs.subarray(4 + 300, 4 + 400)], [...scene], "scene D written");
  const chan = new Uint8Array(80); chan[0] = 2; chan.set([3, 1], 4); chan[68] = 15; chan[69] = 0xFF;
  await link.putChain(chan);
  const back = (await link.mathcore()).chain;
  assert.deepEqual([back[0], back[4], back[5], back[68], back[69]], [2, 3, 1, 15, 0xFF], "the chain and keys written");
  await assert.rejects(link.putSet(0, new Uint8Array(4)), /out of/);
  let st = await link.playScene(2);
  assert.deepEqual([st.playing, st.on, st.scene], [true, 2, 2]);
  st = await link.macros(80, 50, 50, 0); assert.deepEqual(st.macros, [80, 50, 50, 0]);
  st = await link.playChain(); assert.equal(st.scene, 3, "the chain from its first entry");
  st = await link.launch(5); assert.equal(st.scene, 5);
  st = await link.laneState(0, -1); assert.equal(st.state[0], -1);
  st = await link.edit(7, 2); assert.deepEqual([st.edit, st.lane], [7, 2]);
  st = await link.stop(); assert.deepEqual([st.playing, st.on, st.scene], [false, 0, -1]);
  assert.deepEqual(F.mc.ops.map((o) => o[0]).filter((o, i, a) => o !== a[i - 1]), [1, 4, 2, 3, 6, 5, 0], "(a command sent again when its answer was lost: safe twice)");
  link.close();
}
{
  fm1({ proto: 3 });
  const link = await FM1Link.open(); await link.info();
  assert.equal(link.live, false);
  await assert.rejects(link.putScene(0, new Uint8Array(100)), /update/);
  link.close();
}

/* a 1.5 can't take a pack */
assert.throws(() => P.installImages(big, 1), /one break/);
/* the PACK chunk of a head too short to hold it: none */
assert.equal(packOf(new Uint8Array(16 + 168), 1), null);

console.log(`fm1link: the hub's install arrives byte for byte (1.7, 1.6, a bad link), refuses a playing FM-1; MATHCORE live read, written, played (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
process.exit(0);
