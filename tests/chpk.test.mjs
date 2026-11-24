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
