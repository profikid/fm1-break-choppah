// The pack builder in Chrome (tests/page_pack.sh): a track in, its breaks found and cut, an overlay, the pack's
// details, its file (.chpk) out and back in. No errors on the page.
import puppeteer from "puppeteer-core";
import { readFileSync, writeFileSync } from "node:fs";

const [url, wavPath] = process.argv.slice(2);
const browser = await puppeteer.launch({ executablePath: process.env.CHROME || "/usr/bin/google-chrome", headless: "new",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required"] });
const page = await browser.newPage();
const errors = [];
page.on("pageerror", (e) => errors.push(String(e)));
page.on("console", (m) => { if (m.type() === "error" && !/Web MIDI|favicon|fonts\.g|Failed to load resource/.test(m.text())) errors.push(m.text()); });
/* the site build adds the library (lib/) and the fonts; served raw, only those may be missing */
page.on("response", (r) => { if (r.status() >= 400 && !/\/lib\/|favicon|fonts\.g/.test(r.url())) errors.push(`${r.status()} ${r.url()}`); });
/* the destemmer (choppah.app's /api/destem, Demucs on a GPU): a stand-in in the page that answers with the drums
   as the harmonic / percussive measure finds them, so the page's split runs as on the hub */
await page.evaluateOnNewDocument(() => {
  const real = window.fetch; window.__destems = [];
  window.fetch = async (url, init = {}) => {
    if (!String(url).endsWith("/api/destem")) return real(url, init);
    const B = await import(new URL("beats.js", location.href).href);
    const w = B.monoWav(init.body), { drums } = B.stems(w.x, w.rate);
    window.__destems.push({ rate: w.rate, n: w.x.length, hub: init.headers && init.headers["x-hub"] });
    return new Response(B.floatWav(drums, w.rate), { status: 200, headers: { "content-type": "audio/wav" } });
  };
});
const out = { ok: false };
try {
  await page.goto(url, { waitUntil: "load" });
  await page.waitForFunction(() => window.CHPK && window.BEATS && typeof meta !== "undefined" && meta, { timeout: 20000 });
  const input = await page.$("#file");
  await input.uploadFile(wavPath);
  await page.waitForFunction(() => !document.getElementById("track").classList.contains("hidden"), { timeout: 30000 });
  await page.click("#trackGo");
  await page.waitForFunction(() => !document.getElementById("trackOut").classList.contains("hidden") || /didn't work/.test(document.getElementById("trackState").textContent), { timeout: 240000 });
  out.state = await page.$eval("#trackState", (e) => e.textContent);
  out.bpm = +(await page.$eval("#trackBpm", (e) => e.value));
  out.loops = await page.$$eval("#trackLoops li", (l) => l.length);
  await page.click('#trackLoops li:first-child button[data-a="add"]');
  await page.waitForFunction(() => pack.length === 1, { timeout: 30000 });
  /* the overlay from the rest (the stems): a loop's Split, then the rest alone into the pack (PH-049) */
  await page.click('#trackLoops li:first-child button[data-a="split"]');
  await page.waitForFunction(() => !document.getElementById("splitBox").classList.contains("hidden"), { timeout: 10000 });
  await page.click("#spRestOnly");
  await page.waitForFunction(() => meta.overlay || /no room|ends before|fewer/.test(document.getElementById("spState").textContent), { timeout: 30000 });
  out.overlay = await page.evaluate(() => meta.overlay && { bars: meta.overlay.bars, rate: meta.overlay.rate, len: meta.overlay.len });
  await page.$eval("#pkName", (e) => { e.value = "Test Pack"; e.dispatchEvent(new Event("change")); });
  await page.$eval("#slLoop", (e) => e.value);       /* the loop settings exist */
  const f = await page.evaluate(async () => {
    pack[0].slot = { ...(pack[0].slot || {}), loop_steps: 16 };
    const bytes = await packFile(), p = CHPK.parsePack(bytes);
    const back = Array.from(bytes);
    pack = []; takePack(new Uint8Array(back));
    return { name: p.meta.name, n: p.slots.length, ov: !!p.overlay, ovBars: p.overlay && p.overlay.bars, loop: p.meta.slots[0].loop_steps,
      thumb: p.thumbType, size: bytes.length, again: pack.length, againOv: !!meta.overlay, againLoop: pack[0].slot.loop_steps };
  });
  out.file = f;
  const destems = out.destems = await page.evaluate(() => window.__destems);
  out.ok = destems.length === 1 && destems[0].rate === 22050 && destems[0].hub === "1" && Math.abs(out.bpm - 99.81) < 0.6 && out.loops > 0 && !!out.overlay && f.name === "TEST PACK" && f.n === 1 && f.ov && f.loop === 16
    && f.again === 1 && f.againOv && f.againLoop === 16;
} catch (e) { out.error = String(e); }
out.errors = errors;
if (errors.length) out.ok = false;
console.log(JSON.stringify(out));
await browser.close();
process.exit(out.ok ? 0 : 1);
