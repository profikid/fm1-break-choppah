/* SPDX-License-Identifier: MPL-2.0 */
/* the pack builder's heavy listening, off the page's thread: a track in (and its stems, split by Demucs on the hub's
   GPU before: beats.js splitServer), its tempo, bars and loops out */
import { resample, analyse, findLoops } from "./beats.js";

const RATE = 22050;
self.onmessage = (e) => {
  const { id, x, rate, drums = null, rest = null } = e.data;
  try {
    const y = rate === RATE ? x : resample(x, rate, RATE);
    if (drums && (rate !== RATE || drums.length !== y.length)) throw new Error("the stems must come at 22.05 kHz, as long as the track");
    self.postMessage({ id, progress: 0.9 });
    const a = analyse(drums || y, RATE);
    const loops = findLoops(y, RATE, a, { drums, n: 10 });
    const out = { id, done: true, rate: RATE, mix: y, drums, rest, bpm: a.bpm, bars: a.bars, beats: a.beats, confidence: a.confidence, loops };
    self.postMessage(out, [y.buffer, ...(drums ? [drums.buffer, rest.buffer] : [])]);
  } catch (err) { self.postMessage({ id, error: String(err && err.message || err) }); }
};
