// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Laurens (profikid), CHOPPAH
//
// fm1link.js: CHOPPAH's break manager over Web MIDI (ui/main.c app_sysex), no DOM: find the FM-1, ask what is on it
// (its firmware, the pack and the breaks), and write a bank so it arrives byte for byte over a link that loses frames
// (breaks.html's must / sendBank, the same rules). The hub's install (PRD_PACKS PH-030) uses it; tests/fm1link.test.mjs
// runs it against tests/page_send/fake_fm1.js.
//
// SysEx: F0 7D 43 48 CMD 7-bit-packed DATA F7, the bank in CMD's bits 4-5; answers F0 7D 43 48 0x40|CMD CODE DATA F7.
// 1 INFO, 2 BEGIN {size[, the pack's bytes in all, those before this bank, its name (16)]} (the FM-1's upload screen,
// 1.7.1; older firmware reads the size alone), 3 DATA {offset, bytes} -> {offset}, 4 COMMIT {header}, 5 READ {offset, count} ->
// {offset, bytes}, 6 DELETE, 7 GETSET {offset, count} -> {offset, bytes} (the scenes 0..1603, the chain 1604..1691,
// the LOOP settings), and from INFO version 4 (MATHCORE live, PRD_PACKS 10.10): 8 PUTSET {offset, bytes} -> {offset}
// (the scenes and the chain into RAM, playing or not), 9 STATUS -> 32 bytes, 10 MCCTL {op, d0..d3} -> STATUS.
// The screen reader's HELLO (F0 7D 53 52 01 01 F7) names the firmware.

export const ERR = { 1: "CHOPPAH is playing: stop it (PLAY) and try again.", 2: "The pack doesn't fit.",
  3: "The flash could not be written.", 4: "The pack as written doesn't check: send it again." };
export const TRIES = 8;
export const CHUNK = 448;                 /* DATA's bytes a frame: 521 on the wire, inside the 594 the FM-1 always takes */
const SLOT = 168;

export function pack7(b) {
  const o = [];
  for (let i = 0; i < b.length; i += 7) {
    const g = b.subarray(i, i + 7); let hi = 0;
    g.forEach((v, j) => { hi |= (v >> 7) << j; });
    o.push(hi, ...Array.from(g, (v) => v & 0x7F));
  }
  return o;
}
export function unpack7(p) {
  const o = [];
  for (let i = 0; i < p.length; i += 8) for (let j = 1; j < 8 && i + j < p.length; j++) o.push(p[i + j] | ((p[i] >> (j - 1)) & 1) << 7);
  return new Uint8Array(o);
}
const u32 = (v) => [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255];
const word = (d, i) => new DataView(d.buffer, d.byteOffset, d.byteLength).getUint32(i, true);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const str = (b) => { let s = ""; for (let i = 0; i < b.length && b[i]; i++) s += String.fromCharCode(b[i]); return s.trim(); };
const uuidOf = (b) => { const h = [...b].map((x) => x.toString(16).padStart(2, "0")).join("");
  return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`; };
const rgb = (v) => "#" + [(v >> 11 & 31) * 255 / 31, (v >> 5 & 63) * 255 / 63, (v & 31) * 255 / 31].map((x) => Math.round(x).toString(16).padStart(2, "0")).join("");

/* the port went away (the FM-1 restarted): the bank is sent again from BEGIN */
export class Gone extends Error {}

export class FM1Link {
  constructor(midi) { this.midi = midi; this.out = null; this.waiting = null; this.helloWait = null; this.onRetry = null; this.dev = null; }

  /* Web MIDI with SysEx (Chrome, Edge), then the port CHOPPAH answers INFO on; null when there is none */
  static async open(midi = null) {
    if (!midi) {
      if (typeof navigator === "undefined" || !navigator.requestMIDIAccess) throw new Error("no Web MIDI here: open this page in Chrome or Edge");
      try { midi = await navigator.requestMIDIAccess({ sysex: true }); }
      catch (e) { throw new Error("MIDI with SysEx wasn't allowed: allow it for this site and try again"); }
    }
    const l = new FM1Link(midi);
    return (await l.find()) ? l : null;
  }

  listen() { this.midi.inputs.forEach((i) => { i.onmidimessage = (e) => this.onMsg(e); }); }
  async find() {
    this.listen();
    const outs = [...this.midi.outputs.values()].filter((o) => o.state !== "disconnected");
    const likely = outs.filter((o) => /choppah|fm-?1/i.test(o.name));
    for (const o of likely.concat(outs.filter((o) => !likely.includes(o)))) {
      try { await this.ask(1, [], 700, o); this.out = o; return true; } catch (e) { /* not CHOPPAH */ }
    }
    return false;
  }
  close() { this.midi.inputs.forEach((i) => { i.onmidimessage = null; }); this.out = null; }

  onMsg(e) {
    const d = e.data;
    if (d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x53 && d[3] === 0x52 && d[4] === 1 && this.helloWait) {
      const w = this.helloWait; this.helloWait = null; w(str(d.subarray(6, d.length - 1)));
      return;
    }
    if (d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x43 || d[3] !== 0x48) return;
    const w = this.waiting;
    if (w && (d[4] & 0x3F) === w.cmd) {
      const r = { code: d[5], data: unpack7(d.subarray(6, d.length - 1)) };
      if (w.ok && !r.code && !w.ok(r)) return;   /* a late answer to an earlier try: not this one's */
      this.waiting = null; w.res(r);
    }
  }
  ask(cmd, data = [], ms = 3000, port = this.out, ok = null) {
    return new Promise((res, rej) => {
      const w = { cmd, res, ok };
      this.waiting = w;
      try { port.send([0xF0, 0x7D, 0x43, 0x48, cmd, ...pack7(Uint8Array.from(data)), 0xF7]); }
      catch (e) { this.waiting = null; rej(new Error("no answer")); return; }
      setTimeout(() => { if (this.waiting === w) { this.waiting = null; rej(new Error("no answer")); } }, ms);
    });
  }
  /* a command that must get through: sent again when its answer is lost, up to TRIES times (all are safe twice) */
  async must(cmd, data = [], ms = 3000, ok = null) {
    for (let t = 1; ; t++) {
      try { return await this.ask(cmd, data, t > 1 ? Math.max(ms, 3000) : ms, this.out, ok); }
      catch (e) {
        if (!this.out || this.out.state === "disconnected" || !this.midi.outputs.get(this.out.id)) { await this.refind(); throw new Gone("the FM-1 restarted"); }
        if (t >= TRIES) throw new Error(`no answer from the FM-1 after ${TRIES} tries (is it still on USB, and stopped?)`);
        if (this.onRetry) this.onRetry(t);
        await sleep(100 * t);
      }
    }
  }
  async refind(wait = 20000) {            /* CHOPPAH's port again, after it went away: up to 20 s */
    const t0 = Date.now();
    while (Date.now() - t0 < wait) {
      this.listen();
      for (const o of this.midi.outputs.values()) {
        if (o.state === "disconnected") continue;
        try { await this.ask(1, [], 700, o); this.out = o; return; } catch (e) { /* not yet */ }
      }
      await sleep(500);
    }
    this.out = null;
    throw new Error("the FM-1 went away and didn't come back: plug it in and try again");
  }

  /* the firmware's name and version, as the screen reader's HELLO says it ("CHOPPAH 1.7.0"), or "" */
  hello(ms = 600) {
    return new Promise((res) => {
      this.helloWait = res;
      try { this.out.send([0xF0, 0x7D, 0x53, 0x52, 0x01, 0x01, 0xF7]); } catch (e) { this.helloWait = null; res(""); return; }
      setTimeout(() => { if (this.helloWait === res) { this.helloWait = null; res(""); } }, ms);
    }).then((n) => { try { this.out.send([0xF0, 0x7D, 0x53, 0x52, 0x02, 0xF7]); } catch (e) { /* gone */ } return n; });
  }

  /* INFO: {proto (1: 1.4-1.5, 2: 1.6, 3: 1.7 on), playing, caps, sizes, counts} */
  async info() {
    const d = (await this.must(1)).data;
    const dev = d[0] >= 2 && d.length >= 24
      ? { proto: d[0], playing: !!d[1], caps: [word(d, 4), word(d, 16)], sizes: [word(d, 8), word(d, 20)], counts: [d[13], d[14]] }
      : { proto: 1, playing: !!d[1], caps: [word(d, 4)], sizes: [word(d, 8)], counts: [d[2] ? 1 : 0] };
    this.dev = dev;
    return dev;
  }
  async read(k, off, size) {              /* bank k's bytes [off, off + size) */
    const img = new Uint8Array(size);
    for (let at = 0; at < size; at += 256) {
      const n = Math.min(256, size - at), o = off + at;
      const r = await this.must(5 | k << 4, [...u32(o), ...u32(n)], 3000, (a) => a.data.length >= 4 && word(a.data, 0) === o);
      if (r.code) throw new Error("read " + r.code);
      img.set(r.data.subarray(4, 4 + n), at);
    }
    return img;
  }

  /* --- MATHCORE live (INFO version 4) --- */
  get live() { return !!this.dev && this.dev.proto >= 4; }
  async getSet(off, size) {               /* GETSET: size bytes from off (the scenes, the chain, the LOOP settings) */
    const out = new Uint8Array(size);
    for (let at = 0; at < size; at += 256) {
      const n = Math.min(256, size - at), o = off + at;
      const r = await this.must(7, [...u32(o), ...u32(n)], 3000, (a) => a.data.length >= 4 && word(a.data, 0) === o);
      if (r.code) throw new Error("GETSET " + r.code);
      out.set(r.data.subarray(4, 4 + n), at);
    }
    return out;
  }
  /* the scenes (SCNE, 1604 bytes) and the chain (CHAN, 80: its reserved bytes, where the pack's tag is kept, cleared) */
  async mathcore() {
    const scenes = await this.getSet(0, 1604), chain = await this.getSet(1608, 80);
    chain[1] = chain[2] = chain[3] = 0; chain[79] = 0;
    return { scenes, chain };
  }
  /* PUTSET: bytes at off (448 a frame), on the FM-1 at once */
  async putSet(off, bytes) {
    if (!this.live) throw new Error("this CHOPPAH can't take scenes live: update it (CHOPPAH 1.7, INFO version 4)");
    for (let at = 0; at < bytes.length; at += CHUNK) {
      const o = off + at, r = await this.must(8, [...u32(o), ...bytes.subarray(at, at + CHUNK)], 1000, (a) => a.data.length >= 4 && word(a.data, 0) === o);
      if (r.code) throw new Error(r.code === 2 ? "PUTSET: out of the scenes and the chain" : "PUTSET " + r.code);
    }
  }
  putScene(i, bytes100) { return this.putSet(4 + 100 * i, bytes100); }   /* one scene (writeScenes' 100 bytes) */
  async putChain(chan80) {                /* the entries and keys first, then n (CHAN's layout; its reserved bytes left) */
    await this.putSet(1612, chan80.subarray(4, 79));
    await this.putSet(1608, chan80.subarray(0, 1));
  }
  /* what MATHCORE plays: {playing, on (0, 1 the chain or launched, 2 a scene round), scene (-1), next, edit, lane,
     part, step, pos[4], hits[4], missing, qpos, n, bar16, macros[4], state[4], base, bar} */
  async mcStatus(op = null, args = []) {
    const r = op === null ? await this.must(9) : await this.must(10, [op, ...args]);
    if (r.code) throw new Error("STATUS " + r.code);
    const v = r.data, i8 = (x) => (x > 127 ? x - 256 : x);
    return { playing: !!v[1], on: v[2], scene: i8(v[3]), next: i8(v[4]), edit: v[5], lane: v[6], part: v[7] === 255 ? -1 : v[7],
      step: v[8] | v[9] << 8, pos: [...v.subarray(10, 14)], hits: [...v.subarray(14, 18)], missing: v[18], qpos: v[19], n: v[20],
      macros: [...v.subarray(21, 25)], bar16: i8(v[25]), state: [...v.subarray(26, 30)].map(i8), base: v[30], bar: v[31] };
  }
  mc(op, ...args) { return this.mcStatus(op, args); }   /* MCCTL: 0 stop, 1 scene s, 2 the chain, 3 launch s, 4 macros d m s c, 5 edit s l, 6 lane l state */
  stop() { return this.mc(0); }
  playScene(s) { return this.mc(1, s); }
  playChain() { return this.mc(2); }
  launch(s) { return this.mc(3, s); }
  macros(d, m, sp, c) { return this.mc(4, d, m, sp, c); }
  edit(s, l = 0) { return this.mc(5, s, l); }
  laneState(l, st) { return this.mc(6, l, st < 0 ? 0xFF : st ? 1 : 0); }

  /* what is on the FM-1: {name (its firmware), proto, playing, caps, breaks: [{name, bpm, bank}], pack: null or
     {id, name, author, created, updated, theme}} (a pack: a v2 bank 1, CHOPPAH 1.7 on) */
  async status() {
    const name = await this.hello();
    const dev = await this.info();
    const breaks = [];
    let pack = null;
    for (let k = 0; k < dev.caps.length; k++) {
      const n = dev.counts[k];
      if (!n) continue;
      const head = await this.read(k, 0, Math.min(dev.sizes[k], 16 + SLOT * n + (k === 0 && dev.proto >= 3 ? 4 + 8 + 56 + 8 + 40 * 16 + 8 + 32 : 0)   /* PACK, SSET (every break's), THEM */));
      const hv = new DataView(head.buffer);
      for (let i = 0; i < n; i++) {
        const s = 16 + SLOT * i;
        breaks.push({ name: str(head.subarray(s, s + 12)), bpm: hv.getUint32(s + 12, true) / 100, bank: k });
      }
      if (k === 0 && hv.getUint16(4, true) === 2) pack = packOf(head, n);
    }
    return { name: name || (dev.proto >= 2 ? "CHOPPAH" : "CHOPPAH 1.5 or older"), ...dev, breaks, pack };
  }

  /* bank k written whole (img) or emptied (null). The FM-1 checks the bank's CRC-32 at COMMIT; when it doesn't
     check, or the FM-1 restarted on the way, the bank is sent again from BEGIN, up to three times.
     shown(bytes sent, try) follows it; at {total, before, name}: where the bank is in the pack, for the FM-1's screen */
  async sendBank(k, img, shown = () => {}, at = null) {
    const b = k << 4, fail = (r) => { throw new Error(ERR[r.code] || "code " + r.code); };
    for (let again = 0; ; again++) {
      try {
        if (!img) { const r = await this.must(6 | b, [], 8000); if (r.code) fail(r); return; }
        shown(0, again);
        let r = await this.must(2 | b, beginOf(img, at), 20000);   /* the bank erased first: a moment */
        if (r.code) fail(r);
        for (let off = 16; off < img.length; off += CHUNK) {
          r = await this.must(3 | b, [...u32(off), ...img.subarray(off, off + CHUNK)], 1000,
            (a) => a.data.length < 4 || word(a.data, 0) === off);
          if (r.code === 2 && again < 2) throw new Gone("out of place");
          if (r.code) fail(r);
          shown(off, again);
        }
        r = await this.must(4 | b, img.subarray(0, 16), 8000);   /* the header last: the bank counts once it checks */
        if ((r.code === 4 || r.code === 3) && again < 2) throw new Gone("doesn't check");
        if (r.code) fail(r);
        return;
      } catch (e) {
        if (!(e instanceof Gone) || again >= 2) throw e;
        await sleep(300);
      }
    }
  }

  /* a pack's images (chpk.js installImages) onto the FM-1: each bank written or, when the pack doesn't need it,
     emptied; then INFO again to see it took. progress(fraction, words) */
  async install(imgs, progress = () => {}, name = null) {
    const dev = this.dev || await this.info();
    if (dev.playing) throw new Error(ERR[1]);
    const total = imgs.reduce((a, m) => a + (m ? m.length : 0), 0) || 1;
    name = name || packName(imgs.find((m) => m));
    let sent = 0, note = "";
    this.onRetry = (t) => { note = ` · a frame lost on the way, sent again (${t})`; };
    try {
      for (let k = 0; k < dev.caps.length; k++) {
        const img = imgs[k] || null;
        if (!img) { if (dev.counts[k]) await this.sendBank(k, null); continue; }
        await this.sendBank(k, img, (off, again) => progress((sent + off) / total,
          `Writing bank ${k + 1}${again ? ` again (try ${again + 1})` : ""}… ${Math.round(100 * (sent + off) / total)}%${note}`),
          { total, before: sent, name });
        sent += img.length;
      }
    } finally { this.onRetry = null; }
    const after = await this.info();
    const want = imgs.map((m) => (m ? new DataView(m.buffer, m.byteOffset).getUint16(6, true) : 0));
    if (after.counts[0] !== want[0] || (after.counts[1] || 0) !== (want[1] || 0)) throw new Error(`the FM-1 lists ${after.counts[0] + (after.counts[1] || 0)} breaks afterwards`);
    progress(1, "Done");
    return after;
  }
}

/* BEGIN's data: the size, then (at) the pack's bytes in all, those before this bank and the name, 16 bytes (printable
   ASCII, the FM-1's font) */
export function beginOf(img, at = null) {
  const d = u32(img.length);
  if (!at) return d;
  const nm = String(at.name || "").replace(/[^\x20-\x7E]/g, "?").slice(0, 16);
  return [...d, ...u32(at.total || img.length), ...u32(at.before || 0), ...Array.from({ length: 16 }, (_, i) => (i < nm.length ? nm.charCodeAt(i) : 0))];
}

/* a bank image's name for the screen: a v2 bank's pack, else its first break's */
export function packName(img) {
  if (!img || img.length < 16 + SLOT) return "";
  const n = new DataView(img.buffer, img.byteOffset, img.byteLength).getUint16(6, true);
  const p = new DataView(img.buffer, img.byteOffset, img.byteLength).getUint16(4, true) === 2 ? packOf(img, n) : null;
  return (p && p.name) || str(img.subarray(16, 28));
}

/* a v2 bank 1's head: its PACK chunk (and THEM, when read) -> {id, name, author, created, updated, theme} or null */
export function packOf(head, n) {
  const dv = new DataView(head.buffer, head.byteOffset, head.byteLength);
  let at = 16 + SLOT * n;
  if (at + 4 > head.length) return null;
  const nch = dv.getUint32(at, true); at += 4;
  let pack = null;
  for (let i = 0; i < nch && at + 8 <= head.length; i++) {
    const tag = String.fromCharCode(...head.subarray(at, at + 4)), len = dv.getUint32(at + 4, true), d = at + 8;
    if (tag === "PACK" && d + 56 <= head.length) {
      pack = { id: uuidOf(head.subarray(d, d + 16)), name: str(head.subarray(d + 16, d + 32)), author: str(head.subarray(d + 32, d + 48)),
        created: dv.getUint32(d + 48, true), updated: dv.getUint32(d + 52, true), theme: null };
    } else if (tag === "THEM" && pack && d + 16 <= head.length) {
      const k = ["bg", "panel", "text", "dim", "hi", "mid", "wave", "warn"];
      pack.theme = Object.fromEntries(k.map((x, j) => [x, rgb(dv.getUint16(d + 2 * j, true))]));
    }
    at = d + len + (-len & 3);
  }
  return pack;
}
