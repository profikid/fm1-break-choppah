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
 *     THMB  the thumbnail: WebP, PNG or JPEG, at most 64 KB (optional; never sent to the FM-1)
 * Unknown chunks are kept and passed on (forward compatible). Little-endian throughout. */

export const CHPK_MAGIC = 0x4B504843;       /* "CHPK" */
export const CHPK_VERSION = 1;
export const BANK_MAGIC = 0x31424843;       /* "CHB1" */
export const SLOT_BYTES = 168;
export const MAX_SLOTS = 16;
export const RATES = [11025, 16000, 22050];
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
/* the pack over the FM-1's two banks: [breaks in bank 1, in bank 2], or null when it doesn't fit; reserve: bytes the
   overlay takes, from bank 2 first, the rest from bank 1 */
export function packLayout(lens, caps = BANK_CAPS, reserve = 0) {
  if (reserve) {
    caps = caps.slice();
    const r2 = Math.min(reserve, caps[1] || 0);
    caps[1] = (caps[1] || 0) - r2; caps[0] -= reserve - r2;
    if (caps[0] < 0) return null;
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
    if (!RATES.includes(rate)) fail(`break ${k + 1}: rate ${rate}`);
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
    if (![4, 8, 16].includes(overlay.bars)) fail(`overlay of ${overlay.bars} bars (4, 8 or 16)`);
    if (overlay.data.length !== (overlay.len + 1) >> 1) fail("overlay data");
  }
  if (!packLayout(slots.map((s) => s.len), BANK_CAPS, overlayBytes(overlay))) fail("the breaks and the overlay don't fit the FM-1's two banks");
  const scenes = one("SCNE");
  if (scenes && scenes.length !== 4 + 16 * 100) fail("SCNE: 16 scenes of 100 bytes and their magic");
  const chain = one("CHAN");
  if (chain && chain.length !== CHAN_BYTES) fail(`CHAN: the chain and the keys, ${CHAN_BYTES} bytes`);
  if (meta.slots.length !== slots.length) meta.slots = slots.map((_, i) => ({ ...SLOT_DEFAULTS, ...(meta.slots[i] || {}) }));
  return { meta, bank, slots, scenes, chain, overlay, thumb, thumbType: thumb ? imageType(thumb) : null, chunks };
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
  if (p.thumb) chunks.push({ tag: "THMB", data: p.thumb });
  for (const c of p.extra || []) if (!["META", "BANK", "SCNE", "CHAN", "OVLY", "THMB"].includes(c.tag)) chunks.push(c);
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
export function deviceBanks(breaks, k, meta, { scenes = null, overlay = null, chain = null, caps = BANK_CAPS } = {}) {
  const ch = deviceChunks(meta, breaks.length, scenes, overlay, chain);
  const b2 = breaks.slice(k);
  let ovIn2 = !!ch.overlay;
  if (ch.overlay) {
    const two = buildBankV2(b2, [ch.overlay]);
    if (two.length > caps[1]) ovIn2 = false;
  }
  const img1 = buildBankV2(breaks.slice(0, k), ovIn2 || !ch.overlay ? ch.bank1 : [...ch.bank1, ch.overlay]);
  const img2 = b2.length || ovIn2 ? buildBankV2(b2, ovIn2 ? [ch.overlay] : []) : null;
  if (img1.length > caps[0] || (img2 && img2.length > caps[1])) throw new Error("the pack doesn't fit the FM-1's banks");
  return [img1, img2];
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
  const lens = pk.slots.map((s) => s.len), v2 = proto >= 3;
  const L = packLayout(lens, caps, v2 ? overlayBytes(pk.overlay) : 0);
  if (!L) throw new Error("the pack doesn't fit this FM-1's banks");
  const [k] = L;
  const imgs = v2
    ? deviceBanks(pk.slots, k, pk.meta, { scenes: pk.scenes, overlay: pk.overlay, chain: pk.chain, caps })
    : [buildBank(pk.slots.slice(0, k)), k < pk.slots.length ? buildBank(pk.slots.slice(k)) : null];
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
