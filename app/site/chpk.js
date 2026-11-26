/* SPDX-License-Identifier: MPL-2.0 */
/* CHOPPAH's pack file (.chpk, docs/PRD_PACKS.md §3.3): one file for import, export and the hub. Shared by the
 * pack builder (app/site/breaks.html) and the pack hub's Worker (vendored there, pinned to a commit of this repo).
 * No DOM, no Node: runs in a browser, a Worker and plain `node` (tests/chpk.test.mjs).
 *
 *   header (16 B): magic "CHPK", u16 version 1, u16 chunks, u32 body length, u32 CRC-32 of the body
 *   body: chunks, each tag[4] u32 len data, zero-padded to 4 bytes
 *     META  UTF-8 JSON (required, first): id, name, author, created, updated, description, tags, theme, slots
 *     BANK  a CHB1 bank (app/dsp/brk_bank.h, version 1) of all the pack's breaks, 1..16 (required)
 *     SCNE  MATHCORE's scenes as the FM-1 stores them (OBJ_MC: magic, 16 x mc_scene_t) (optional)
 *     CHAN  MATHCORE's chain and the black keys' scenes (OBJ_MQ after its magic: n, rsv[3], q[64], key[11], rsv; 80 B) (optional)
 *     OVLY  the overlay: u32 rate, u32 samples, u32 bpm x100, u8 bars, 3 reserved, IMA ADPCM (optional)
 *     LOOP  the layer's loops (docs/PRD_LAYER.md 6, CHOPPAH 1.8): u32 n (<= 4), n x 32-byte records, their ADPCM;
 *           loop 1 is OVLY's when the pack has an overlay (its record's flag LPF_OVLY) (optional)
 *     SHOT  the one-shots: u32 n (<= 16), n x 32-byte records, their ADPCM (optional)
 *     PADS  the ARP page's 11 effect pads, as the FM-1 keeps them: 616 bytes (optional; without: the FM-1's own)
 *     THMB  the thumbnail: WebP, PNG or JPEG, at most 64 KB (optional; never sent to the FM-1)
 * Unknown chunks are kept and passed on (forward compatible). Little-endian throughout. */

export const CHPK_MAGIC = 0x4B504843;       /* "CHPK" */
export const CHPK_VERSION = 1;
export const BANK_MAGIC = 0x31424843;       /* "CHB1" */
export const SLOT_BYTES = 168;
export const MAX_SLOTS = 16;
export const RATES = [11025, 16000, 22050];
/* a break's own rate: the builder plays a slow one at the rate that fits CH_PCM_MAX (e.g. 22013 Hz at 99 BPM), and the
   FM-1 plays a slot at any rate; the overlay keeps to RATES */
export const BREAK_RATE_MIN = 8000, BREAK_RATE_MAX = 22050, CH_PCM_MAX = 106496;
/* the FM-1's two pack banks (CLAUDE.md: bank 1 0x97000-0xD5FFF, bank 2 XDATA1 0xE5000-0xFBFFF) */
export const BANK_CAPS = [0xD6000 - 0x97000, 0xFC000 - 0xE5000];
export const THUMB_MAX = 64 * 1024;
export const CHAN_BYTES = 80;                 /* CHAN: MATHCORE's chain and the black keys (n, rsv[3], q[64], key[11], rsv) */
export const FILE_MAX = 512 * 1024;
export const THEME_KEYS = ["bg", "panel", "text", "dim", "hi", "mid", "wave", "warn"];
/* the factory's colours as the FM-1 draws them (gfx.c AMBER and CHOPPAH's blocks): a pack in these looks as CHOPPAH does */
export const THEME_AMBER = { bg: "#000000", panel: "#18181e", text: "#96969e", dim: "#4a4a52", hi: "#ffb846",
  mid: "#e8881e", wave: "#ffb846", warn: "#ff6848" };
export const SLOT_DEFAULTS = { loop_steps: 32, loop_start: 0, bpm: null, gain_db: 0, pitch: 0, swing: null,
  pitch_mode: null, roles: null };

const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
export function crc32(b) { let c = ~0; for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8); return (~c) >>> 0; }

const STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
  107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
  7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767];
const ADJ = [-1, -1, -1, -1, 2, 4, 6, 8];
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);

/* IMA ADPCM as the FM-1 decodes it (app/dsp/adpcm.c): low nibble first, from state 0 */
export function adpcmDecode(data, n) {
  let pred = 0, idx = 0;
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const code = (data[i >> 1] >> (4 * (i & 1))) & 15, step = STEP[idx];
    let diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    pred = clamp(code & 8 ? pred - diff : pred + diff, -32768, 32767);
    idx = clamp(idx + ADJ[code & 7], 0, 88);
    out[i] = pred / 32768;
  }
  return out;
}
export function adpcmEncode(x) {
  let pred = 0, idx = 0;
  const out = new Uint8Array((x.length + 1) >> 1);
  for (let i = 0; i < x.length; i++) {
    const s = clamp(Math.round(x[i] * 32767), -32768, 32767);
    let d = s - pred, step = STEP[idx], code = 0, diff = step >> 3;
    if (d < 0) { code = 8; d = -d; }
    if (d >= step) { code |= 4; d -= step; diff += step; }
    if (d >= step >> 1) { code |= 2; d -= step >> 1; diff += step >> 1; }
    if (d >= step >> 2) { code |= 1; diff += step >> 2; }
    pred = clamp(code & 8 ? pred - diff : pred + diff, -32768, 32767);
    idx = clamp(idx + ADJ[code & 7], 0, 88);
    out[i >> 1] |= code << (4 * (i & 1));
  }
  return out;
}

/* a bank's bytes for breaks of these sample counts (as breaks.html bankSize) */
export function bankSize(lens) {
  let size = 16 + SLOT_BYTES * lens.length;
  lens.forEach((l, i) => { const d = (l + 1) >> 1; size += d + (i < lens.length - 1 ? (-d & 3) : 0); });
  return size;
}
/* the overlay's bytes in flash (OVLY: its 16-byte header and its ADPCM), kept at the end of bank 2 (PRD_PACKS 7) */
export const overlayBytes = (ov) => ov ? 16 + ((ov.len + 1) >> 1) : 0;
/* the overlay's most samples: the FM-1 marks where decoding may start every 4096 (brk.c OV_NCK × OV_CK) */
export const OVLY_MAX_LEN = 160 * 4096;
export const OVLY_MAX_BARS = 32;              /* (the FM-1 takes any; the builder and the hub make 1 to 32) */
/* the pack over the FM-1's two banks: [breaks in bank 1, in bank 2], or null when it doesn't fit; reserve: bytes the
   overlay takes, all in one bank (deviceBanks): bank 2 when the breaks leave it room there, else bank 1. reserve may
   also be a list (the overlay, the loops, the shots: each whole in one bank, bank 2 first) */
export function packLayout(lens, caps = BANK_CAPS, reserve = 0) {
  if (Array.isArray(reserve)) {
    const r = reserve.filter((x) => x > 0), m = r.length;
    for (let mask = (1 << m) - 1; mask >= 0; mask--) {   /* (as many in bank 2 as fit) */
      let c0 = caps[0], c1 = caps.length > 1 ? caps[1] : 0;
      r.forEach((x, i) => { if (mask >> i & 1) c1 -= x; else c0 -= x; });
      if (c0 < 0 || c1 < 0 || (c1 < caps[1] && caps.length < 2)) continue;
      const L = packLayout(lens, caps.length > 1 ? [c0, c1] : [c0]);
      if (L) return L;
    }
    return null;
  }
  if (reserve) {
    const in2 = caps.length > 1 && reserve <= caps[1] ? packLayout(lens, [caps[0], caps[1] - reserve]) : null;
    return in2 || (reserve <= caps[0] ? packLayout(lens, [caps[0] - reserve, ...caps.slice(1)]) : null);
  }
  for (let k = Math.min(lens.length, MAX_SLOTS); k >= 0; k--) {
    const a = lens.slice(0, k), b = lens.slice(k);
    if (b.length > MAX_SLOTS || (b.length && caps.length < 2)) continue;
    if ((!a.length || bankSize(a) <= caps[0]) && (!b.length || bankSize(b) <= caps[1])) return [k, b.length];
  }
  return null;
}

/* a CHB1 bank -> {slots: [{name, bpm, rate, len, bars, slices, data}]} or throws */
export function readBank(img) {
  const fail = (m) => { throw new Error("bank: " + m); };
  if (img.length < 16) fail("too short");
  const dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
  if (dv.getUint32(0, true) !== BANK_MAGIC) fail("not a CHB1 bank");
  if (dv.getUint16(4, true) !== 1) fail("bank version " + dv.getUint16(4, true));
  const n = dv.getUint16(6, true), size = dv.getUint32(8, true);
  if (n < 1 || n > MAX_SLOTS) fail(`${n} breaks (1..16)`);
  if (16 + size > img.length || size < SLOT_BYTES * n) fail("size");
  if (crc32(img.subarray(16, 16 + size)) !== dv.getUint32(12, true)) fail("CRC");
  const slots = [];
  for (let k = 0; k < n; k++) {
    const s = 16 + SLOT_BYTES * k, len = dv.getUint32(s + 20, true), off = dv.getUint32(s + 24, true), dl = dv.getUint32(s + 28, true);
    const ns = img[s + 33];
    let name = "";
    for (let i = 0; i < 12 && img[s + i]; i++) name += String.fromCharCode(img[s + i]);
    const rate = dv.getUint32(s + 16, true);
    if (rate < BREAK_RATE_MIN || rate > BREAK_RATE_MAX) fail(`break ${k + 1}: rate ${rate}`);
    if (len > CH_PCM_MAX) fail(`break ${k + 1}: ${len} samples (at most ${CH_PCM_MAX})`);
    if (off + dl > 16 + size || dl !== (len + 1) >> 1) fail(`break ${k + 1}: data`);
    if (![8, 16, 32].includes(ns)) fail(`break ${k + 1}: ${ns} slices`);
    const slices = [];
    for (let i = 0; i <= ns; i++) slices.push(dv.getUint32(s + 36 + 4 * i, true));
    for (let i = 1; i <= ns; i++) if (slices[i] < slices[i - 1] || slices[i] > len) fail(`break ${k + 1}: slice ${i}`);
    slots.push({ name, bpm: dv.getUint32(s + 12, true) / 100, rate, len, bars: img[s + 32], slices,
      data: img.subarray(off, off + dl) });
  }
  return { slots };
}

const enc = new TextEncoder(), dec = new TextDecoder();
const tagOf = (b, o) => String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);

/* a .chpk -> {meta, bank, slots, scenes, overlay, thumb, chunks} or throws (with a reason for people) */
export function parsePack(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const fail = (m) => { throw new Error(m); };
  if (b.length > FILE_MAX) fail(`the file is ${b.length} bytes; a pack is at most ${FILE_MAX}`);
  if (b.length < 16) fail("not a CHOPPAH pack (.chpk)");
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (dv.getUint32(0, true) !== CHPK_MAGIC) fail("not a CHOPPAH pack (.chpk)");
  if (dv.getUint16(4, true) !== CHPK_VERSION) fail(`pack version ${dv.getUint16(4, true)}: this page knows 1`);
  const nch = dv.getUint16(6, true), len = dv.getUint32(8, true);
  if (16 + len > b.length) fail("the file is cut short");
  if (crc32(b.subarray(16, 16 + len)) !== dv.getUint32(12, true)) fail("the file is damaged (CRC)");
  const chunks = [];
  let at = 16;
  for (let i = 0; i < nch; i++) {
    if (at + 8 > 16 + len) fail("chunk table");
    const tag = tagOf(b, at), cl = dv.getUint32(at + 4, true);
    if (at + 8 + cl > 16 + len) fail(`chunk ${tag} runs past the end`);
    chunks.push({ tag, data: b.subarray(at + 8, at + 8 + cl) });
    at += 8 + cl + (-cl & 3);
  }
  const one = (t) => { const c = chunks.filter((x) => x.tag === t); if (c.length > 1) fail(`two ${t} chunks`); return c[0] ? c[0].data : null; };
  const metaB = one("META"), bank = one("BANK");
  if (!metaB) fail("no META chunk");
  if (!bank) fail("no BANK chunk");
  let meta;
  try { meta = JSON.parse(dec.decode(metaB)); } catch (e) { fail("META isn't JSON"); }
  meta = checkMeta(meta);
  const { slots } = readBank(bank);
  const thumb = one("THMB");
  if (thumb) {
    if (thumb.length > THUMB_MAX) fail("the thumbnail is over 64 KB");
    if (!imageType(thumb)) fail("the thumbnail isn't WebP, PNG or JPEG");
  }
  let overlay = null;
  const ov = one("OVLY");
  if (ov) {
    if (ov.length < 16) fail("OVLY too short");
    const ovd = new DataView(ov.buffer, ov.byteOffset, ov.byteLength);
    overlay = { rate: ovd.getUint32(0, true), len: ovd.getUint32(4, true), bpm: ovd.getUint32(8, true) / 100, bars: ov[12],
      data: ov.subarray(16) };
    if (!RATES.includes(overlay.rate)) fail(`overlay rate ${overlay.rate}`);
    if (!(overlay.bars >= 1 && overlay.bars <= OVLY_MAX_BARS)) fail(`overlay of ${overlay.bars} bars (1 to ${OVLY_MAX_BARS})`);
    if (overlay.len > OVLY_MAX_LEN) fail(`overlay of ${overlay.len} samples (at most ${OVLY_MAX_LEN})`);
    if (overlay.data.length !== (overlay.len + 1) >> 1) fail("overlay data");
  }
  const loopB = one("LOOP"), shotB = one("SHOT"), padsB = one("PADS");
  const loops = loopB ? readLoops(loopB, overlay) : [], shots = shotB ? readShots(shotB) : [];
  if (padsB && padsB.length !== PADS_BYTES) fail(`PADS: the 11 effect pads, ${PADS_BYTES} bytes`);
  if (!packLayout(slots.map((s) => s.len), BANK_CAPS, [overlayBytes(overlay), chunkBytes(loopB), chunkBytes(shotB)]))
    fail("the breaks, the overlay, the loops and the shots don't fit the FM-1's two banks");
  const scenes = one("SCNE");
  if (scenes && scenes.length !== 4 + 16 * 100) fail("SCNE: 16 scenes of 100 bytes and their magic");
  const chain = one("CHAN");
  if (chain && chain.length !== CHAN_BYTES) fail(`CHAN: the chain and the keys, ${CHAN_BYTES} bytes`);
  if (meta.slots.length !== slots.length) meta.slots = slots.map((_, i) => ({ ...SLOT_DEFAULTS, ...(meta.slots[i] || {}) }));
  return { meta, bank, slots, scenes, chain, overlay, thumb, thumbType: thumb ? imageType(thumb) : null, chunks,
    loops, shots, loopChunk: loopB, shotChunk: shotB, pads: padsB ? readPads(padsB) : null, padsChunk: padsB };
}

export function imageType(b) {
  if (b.length > 12 && b[0] === 0x52 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x46 && b[8] === 0x57 && b[9] === 0x45 && b[10] === 0x42 && b[11] === 0x50) return "image/webp";
  if (b.length > 8 && b[0] === 0x89 && b[1] === 0x50 && b[2] === 0x4E && b[3] === 0x47) return "image/png";
  if (b.length > 3 && b[0] === 0xFF && b[1] === 0xD8 && b[2] === 0xFF) return "image/jpeg";
  return null;
}

const HEX = /^#[0-9a-f]{6}$/i;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const devName = (s) => String(s || "").toUpperCase().replace(/[^A-Z0-9 ._-]/g, " ").replace(/\s+/g, " ").trim().slice(0, 16);

/* META: checked, trimmed and given its defaults */
export function checkMeta(m) {
  const fail = (x) => { throw new Error("META: " + x); };
  if (!m || typeof m !== "object") fail("not an object");
  if (!UUID.test(m.id || "")) fail("id must be a UUID");
  const name = devName(m.name);
  if (!name) fail("the pack needs a name");
  const theme = {};
  for (const k of THEME_KEYS) theme[k] = HEX.test((m.theme || {})[k] || "") ? m.theme[k].toLowerCase() : THEME_AMBER[k];
  const tags = Array.isArray(m.tags) ? [...new Set(m.tags.map((t) => String(t).toLowerCase().replace(/[^a-z0-9-]/g, "").slice(0, 24)).filter(Boolean))].slice(0, 8) : [];
  const now = Math.floor(Date.now() / 1000);
  const slots = Array.isArray(m.slots) ? m.slots.slice(0, MAX_SLOTS).map(checkSlot) : [];
  return {
    format: "choppah-pack", v: 1, id: m.id.toLowerCase(), name,
    author: devName(m.author), author_full: String(m.author_full || m.author || "").slice(0, 80),
    created: Number.isFinite(m.created) ? Math.floor(m.created) : now,
    updated: Number.isFinite(m.updated) ? Math.floor(m.updated) : now,
    description: String(m.description || "").slice(0, 2000), tags, theme, slots,
  };
}
export function checkSlot(s) {
  s = { ...SLOT_DEFAULTS, ...(s || {}) };
  const num = (v, a, b, d) => (Number.isFinite(+v) && v !== null ? clamp(+v, a, b) : d);
  return {
    loop_steps: [4, 8, 16, 32].includes(+s.loop_steps) ? +s.loop_steps : 32,
    loop_start: Math.round(num(s.loop_start, 0, 31, 0)),
    bpm: s.bpm === null ? null : num(s.bpm, 40, 300, null),
    gain_db: Math.round(num(s.gain_db, -24, 12, 0)),
    pitch: Math.round(num(s.pitch, -24, 24, 0)),
    swing: s.swing === null ? null : Math.round(num(s.swing, 50, 75, null)),
    pitch_mode: ["keep", "speed"].includes(s.pitch_mode) ? s.pitch_mode : null,
    roles: typeof s.roles === "string" && /^[KSHP-]{1,32}$/.test(s.roles) ? s.roles : null,
  };
}

/* {meta, bank, scenes?, overlay?, thumb?, extra?: [{tag, data}]} -> a .chpk */
export function buildPack(p) {
  const meta = checkMeta({ ...p.meta, updated: p.meta.updated || Math.floor(Date.now() / 1000) });
  readBank(p.bank);
  const chunks = [{ tag: "META", data: enc.encode(JSON.stringify(meta)) }, { tag: "BANK", data: p.bank }];
  if (p.scenes) chunks.push({ tag: "SCNE", data: p.scenes });
  if (p.chain) { if (p.chain.length !== CHAN_BYTES) throw new Error(`CHAN: ${CHAN_BYTES} bytes`); chunks.push({ tag: "CHAN", data: p.chain }); }
  if (p.overlay) {
    const h = new Uint8Array(16), hv = new DataView(h.buffer);
    hv.setUint32(0, p.overlay.rate, true); hv.setUint32(4, p.overlay.len, true);
    hv.setUint32(8, Math.round(p.overlay.bpm * 100), true); h[12] = p.overlay.bars;
    const d = new Uint8Array(16 + p.overlay.data.length); d.set(h); d.set(p.overlay.data, 16);
    chunks.push({ tag: "OVLY", data: d });
  }
  if (p.loopChunk) chunks.push({ tag: "LOOP", data: p.loopChunk });
  if (p.shotChunk) chunks.push({ tag: "SHOT", data: p.shotChunk });
  if (p.padsChunk) { if (p.padsChunk.length !== PADS_BYTES) throw new Error(`PADS: ${PADS_BYTES} bytes`); chunks.push({ tag: "PADS", data: p.padsChunk }); }
  if (p.thumb) chunks.push({ tag: "THMB", data: p.thumb });
  for (const c of p.extra || []) if (!["META", "BANK", "SCNE", "CHAN", "OVLY", "LOOP", "SHOT", "PADS", "THMB"].includes(c.tag)) chunks.push(c);
  let len = 0;
  for (const c of chunks) len += 8 + c.data.length + (-c.data.length & 3);
  const out = new Uint8Array(16 + len), dv = new DataView(out.buffer);
  let at = 16;
  for (const c of chunks) {
    for (let i = 0; i < 4; i++) out[at + i] = c.tag.charCodeAt(i);
    dv.setUint32(at + 4, c.data.length, true);
    out.set(c.data, at + 8);
    at += 8 + c.data.length + (-c.data.length & 3);
  }
  dv.setUint32(0, CHPK_MAGIC, true); dv.setUint16(4, CHPK_VERSION, true); dv.setUint16(6, chunks.length, true);
  dv.setUint32(8, len, true); dv.setUint32(12, crc32(out.subarray(16)), true);
  return out;
}

/* a v1 .chb (the 1.6 page's export) -> a pack's parts with a fresh id */
export function packFromChb(chb, meta = {}) {
  readBank(chb);
  return { meta: { name: "MY PACK", ...meta, id: meta.id || uuid() }, bank: chb };
}
export function uuid() {
  if (globalThis.crypto && crypto.randomUUID) return crypto.randomUUID();
  const r = new Uint8Array(16); crypto.getRandomValues(r); r[6] = (r[6] & 15) | 64; r[8] = (r[8] & 63) | 128;
  const h = [...r].map((x) => x.toString(16).padStart(2, "0")).join("");
  return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
}

/* breaks [{name, bpm, rate, len?, slices, data? | pcm}] -> a CHB1 bank (as breaks.html buildBank, byte for byte) */
export function buildBank(list) {
  const n = list.length;
  const datas = list.map((b) => b.data || adpcmEncode(b.pcm));
  let size = SLOT_BYTES * n;
  datas.forEach((d, i) => { size += d.length + (i < n - 1 ? (-d.length & 3) : 0); });
  const body = new Uint8Array(size), dv = new DataView(body.buffer);
  let at = SLOT_BYTES * n;
  list.forEach((b, k) => {
    const s = SLOT_BYTES * k, d = datas[k], len = b.len || b.pcm.length;
    for (let i = 0; i < 12; i++) body[s + i] = i < b.name.length ? b.name.charCodeAt(i) & 0x7F : 0;
    dv.setUint32(s + 12, Math.round(b.bpm * 100), true);
    dv.setUint32(s + 16, b.rate, true);
    dv.setUint32(s + 20, len, true);
    dv.setUint32(s + 24, 16 + at, true);
    dv.setUint32(s + 28, d.length, true);
    body[s + 32] = 2; body[s + 33] = b.slices.length - 1;
    for (let i = 0; i < 33; i++) dv.setUint32(s + 36 + 4 * i, i < b.slices.length ? b.slices[i] : len, true);
    body.set(d, at);
    at += d.length + (-d.length & 3);
  });
  const img = new Uint8Array(16 + body.length), hv = new DataView(img.buffer);
  hv.setUint32(0, BANK_MAGIC, true); hv.setUint16(4, 1, true); hv.setUint16(6, n, true);
  hv.setUint32(8, body.length, true); hv.setUint32(12, crc32(body), true);
  img.set(body, 16);
  return img;
}

/* ------------------------------------------------------------------ the FM-1's banks (PRD_PACKS 3.2, CHOPPAH 1.7+) */
const TAG = (t) => t.charCodeAt(0) | t.charCodeAt(1) << 8 | t.charCodeAt(2) << 16 | t.charCodeAt(3) << 24;
const ROLE_BIT = { K: 1, S: 2, H: 4, P: 8, "-": 0 };
const rgb565 = (hex) => { const v = parseInt(hex.slice(1), 16); return ((v >> 19 & 31) << 11) | ((v >> 10 & 63) << 5) | (v >> 3 & 31); };
const pad16 = (s) => { const b = new Uint8Array(16); for (let i = 0; i < 16 && i < s.length; i++) b[i] = s.charCodeAt(i) & 0x7F; return b; };
function uuidBytes(id) { const h = id.replace(/-/g, ""); return Uint8Array.from({ length: 16 }, (_, i) => parseInt(h.slice(2 * i, 2 * i + 2), 16)); }

/* the device chunks of a pack: PACK, SSET, THEM, SCNE (bank 1) and OVLY (bank 2 when it fits there, else bank 1) */
export function deviceChunks(meta, nbreaks, scenes, overlay, chain = null) {
  const m = checkMeta(meta);
  const pk = new Uint8Array(56), pv = new DataView(pk.buffer);
  pk.set(uuidBytes(m.id), 0); pk.set(pad16(m.name), 16); pk.set(pad16(m.author), 32);
  pv.setUint32(48, m.created >>> 0, true); pv.setUint32(52, m.updated >>> 0, true);
  const ss = new Uint8Array(40 * nbreaks), sv = new DataView(ss.buffer);
  for (let i = 0; i < nbreaks; i++) {
    const s = checkSlot(m.slots[i]), o = 40 * i;
    ss[o] = s.loop_steps; ss[o + 1] = s.loop_start; sv.setInt8(o + 2, s.pitch); sv.setInt8(o + 3, s.gain_db);
    sv.setUint16(o + 4, s.bpm ? Math.round(s.bpm * 100) : 0, true); ss[o + 6] = s.swing || 0;
    for (let k = 0; k < 32; k++) ss[o + 8 + k] = s.roles && k < s.roles.length ? ROLE_BIT[s.roles[k]] : 0xFF;
  }
  const th = new Uint8Array(32), tv = new DataView(th.buffer);
  THEME_KEYS.forEach((k, i) => tv.setUint16(2 * i, rgb565(m.theme[k]), true));
  const c1 = [{ tag: "PACK", data: pk }, { tag: "SSET", data: ss }, { tag: "THEM", data: th }];
  if (scenes) c1.push({ tag: "SCNE", data: scenes });
  if (chain) c1.push({ tag: "CHAN", data: chain });   /* (CHOPPAH 1.7 reads it; before: the factory's chain) */
  let ov = null;
  if (overlay) {
    ov = new Uint8Array(16 + overlay.data.length);
    const ovv = new DataView(ov.buffer);
    ovv.setUint32(0, overlay.rate, true); ovv.setUint32(4, overlay.len, true); ovv.setUint32(8, Math.round(overlay.bpm * 100), true);
    ov[12] = overlay.bars; ov.set(overlay.data, 16);
  }
  return { bank1: c1, overlay: ov ? { tag: "OVLY", data: ov } : null };
}

/* breaks + chunks -> a CHB1 bank of version 2: header, slot table, the chunks, the slots' ADPCM */
export function buildBankV2(list, chunks) {
  const n = list.length, datas = list.map((b) => b.data || adpcmEncode(b.pcm));
  let cl = 4;
  for (const c of chunks) cl += 8 + c.data.length + (-c.data.length & 3);
  let size = SLOT_BYTES * n + cl;
  datas.forEach((d, i) => { size += d.length + (i < n - 1 ? (-d.length & 3) : 0); });
  const body = new Uint8Array(size), dv = new DataView(body.buffer);
  let at = SLOT_BYTES * n;
  dv.setUint32(at, chunks.length, true); at += 4;
  for (const c of chunks) {
    dv.setUint32(at, TAG(c.tag), true); dv.setUint32(at + 4, c.data.length, true); body.set(c.data, at + 8);
    at += 8 + c.data.length + (-c.data.length & 3);
  }
  list.forEach((b, k) => {
    const s = SLOT_BYTES * k, d = datas[k], len = b.len || b.pcm.length;
    for (let i = 0; i < 12; i++) body[s + i] = i < b.name.length ? b.name.charCodeAt(i) & 0x7F : 0;
    dv.setUint32(s + 12, Math.round(b.bpm * 100), true); dv.setUint32(s + 16, b.rate, true); dv.setUint32(s + 20, len, true);
    dv.setUint32(s + 24, 16 + at, true); dv.setUint32(s + 28, d.length, true);
    body[s + 32] = 2; body[s + 33] = b.slices.length - 1;
    for (let i = 0; i < 33; i++) dv.setUint32(s + 36 + 4 * i, i < b.slices.length ? b.slices[i] : len, true);
    body.set(d, at); at += d.length + (-d.length & 3);
  });
  const img = new Uint8Array(16 + size), hv = new DataView(img.buffer);
  hv.setUint32(0, BANK_MAGIC, true); hv.setUint16(4, 2, true); hv.setUint16(6, n, true);
  hv.setUint32(8, size, true); hv.setUint32(12, crc32(body), true);
  img.set(body, 16);
  return img;
}

/* the two images the builder sends to a CHOPPAH of 1.7 and up (INFO version 3): bank 1 with the pack's chunks, the
 * overlay in bank 2 when it fits there. k: the breaks in bank 1 (packLayout's first). -> [bank 1, bank 2 or null] */
export function deviceBanks(breaks, k, meta, { scenes = null, overlay = null, chain = null, caps = BANK_CAPS,
  loopChunk = null, shotChunk = null, padsChunk = null } = {}) {
  const ch = deviceChunks(meta, breaks.length, scenes, overlay, chain);
  const b1 = breaks.slice(0, k), b2 = breaks.slice(k);
  const base = padsChunk ? [...ch.bank1, { tag: "PADS", data: padsChunk }] : ch.bank1;
  const big = [ch.overlay, loopChunk && { tag: "LOOP", data: loopChunk }, shotChunk && { tag: "SHOT", data: shotChunk }].filter(Boolean);
  for (let mask = (1 << big.length) - 1; mask >= 0; mask--) {   /* each big chunk whole in a bank: bank 2 first */
    const in2 = big.filter((_, i) => mask >> i & 1), in1 = big.filter((_, i) => !(mask >> i & 1));
    if (in2.length && caps.length < 2) continue;
    const img1 = buildBankV2(b1, [...base, ...in1]);
    const img2 = b2.length || in2.length ? buildBankV2(b2, in2) : null;
    if (img1.length <= caps[0] && (!img2 || img2.length <= caps[1])) return [img1, img2];
  }
  throw new Error("the pack doesn't fit the FM-1's banks");
}

/* ------------------------------------------------------------- the layer (docs/PRD_LAYER.md 6, CHOPPAH 1.8) */
export const LOOP_REC = 32, SHOT_REC = 32, MAX_LOOPS = 4, MAX_SHOTS = 16, PADS_BYTES = 616, NPADS = 11;
export const LPF_AUTO = 1, LPF_OVLY = 2, SHF_CUT = 1;
export const LOOP_QUANT = ["now", "beat", "bar", "loop"], SHOT_QUANT = ["off", "1/16", "beat"], SHOT_MODES = ["one", "gate", "loop"];
export const SHOT_MAX_LEN = 8 * 22050;        /* (8 s at 22.05 kHz: the FM-1 plays any length, the hub cuts to 4 s) */
export const SHOT_RATE_MIN = 8000, SHOT_RATE_MAX = 22050;
export const chunkBytes = (b) => b ? 8 + b.length + (-b.length & 3) : 0;
const name8 = (s) => { const b = new Uint8Array(8); const n = devName(s).replace(/\s+/g, " ").slice(0, 8); for (let i = 0; i < n.length; i++) b[i] = n.charCodeAt(i) & 0x7F; return b; };
const str8 = (b, o) => { let n = ""; for (let i = 0; i < 8 && b[o + i]; i++) n += String.fromCharCode(b[o + i]); return n; };
const num = (v, a, b, d) => (Number.isFinite(+v) && v !== null && v !== undefined ? clamp(Math.round(+v), a, b) : d);
/* loops [{name, rate, len, bpm, bars, data (ADPCM), level, filter, quant, wstart, wbars, auto}] -> a LOOP chunk. With
   overlay true the first loop is the pack's OVLY (its record points there: give it no data) */
export function loopChunk(loops, overlay = false) {
  if (loops.length > MAX_LOOPS) throw new Error(`at most ${MAX_LOOPS} loops`);
  const datas = loops.map((l, i) => (overlay && !i ? new Uint8Array(0) : l.data));
  let size = 4 + LOOP_REC * loops.length;
  const offs = datas.map((d) => { const o = size; size += d.length + (-d.length & 3); return o; });
  const out = new Uint8Array(size), dv = new DataView(out.buffer);
  dv.setUint32(0, loops.length, true);
  loops.forEach((l, i) => {
    const o = 4 + LOOP_REC * i, q = typeof l.quant === "string" ? LOOP_QUANT.indexOf(l.quant) : l.quant;
    out.set(name8(l.name || `LOOP ${i + 1}`), o);
    dv.setUint32(o + 8, l.rate, true); dv.setUint32(o + 12, l.len, true); dv.setUint32(o + 16, Math.round((l.bpm || 0) * 100), true);
    dv.setUint32(o + 20, overlay && !i ? 0 : offs[i], true);
    out[o + 24] = num(l.bars, 1, 32, 1);
    out[o + 25] = (l.auto ? LPF_AUTO : 0) | (overlay && !i ? LPF_OVLY : 0);
    out[o + 26] = num(l.level, 0, 100, 100); dv.setInt8(o + 27, num(l.filter, -50, 50, 0));
    out[o + 28] = num(q, 0, 3, 2); out[o + 29] = num(l.wstart, 0, 31, 0); out[o + 30] = num(l.wbars, 0, 32, 0);
    out.set(datas[i], offs[i]);
  });
  return out;
}
export function readLoops(b, overlay = null) {
  const fail = (m) => { throw new Error("LOOP: " + m); };
  if (b.length < 4) fail("too short");
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), n = dv.getUint32(0, true), out = [];
  if (n > MAX_LOOPS || b.length < 4 + LOOP_REC * n) fail(`${n} loops`);
  for (let i = 0; i < n; i++) {
    const o = 4 + LOOP_REC * i, len = dv.getUint32(o + 12, true), off = dv.getUint32(o + 20, true), fl = b[o + 25];
    const l = { name: str8(b, o), rate: dv.getUint32(o + 8, true), len, bpm: dv.getUint32(o + 16, true) / 100, bars: b[o + 24],
      auto: !!(fl & LPF_AUTO), fromOverlay: !!(fl & LPF_OVLY), level: b[o + 26], filter: dv.getInt8(o + 27),
      quant: LOOP_QUANT[b[o + 28] & 3], wstart: b[o + 29], wbars: b[o + 30] };
    if (!(l.bars >= 1 && l.bars <= OVLY_MAX_BARS)) fail(`loop ${i + 1}: ${l.bars} bars`);
    if (len > OVLY_MAX_LEN || len < 64) fail(`loop ${i + 1}: ${len} samples`);
    if (l.fromOverlay) {
      if (!overlay || overlay.len !== len) fail(`loop ${i + 1} is the overlay's, and there is none of its length`);
      l.data = overlay.data;
    } else {
      if (!RATES.includes(l.rate)) fail(`loop ${i + 1}: rate ${l.rate}`);
      if (off + ((len + 1) >> 1) > b.length) fail(`loop ${i + 1}: data`);
      l.data = b.subarray(off, off + ((len + 1) >> 1));
    }
    out.push(l);
  }
  return out;
}
/* shots [{name, rate, len, bpm?, data (ADPCM), level, mode, choke, quant, pitch, decay, cut}] -> a SHOT chunk */
export function shotChunk(shots) {
  if (shots.length > MAX_SHOTS) throw new Error(`at most ${MAX_SHOTS} one-shots`);
  let size = 4 + SHOT_REC * shots.length;
  const offs = shots.map((h) => { const o = size; size += h.data.length + (-h.data.length & 3); return o; });
  const out = new Uint8Array(size), dv = new DataView(out.buffer);
  dv.setUint32(0, shots.length, true);
  shots.forEach((h, i) => {
    const o = 4 + SHOT_REC * i;
    const md = typeof h.mode === "string" ? SHOT_MODES.indexOf(h.mode) : h.mode, q = typeof h.quant === "string" ? SHOT_QUANT.indexOf(h.quant) : h.quant;
    out.set(name8(h.name || `SHOT ${i + 1}`), o);
    dv.setUint32(o + 8, h.rate, true); dv.setUint32(o + 12, h.len, true); dv.setUint32(o + 16, Math.round((h.bpm || 0) * 100), true);
    dv.setUint32(o + 20, offs[i], true);
    out[o + 24] = num(h.level, 0, 100, 100); out[o + 25] = num(md, 0, 2, 0); out[o + 26] = num(h.choke, 0, 4, 0);
    out[o + 27] = num(q, 0, 2, 0); dv.setInt8(o + 28, num(h.pitch, -24, 24, 0)); out[o + 29] = num(h.decay, 0, 31, 0);
    out[o + 30] = h.cut ? SHF_CUT : 0;
    out.set(h.data, offs[i]);
  });
  return out;
}
export function readShots(b) {
  const fail = (m) => { throw new Error("SHOT: " + m); };
  if (b.length < 4) fail("too short");
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), n = dv.getUint32(0, true), out = [];
  if (n > MAX_SHOTS || b.length < 4 + SHOT_REC * n) fail(`${n} one-shots`);
  for (let i = 0; i < n; i++) {
    const o = 4 + SHOT_REC * i, len = dv.getUint32(o + 12, true), off = dv.getUint32(o + 20, true);
    const h = { name: str8(b, o), rate: dv.getUint32(o + 8, true), len, bpm: dv.getUint32(o + 16, true) / 100 || null,
      level: b[o + 24], mode: SHOT_MODES[b[o + 25] % 3], choke: b[o + 26], quant: SHOT_QUANT[b[o + 27] % 3],
      pitch: dv.getInt8(o + 28), decay: b[o + 29], cut: !!(b[o + 30] & SHF_CUT) };
    if (h.rate < SHOT_RATE_MIN || h.rate > SHOT_RATE_MAX) fail(`shot ${i + 1}: rate ${h.rate}`);
    if (len < 2 || len > SHOT_MAX_LEN) fail(`shot ${i + 1}: ${len} samples`);
    if (off + ((len + 1) >> 1) > b.length) fail(`shot ${i + 1}: data`);
    h.data = b.subarray(off, off + ((len + 1) >> 1));
    out.push(h);
  }
  return out;
}
/* the effect pads (PRD_LAYER 3.6): each {type, on, target, p: [3], mix, x: [8]} as the FM-1's brk_fxslot_t and
   brk_fxx_t (app/dsp/brk_fx.h), floats little-endian. FX_TYPES: the FM-1's effect library (MB COMP: not a pad) */
export const FX_TYPES = ["EMPTY", "FILTER", "CRUNCH", "DRIVE", "PUNCH", "FLANGER", "RING MOD", "TREMOLO", "REVERB", "DELAY", "EQ", "TAPE", "MB COMP", "CLIP"];
export const PAD_TARGETS = ["mix", "layer", "break"];
const FX_D = [[0, 0, 0, 1], [0.55, 0.3, 0, 1], [0.57, 0.4, 0.2, 1], [0.35, 0.6, 0.7, 1], [0.5, 0.3, 0.3, 1], [0.4, 0.6, 0.75, 0.5],
  [0.35, 0, 0, 0.5], [0.6, 0.7, 0, 1], [0.6, 0.4, 0.25, 0.3], [2 / 7, 0.5, 0.5, 0.35], [0.5, 0.5, 0.5, 0.5], [0.4, 0.5, 0.15, 1],
  [0.5, 0.35, 0.45, 1], [0.4, 0.5, 0.06, 1]];   /* brk_fx_slot_defaults */
const FXX_D = [[0, 0, 0, 0], [0, 0, 0, 0], [1, 0, 0, 0], [0, 0, 0, 0], [3 / 7, 0, 0, 0], [0, 0.5, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0],
  [1, 0, 1, 1], [1, 0, 0, 0], [0.47712, 0.42413, 2 / 3, 0.5], [0.1, 0.50117, 0, 0], [0, 0, 0, 0], [0, 1, 0, 0]];   /* brk_fxx_defaults */
export function padDefaults(type) {
  const t = type >= 0 && type < FX_TYPES.length ? type : 0;
  return { type: t, on: 1, target: "mix", p: FX_D[t].slice(0, 3), mix: FX_D[t][3], x: [...FXX_D[t], 2 / 8, 0.5, 0, 0] };
}
/* the FM-1's own pads as they come (app.c settings_defaults): LP BP HP filters, crunch, drive, ring mod, flanger, a
   gate, tape, a delay, a reverb */
export function factoryPads() {
  const T = [1, 1, 1, 2, 3, 6, 5, 7, 11, 9, 8];
  const P = [[0.40, 0.55, 0.0, 1.0], [0.62, 0.40, 1.0, 1.0], [0.60, 0.70, 0.5, 1.0], [0.75, 0.60, 0.3, 1.0], [0.60, 0.50, 0.6, 1.0],
    [0.45, 0.30, 0.0, 1.0], [0.45, 0.80, 0.85, 1.0], [0.60, 1.00, 0.0, 1.0], [0.50, 0.50, 0.70, 1.0], [2 / 7, 0.60, 0.5, 0.8], [0.75, 0.40, 0.25, 0.6]];
  return T.map((t, k) => ({ ...padDefaults(t), p: P[k].slice(0, 3), mix: P[k][3] }));
}
export function padsChunk(pads) {
  if (pads.length !== NPADS) throw new Error(`${NPADS} effect pads`);
  const out = new Uint8Array(PADS_BYTES), dv = new DataView(out.buffer);
  pads.forEach((pd, i) => {
    const o = 56 * i, t = pd.type >= 0 && pd.type < FX_TYPES.length && pd.type !== 12 ? pd.type : 0;
    const d = padDefaults(t), f = (v, k) => (Number.isFinite(+v) ? clamp(+v, 0, 1) : k);
    out[o] = t; out[o + 1] = pd.on === 0 || pd.on === false ? 0 : 1;
    out[o + 2] = Math.max(0, PAD_TARGETS.indexOf(pd.target || "mix")); out[o + 3] = 0;
    for (let k = 0; k < 3; k++) dv.setFloat32(o + 4 + 4 * k, f((pd.p || [])[k], d.p[k]), true);
    dv.setFloat32(o + 16, f(pd.mix, d.mix), true);
    for (let k = 0; k < 8; k++) dv.setFloat32(o + 20 + 4 * k, f((pd.x || [])[k], d.x[k]), true);
    out[o + 52] = t;
  });
  return out;
}
export function readPads(b) {
  if (b.length !== PADS_BYTES) throw new Error(`PADS: ${PADS_BYTES} bytes`);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), out = [];
  for (let i = 0; i < NPADS; i++) {
    const o = 56 * i;
    out.push({ type: b[o] < FX_TYPES.length ? b[o] : 0, on: b[o + 1] ? 1 : 0, target: PAD_TARGETS[b[o + 2]] || "mix",
      p: [0, 1, 2].map((k) => dv.getFloat32(o + 4 + 4 * k, true)), mix: dv.getFloat32(o + 16, true),
      x: [0, 1, 2, 3, 4, 5, 6, 7].map((k) => dv.getFloat32(o + 20 + 4 * k, true)) });
  }
  return out;
}

/* bank 2 (XDATA1): "AT" never at a sector's 0xF06 (the update loader's record): the byte there nudged (one ADPCM step,
 * inaudible), the CRC made again (breaks.html's noAt, tools/cu_frames.py) */
export function noAt(img) {
  let hit = 0;
  for (let q = 0xF06; q + 1 < img.length; q += 4096) if (img[q] === 0x41 && img[q + 1] === 0x54) { img[q] ^= 1; hit++; }
  if (hit) {
    const dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
    dv.setUint32(12, crc32(img.subarray(16, 16 + dv.getUint32(8, true))), true);
  }
  return img;
}

/* what an FM-1 is sent for a pack (parsePack's), by the break manager's version INFO tells (proto): 3 (CHOPPAH 1.7 on)
 * the v2 banks, with the theme, the LOOP settings, the scenes and the overlay; 2 (1.6) the breaks alone, in v1 banks;
 * 1 (1.4-1.5) can't take a pack. -> [bank 1, bank 2 or null], bank 2 nudged off "AT" (the hub's install, PH-032) */
export function installImages(pk, proto, caps = BANK_CAPS) {
  if (proto < 2) throw new Error("this CHOPPAH takes one break of its own, not a pack: update it to CHOPPAH 1.6 or later");
  const lens = pk.slots.map((s) => s.len), v2 = proto >= 3, ly = proto >= 5;   /* (5: CHOPPAH 1.8, the layer) */
  const L = packLayout(lens, caps, v2 ? [overlayBytes(pk.overlay), ly ? chunkBytes(pk.loopChunk) : 0, ly ? chunkBytes(pk.shotChunk) : 0] : 0);
  if (!L) throw new Error("the pack doesn't fit this FM-1's banks");
  const [k0] = L;
  /* the layout counts the breaks and the big chunks; bank 1 also carries the pack's own (PACK, THEM, SSET, SCNE, CHAN,
     PADS: a few KB). When they tip it over, a break moves to bank 2, and another, until it fits */
  let imgs = null, why = null;
  for (let k = k0; k >= 0 && !imgs; k--) {
    if (pk.slots.length - k > MAX_SLOTS || (k < pk.slots.length && caps.length < 2)) break;
    try {
      imgs = v2
        ? deviceBanks(pk.slots, k, pk.meta, { scenes: pk.scenes, overlay: pk.overlay, chain: pk.chain, caps,
          loopChunk: ly ? pk.loopChunk : null, shotChunk: ly ? pk.shotChunk : null, padsChunk: ly ? pk.padsChunk : null })
        : [buildBank(pk.slots.slice(0, k)), k < pk.slots.length ? buildBank(pk.slots.slice(k)) : null];
    } catch (e) { why = e; }
    if (!v2) break;
  }
  if (!imgs) throw why || new Error("the pack doesn't fit the FM-1's banks");
  if (imgs[1]) noAt(imgs[1]);
  return imgs;
}

/* the install file the hub serves (GET /api/packs/:id/install): "CHIM", version 1, the bank count, then each bank's
 * length u32 (0: the bank emptied) and the banks */
export const CHIM_MAGIC = 0x4D494843;          /* "CHIM" */
export function packImages(imgs) {
  const n = imgs.length, size = 8 + 4 * n + imgs.reduce((a, m) => a + (m ? m.length : 0), 0);
  const out = new Uint8Array(size), dv = new DataView(out.buffer);
  dv.setUint32(0, CHIM_MAGIC, true); dv.setUint16(4, 1, true); dv.setUint16(6, n, true);
  let at = 8 + 4 * n;
  imgs.forEach((m, i) => { dv.setUint32(8 + 4 * i, m ? m.length : 0, true); if (m) { out.set(m, at); at += m.length; } });
  return out;
}
export function unpackImages(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (b.length < 8 || dv.getUint32(0, true) !== CHIM_MAGIC || dv.getUint16(4, true) !== 1) throw new Error("not an install file");
  const n = dv.getUint16(6, true), out = [];
  let at = 8 + 4 * n;
  for (let i = 0; i < n; i++) {
    const len = dv.getUint32(8 + 4 * i, true);
    if (at + len > b.length) throw new Error("the install file is cut short");
    out.push(len ? b.subarray(at, at + len) : null); at += len;
  }
  return out;
}
