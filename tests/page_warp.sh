#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The break manager's warp (app/site/breaks.html): run in node. A loop whose 15 hits sit up to 1500 samples off the
# grid, warped: each hit's attack (8 ms) lands on its grid line sample for sample, the loop keeps its length, the
# sound between is stretched (its level kept, nothing silent), and markers already on the grid change nothing.
#   tests/page_warp.sh
set -e
cd "$(dirname "$0")/.."
node - <<'N'
const fs = require("fs"), page = fs.readFileSync("app/site/breaks.html", "utf8");
const pure = page.split("the break, as tools/prep_breaks.py makes it */")[1].split("/* ----------------------------------------------------------------")[0];
const w = page.split("/* ---------------------------------------------------------------- warp: the hits onto the grid */")[1].split("/* ---------------------------------------------------------------- the slices by hand */")[0];
const { warp } = new Function(pure + w + "; return { warp };")();
const L = 100000, n = 16, rate = 22050, A = Math.round(rate * 0.008), x = new Float32Array(L), marks = [0];
let seed = 7; const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
for (let i = 1; i < n; i++) marks.push(Math.round(i * L / n + (rnd() * 2 - 1) * 1500));
marks.push(L);
for (let i = 0; i < n; i++) for (let j = 0; marks[i] + j < marks[i + 1]; j++) x[marks[i] + j] = (rnd() * 2 - 1) * (0.2 + 0.8 * Math.exp(-j / 600));
const t0 = Date.now(), y = warp(x, marks, rate), ms = Date.now() - t0;
let off = 0, quiet = 0;
for (let i = 0; i < n; i++) {
  const g = Math.round(i * L / n), g1 = Math.round((i + 1) * L / n);
  for (let j = 0; j < A; j++) if (y[g + j] !== x[marks[i] + j]) off++;
  let e = 0; for (let k = g + A; k < g1 - 200; k++) e += y[k] * y[k];
  if (Math.sqrt(e / (g1 - 200 - g - A)) < 0.1) quiet++;
}
const same = warp(x, Array.from({ length: n + 1 }, (_, i) => Math.round(i * L / n)), rate);
let diff = 0; for (let i = 0; i < L; i++) diff = Math.max(diff, Math.abs(same[i] - x[i]));
if (off || quiet || y.length !== L || diff) { console.error({ off, quiet, len: y.length, diff }); process.exit(1); }
console.log(`page_warp: 15 hits up to 1500 samples off land on the grid sample for sample, the loop's length kept, nothing silent between, on-grid markers change nothing (${ms} ms)`);
N
