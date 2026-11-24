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
export const FILE_MAX = 512 * 1024;
export const THEME_KEYS = ["bg", "panel", "text", "dim", "hi", "mid", "wave", "warn"];
export const THEME_AMBER = { bg: "#0e0d0b", panel: "#18181e", text: "#ede6d4", dim: "#6b6353", hi: "#ffb846",
  mid: "#a36a1e", wave: "#ffb21a", warn: "#ff6848" };
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
  if (meta.slots.length !== slots.length) meta.slots = slots.map((_, i) => ({ ...SLOT_DEFAULTS, ...(meta.slots[i] || {}) }));
  return { meta, bank, slots, scenes, overlay, thumb, thumbType: thumb ? imageType(thumb) : null, chunks };
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
  if (p.overlay) {
    const h = new Uint8Array(16), hv = new DataView(h.buffer);
    hv.setUint32(0, p.overlay.rate, true); hv.setUint32(4, p.overlay.len, true);
    hv.setUint32(8, Math.round(p.overlay.bpm * 100), true); h[12] = p.overlay.bars;
    const d = new Uint8Array(16 + p.overlay.data.length); d.set(h); d.set(p.overlay.data, 16);
    chunks.push({ tag: "OVLY", data: d });
  }
  if (p.thumb) chunks.push({ tag: "THMB", data: p.thumb });
  for (const c of p.extra || []) if (!["META", "BANK", "SCNE", "OVLY", "THMB"].includes(c.tag)) chunks.push(c);
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
