/* SPDX-License-Identifier: MPL-2.0 */
/* A stand-in FM-1 behind Web MIDI for breaks.html, as ui/main.c's app_sysex answers: INFO, BEGIN, DATA (offset
   echoed, 1.6), COMMIT (the bank's CRC-32 checked), READ, DELETE, and the screen reader's HELLO. NOR flash: erase sets
   0xFF, a write ANDs. Faults from window.FAULTS: drop a frame coming in (pIn), drop an answer (pOut), answer late
   (pSlow: 2.1-3 s, past DATA's 2 s), and restart once (rebootAt: the nth DATA, the port gone 1.5 s, BEGIN forgotten).
   proto: INFO's version (2: CHOPPAH 1.6, 3: 1.7, 4: 1.7 with MATHCORE live: GETSET, PUTSET, STATUS, MCCTL into
   window.FM1.gs and .mc), name: HELLO's, playing: writes answered 1. In node too
   (tests/fm1link.test.mjs: window and navigator made first). */
(() => {
  const F = Object.assign({ pIn: 0, pOut: 0, pSlow: 0, rebootAt: 0, seed: 1, echo: true, proto: 2, name: "CHOPPAH 1.6.0", playing: false }, window.FAULTS || {});
  let s = F.seed >>> 0;
  const rnd = () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const CAP = [63 * 4096, 23 * 4096], flash = CAP.map((c) => new Uint8Array(c).fill(0xFF));
  const st = { cuSize: 0, cuBank: -1, pkN: [0, 0], frames: 0, dropIn: 0, dropOut: 0, slow: 0, reboots: 0, data: 0, dup: 0 };
  const seen = new Set();
  /* MATHCORE (proto 4): GETSET's memory (the scenes, the chain, the LOOP settings), PUTSET into it, STATUS, MCCTL */
  const gs = new Uint8Array(1604 + 728); gs.set([0x4D, 0x43, 0x53, 0x35]); gs.set([0x4D, 0x43, 0x51, 0x31], 1604);
  for (let k = 0; k < 11; k++) gs[1604 + 72 + k] = k;
  const mc = { playing: 0, on: 0, scene: 0xFF, edit: 0, lane: 0, macros: [50, 0, 50, 0], state: [0, 0, 0, 0], ops: [], t0: 0 };
  window.FM1 = { flash, st, F, gs, mc };
  const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc32 = (b) => { let c = 0xFFFFFFFF; for (const v of b) c = CRC[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
  const pack7 = (b) => { const o = []; for (let i = 0; i < b.length; i += 7) { const g = b.subarray(i, i + 7); let hi = 0; g.forEach((v, j) => { hi |= (v >> 7) << j; }); o.push(hi, ...Array.from(g, (v) => v & 0x7F)); } return o; };
  const unpack7 = (p) => { const o = []; for (let i = 0; i < p.length; i += 8) for (let j = 1; j < 8 && i + j < p.length; j++) o.push(p[i + j] | ((p[i] >> (j - 1)) & 1) << 7); return new Uint8Array(o); };
  const rd32 = (u, i) => (u[i] | u[i + 1] << 8 | u[i + 2] << 16 | u[i + 3] << 24) >>> 0;
  const w32 = (v) => [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255];
  const input = { id: "fm1-in", name: "CHOPPAH", state: "connected", type: "input", onmidimessage: null };
  const output = { id: "fm1-out", name: "CHOPPAH", state: "connected", type: "output", send };
  const inputs = new Map([[input.id, input]]), outputs = new Map([[output.id, output]]);
  let down = false;
  function answer(bytes, mayLose = true) {
    if (mayLose && rnd() < F.pOut) { st.dropOut++; return; }
    let ms = 1 + rnd() * 4;
    if (mayLose && rnd() < F.pSlow) { st.slow++; ms = 2100 + rnd() * 900; }
    const gen = st.reboots;
    setTimeout(() => { if (!down && gen === st.reboots && input.onmidimessage) input.onmidimessage({ data: Uint8Array.from(bytes), target: input }); }, ms);
  }
  const reply = (cmd, code, d = []) => answer([0xF0, 0x7D, 0x43, 0x48, 0x40 | cmd, code, ...pack7(Uint8Array.from(d)), 0xF7], cmd !== 1);
  function reboot() {
    st.reboots++; down = true; output.state = input.state = "disconnected"; outputs.delete(output.id); inputs.delete(input.id);
    st.cuSize = 0; st.cuBank = -1;
    setTimeout(() => { down = false; output.state = input.state = "connected"; outputs.set(output.id, output); inputs.set(input.id, input); }, 1500);
  }
  function check(k) {
    const f = flash[k], n = f[6] | f[7] << 8, size = rd32(f, 8);
    st.pkN[k] = rd32(f, 0) === 0x31424843 && n && n <= 16 && size <= CAP[k] - 16 && crc32(f.subarray(16, 16 + size)) === rd32(f, 12) ? n : 0;
  }
  function send(data) {
    if (down) throw new DOMException("disconnected", "InvalidStateError");
    const p = Uint8Array.from(data);
    if (p[1] === 0x7D && p[2] === 0x53 && p[3] === 0x52) {   /* the screen reader's HELLO */
      if (p[4] === 1) answer([0xF0, 0x7D, 0x53, 0x52, 0x01, 0x01, ...Array.from(F.name, (c) => c.charCodeAt(0)), 0, 0xF7], false);
      return;
    }
    if (p[1] !== 0x7D || p[2] !== 0x43 || p[3] !== 0x48) return;
    st.frames++;
    const c = p[4], cmd = c & 15, k = c >> 4 & 3, u = unpack7(p.subarray(5, p.length - 1));
    if (cmd === 3) {
      st.data++;
      if (F.rebootAt && st.data === F.rebootAt && !st.reboots) { reboot(); return; }
    }
    if (cmd !== 1 && rnd() < F.pIn) { st.dropIn++; return; }   /* (INFO is the port search: kept, so Connect is quick) */
    if (cmd === 1) {
      const v = [F.proto | (F.playing ? 1 : 0) << 8 | (st.pkN[0] + st.pkN[1]) << 16 | 63 << 24, CAP[0], st.pkN[0] ? 16 + rd32(flash[0], 8) : 0,
        2 | st.pkN[0] << 8 | st.pkN[1] << 16, CAP[1], st.pkN[1] ? 16 + rd32(flash[1], 8) : 0];
      return reply(c, 0, v.flatMap(w32));
    }
    if (cmd === 7) {
      const off = rd32(u, 0), n = rd32(u, 4);
      if (off + n > gs.length || n > 256) return reply(c, 2);
      return reply(c, 0, [...u.subarray(0, 4), ...gs.subarray(off, off + n)]);
    }
    if (cmd === 8 || cmd === 9 || cmd === 10) {
      if (F.proto < 4) return;                    /* (before INFO 4: no answer, as the firmware ignores them) */
      if (cmd === 8) {
        const off = rd32(u, 0), b = u.subarray(4);
        if (off < 4 || off + b.length > 1604 + 84) return reply(c, 2);
        b.forEach((v, i) => { const at = off + i, m = at - 1604;
          if (at < 1604) gs[at] = v; else if (m === 4) gs[at] = Math.min(64, v); else if (m >= 8 && m < 72) gs[at] = v < 16 ? v : 0; else if (m >= 72 && m < 83) gs[at] = v < 16 ? v : 0xFF; });
        return reply(c, 0, [...u.subarray(0, 4)]);
      }
      if (cmd === 10) {
        const [op, d0, d1, d2, d3] = [u[0], u[1] | 0, u[2] | 0, u[3] | 0, u[4] | 0];
        mc.ops.push([op, d0, d1, d2, d3]);
        if (op === 0) { mc.playing = 0; mc.on = 0; mc.scene = 0xFF; }
        else if (op === 1) { mc.playing = 1; mc.on = 2; mc.scene = d0 % 16; mc.edit = d0 % 16; mc.t0 = Date.now(); }
        else if (op === 2) { mc.playing = 1; mc.on = 1; mc.scene = gs[1608] ? gs[1612] : 0; mc.t0 = Date.now(); }
        else if (op === 3) { mc.playing = 1; mc.on = 1; mc.scene = d0 % 16; mc.t0 = Date.now(); }
        else if (op === 4) mc.macros = [d0, d1, d2, d3].map((x) => Math.min(100, x));
        else if (op === 5) { mc.edit = d0 % 16; mc.lane = d1 % 4; }
        else if (op === 6) mc.state[d0 % 4] = d1 === 0xFF ? -1 : d1 ? 1 : 0;
      }
      const v = new Uint8Array(32), step = mc.playing ? Math.floor((Date.now() - mc.t0) / 111) : 0;
      v[0] = 1; v[1] = mc.playing; v[2] = mc.on; v[3] = mc.scene; v[4] = mc.scene; v[5] = mc.edit; v[6] = mc.lane; v[7] = 0xFF;
      v[8] = step & 255; v[9] = step >> 8; for (let l = 0; l < 4; l++) { v[10 + l] = step % 16; v[21 + l] = mc.macros[l]; v[26 + l] = mc.state[l] & 255; }
      v[20] = gs[1608]; v[25] = step % 16;
      return reply(c, 0, [...v]);
    }
    if (cmd === 5) {
      const off = rd32(u, 0), n = rd32(u, 4);
      return reply(c, 0, [...u.subarray(0, 4), ...flash[k].subarray(off, off + n)]);
    }
    if (F.playing && (cmd === 2 || cmd === 3 || cmd === 4 || cmd === 6)) return reply(c, 1);   /* writes only while stopped */
    if (cmd === 2 || cmd === 6) {
      st.cuSize = cmd === 2 ? rd32(u, 0) : 1; st.cuBank = k;
      for (let i = 0; i < Math.ceil(st.cuSize / 4096); i++) flash[k].fill(0xFF, i * 4096, (i + 1) * 4096);
      if (cmd === 6) st.cuSize = 0;
      check(k);
      return reply(c, 0);
    }
    if (cmd === 3) {
      const off = rd32(u, 0), b = u.subarray(4);
      const key = k + ":" + off; if (seen.has(key)) st.dup++; seen.add(key);
      let code = 0;
      if (k !== st.cuBank || off < 16 || off > st.cuSize || b.length > st.cuSize - off) code = 2;
      else for (let i = 0; i < b.length; i++) flash[k][off + i] &= b[i];
      return reply(c, code, F.echo ? u.subarray(0, 4) : []);
    }
    if (cmd === 4) {
      let code = 0;
      if (k !== st.cuBank || u.length < 16) code = 3;
      else { for (let i = 0; i < 16; i++) flash[k][i] &= u[i]; check(k); if (!st.pkN[k]) code = 4; }
      return reply(c, code);
    }
  }
  const access = { inputs, outputs, sysexEnabled: true, onstatechange: null };
  navigator.requestMIDIAccess = async () => access;
})();
