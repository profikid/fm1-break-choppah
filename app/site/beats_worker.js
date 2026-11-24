/* SPDX-License-Identifier: MPL-2.0 */
/* the pack builder's heavy listening, off the page's thread: a track in, its stems, tempo, bars and loops out */
import { resample, stems, analyse, findLoops } from "./beats.js";

const RATE = 22050;
self.onmessage = (e) => {
  const { id, x, rate, split } = e.data;
  try {
    const y = resample(x, rate, RATE);
    let drums = null, rest = null;
    if (split) ({ drums, rest } = stems(y, RATE, { onProgress: (p) => self.postMessage({ id, progress: p * 0.85 }) }));
    self.postMessage({ id, progress: 0.9 });
    const a = analyse(drums || y, RATE);
    const loops = findLoops(y, RATE, a, { drums, n: 10 });
    const out = { id, done: true, rate: RATE, mix: y, drums, rest, bpm: a.bpm, bars: a.bars, beats: a.beats, confidence: a.confidence, loops };
    self.postMessage(out, [y.buffer, ...(drums ? [drums.buffer, rest.buffer] : [])]);
  } catch (err) { self.postMessage({ id, error: String(err && err.message || err) }); }
};
