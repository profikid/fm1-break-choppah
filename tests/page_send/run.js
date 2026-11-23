/* SPDX-License-Identifier: MPL-2.0
   breaks.html's Send against fake_fm1.js (tests/page_send.sh): node run.js URL FAULTS_JSON PACK.chb -> one JSON line (sent?, the status, the fake's counts,
   whether each bank on the "flash" is the page's image byte for byte) */
const puppeteer = require("puppeteer-core");
const fs = require("fs");
(async () => {
  const [url, faults, chb] = process.argv.slice(2);
  const chrome = process.env.CHROME || ["/usr/bin/google-chrome", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"].find((p) => fs.existsSync(p));
  const b = await puppeteer.launch({ executablePath: chrome, protocolTimeout: 1200000, args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required"] });
  const pg = await b.newPage();
  await pg.evaluateOnNewDocument(`window.FAULTS = ${faults};\n` + fs.readFileSync(__dirname + "/fake_fm1.js", "utf8"));
  await pg.goto(url, { waitUntil: "load" });
  const inp = await pg.$("#importFile");
  await inp.uploadFile(chb);
  await pg.waitForFunction(() => pack.length === 7, { timeout: 60000 });
  await pg.click("#connect");
  await pg.waitForFunction(() => /connected/.test(document.getElementById("devStatus").textContent), { timeout: 30000 });
  await pg.waitForFunction(() => !document.getElementById("send").disabled, { timeout: 30000 });
  const t0 = Date.now();
  const seen = new Set();
  const poll = setInterval(async () => { try { const t = await pg.$eval("#sendStatus", (e) => e.textContent); if (/again|lost/.test(t)) seen.add(t.replace(/\d+%/, "")); } catch (e) {} }, 400);
  await pg.click("#send");
  await pg.waitForFunction(() => /on the FM-1|Not sent/.test(document.getElementById("sendStatus").textContent), { timeout: 900000, polling: 200 });
  clearInterval(poll);
  const res = await pg.evaluate(async () => {
    const L = layout(), bs = await Promise.all(pack.slice(0, L.k + L.m).map((e) => atQuality(e, quality)));
    const imgs = [buildBank(bs.slice(0, L.k)), L.m ? noAt(buildBank(bs.slice(L.k))) : null];
    const same = imgs.map((img, k) => !img || img.every((v, i) => FM1.flash[k][i] === v));
    return { status: document.getElementById("sendStatus").textContent, layout: [L.k, L.m], same, st: FM1.st };
  });
  res.secs = Math.round((Date.now() - t0) / 100) / 10;
  res.notes = [...seen].slice(0, 4);
  console.log(JSON.stringify(res));
  await b.close();
})().catch((e) => { console.log(JSON.stringify({ error: String(e) })); process.exit(1); });
