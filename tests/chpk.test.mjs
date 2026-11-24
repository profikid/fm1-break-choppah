// node tests/chpk.test.mjs: the pack file (.chpk) round trip, its checks, and the bank byte-for-byte with breaks.html
import * as P from "../app/site/chpk.js";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const tone = (n, f) => Float32Array.from({ length: n }, (_, i) => 0.5 * Math.sin(2 * Math.PI * f * i / 22050));
const brk = (name, n) => ({ name, bpm: 136.9, rate: 22050, pcm: tone(n, 220), slices: Array.from({ length: 17 }, (_, i) => Math.floor(n * i / 16)) });
const bank = P.buildBank([brk("AMEN", 40000), brk("THINK", 30000)]);
assert.equal(P.readBank(bank).slots.length, 2);

const meta = { id: P.uuid(), name: "test pack!", author: "Laurens", tags: ["Jungle", "jungle", "amen"], theme: { hi: "#00FFAA" },
  slots: [{ loop_steps: 16 }, { pitch: 99 }] };
const thumb = new Uint8Array([0x89, 0x50, 0x4E, 0x47, 0, 0, 0, 0, 0]);
const f = P.buildPack({ meta, bank, thumb, extra: [{ tag: "XTRA", data: new Uint8Array([1, 2, 3]) }] });
const p = P.parsePack(f);
assert.equal(p.meta.name, "TEST PACK");
assert.deepEqual(p.meta.tags, ["jungle", "amen"]);
assert.equal(p.meta.theme.hi, "#00ffaa");
assert.equal(p.meta.theme.bg, P.THEME_AMBER.bg);
assert.equal(p.meta.slots[0].loop_steps, 16);
assert.equal(p.meta.slots[1].pitch, 24);
assert.equal(p.slots[1].name, "THINK");
assert.equal(p.thumbType, "image/png");
assert.ok(p.chunks.find((c) => c.tag === "XTRA"));
assert.deepEqual(P.parsePack(P.buildPack({ meta: p.meta, bank: p.bank, thumb: p.thumb })).meta, { ...p.meta, updated: p.meta.updated });

{ /* CHAN: the chain and the black keys, in the file and to the FM-1 after SCNE */
  const chain = new Uint8Array(P.CHAN_BYTES); chain[0] = 2; chain[4] = 3; chain[68] = 15;
  const sc = new Uint8Array(1604); new DataView(sc.buffer).setUint32(0, 0x3553434D, true);
  const q = P.parsePack(P.buildPack({ meta, bank, scenes: sc, chain }));
  assert.deepEqual([...q.chain], [...chain]);
  assert.deepEqual(P.deviceChunks(q.meta, 2, q.scenes, null, q.chain).bank1.map((c) => c.tag), ["PACK", "SSET", "THEM", "SCNE", "CHAN"]);
  assert.throws(() => P.buildPack({ meta, bank, chain: new Uint8Array(5) }), /CHAN/);
  assert.equal(P.parsePack(f).chain, null);
}
const bad = f.slice(); bad[40] ^= 1;
assert.throws(() => P.parsePack(bad), /CRC/);
assert.throws(() => P.parsePack(new Uint8Array(20)), /not a CHOPPAH pack/);
assert.throws(() => P.buildPack({ meta: { ...meta, id: "x" }, bank }), /UUID/);
const big = P.buildBank([brk("HUGE", 800000)]);
assert.throws(() => P.parsePack(P.buildPack({ meta, bank: big })), /at most|fit/);

// the decoder is the page's: the same samples back as breaks.html's adpcmDecode
const page = readFileSync(new URL("../app/site/breaks.html", import.meta.url), "utf8");
assert.ok(page.includes("out[i >> 1] |= code << (4 * (i & 1));"), "breaks.html's encoder layout unchanged");
const x = tone(1000, 440), y = P.adpcmDecode(P.adpcmEncode(x), 1000);
assert.ok(Math.max(...x.slice(200).map((v, i) => Math.abs(v - y[i + 200]))) < 0.05, "ADPCM tracks the tone once it has adapted");
// the overlay counts: it takes bank 2 first
{
  const ov = { rate: 22050, bpm: 136.9, bars: 4, len: 140000 };
  ov.data = P.adpcmEncode(tone(ov.len, 110));
  const withOv = P.parsePack(P.buildPack({ meta, bank, overlay: ov }));
  assert.equal(withOv.overlay.bars, 4); assert.equal(withOv.overlay.len, 140000);
  assert.ok(P.packLayout(Array(6).fill(100000), P.BANK_CAPS, 0)[1] > 0, "past bank 1: bank 2");
  assert.equal(P.packLayout(Array(6).fill(100000), P.BANK_CAPS, P.BANK_CAPS[1]), null, "with bank 2 full of overlay it doesn't fit");
  assert.equal(P.packLayout([60000], P.BANK_CAPS, P.BANK_CAPS[1])[0], 1, "bank 2 full of overlay: breaks in bank 1");
  const bigOv = { ...ov, len: 800000, data: new Uint8Array(400000) };
  assert.throws(() => P.parsePack(P.buildPack({ meta, bank, overlay: bigOv })), /don't fit|at most/);
}
console.log("chpk: all good");

// byte for byte with the page's own buildBank (the code the 1.6 break manager sends to the FM-1)
{
  const cut = (a, b) => page.slice(page.indexOf(a), page.indexOf(b));
  const src = [page.match(/const STEP = \[[^\]]*\];/)[0], page.match(/const ADJ = [^;]*;/)[0],
    page.match(/const clamp = [^;]*;/)[0], cut("function adpcmEncode", "function adpcmDecode"),
    page.match(/const CRC = [^\n]*\n/)[0], page.match(/function crc32\(b\)[^\n]*\n/)[0],
    cut("function buildBank", "function bankSize"), "return buildBank;"].join("\n");
  const pageBuild = new Function(src)();
  const bs = [brk("AMEN", 40001), brk("THINK", 30000)];
  assert.deepEqual(P.buildBank(bs), pageBuild(bs));
  console.log("chpk: bank = breaks.html's");
}
// the FM-1's v2 banks: the chunks after the slot table, the slots' offsets past them, the CRC
{
  const bs = [brk("AMEN", 40000), brk("THINK", 30000)];
  const m = { id: P.uuid(), name: "v2 pack", author: "me", slots: [{ loop_steps: 16, gain_db: -3, roles: "KSH-" }] };
  const [b1, b2] = P.deviceBanks(bs, 2, m, { scenes: new Uint8Array(1604), overlay: { rate: 22050, len: 5000, bpm: 136.9, bars: 4, data: new Uint8Array(2500) } });
  const dv = new DataView(b1.buffer);
  assert.equal(dv.getUint16(4, true), 2);
  assert.equal(P.crc32(b1.subarray(16, 16 + dv.getUint32(8, true))), dv.getUint32(12, true));
  const at = 16 + 168 * 2;
  assert.equal(dv.getUint32(at, true), 4, "PACK SSET THEM SCNE in bank 1");
  assert.equal(String.fromCharCode(...b1.subarray(at + 4, at + 8)), "PACK");
  const off0 = dv.getUint32(16 + 24, true);
  assert.ok(off0 > at + 4 + 56 + 80 + 32 + 1604, "the ADPCM after the chunks");
  assert.deepEqual(b1.subarray(off0, off0 + 20), bs[0].data ? bs[0].data.subarray(0, 20) : P.adpcmEncode(bs[0].pcm).subarray(0, 20));
  const ssAt = at + 4 + 8 + 56 + 8;
  assert.deepEqual([b1[ssAt], b1[ssAt + 3] << 24 >> 24, b1[ssAt + 8], b1[ssAt + 9], b1[ssAt + 10], b1[ssAt + 11], b1[ssAt + 12]], [16, -3, 1, 2, 4, 0, 0xFF]);
  assert.ok(b2, "the overlay in bank 2"); const d2 = new DataView(b2.buffer);
  assert.equal(d2.getUint16(6, true), 0, "bank 2: no breaks, the overlay alone");
  assert.equal(String.fromCharCode(...b2.subarray(16 + 4, 16 + 8)), "OVLY");
  console.log("chpk: v2 banks");
}
