// SPDX-License-Identifier: MPL-2.0
// tests/emu_install.sh: the emulator (app/build/emu) in Chrome, a pack installed into it from another tab of the
// same site through the emulator's port part (fm1-base web/emu/parts/port.js, a BroadcastChannel), as choppah.app's
// hub does; read back byte for byte, and still there after the emulator's page reloads (the data part).
import puppeteer from "puppeteer-core";
const [base, chrome] = process.argv.slice(2);
const b = await puppeteer.launch({ executablePath: chrome, headless: "new", protocolTimeout: 170000,
  args: ["--autoplay-policy=no-user-gesture-required", "--no-sandbox"] });
const out = {};
try {
  const emu = await b.newPage();
  emu.on("pageerror", (e) => console.error("emu:", e.message));
  await emu.goto(`${base}/emu/`, { waitUntil: "load" });
  const on = async () => { await emu.bringToFront(); await emu.click("#go"); await emu.waitForFunction(() => document.getElementById("start").hidden, { timeout: 30000 }); };
  await on();
  const pg = await b.newPage();
  pg.on("pageerror", (e) => console.error("page:", e.message));
  await pg.goto(`${base}/tests/emu_install/page.html`, { waitUntil: "load" });
  await pg.waitForFunction(() => window.ready);
  out.install = await pg.evaluate(() => window.install());
  await new Promise((r) => setTimeout(r, 3000));       /* the data part keeps the area every 2 s */
  await emu.reload({ waitUntil: "load" });
  await on();
  await pg.bringToFront();
  out.reloaded = await pg.evaluate(() => window.check());
} catch (e) { out.error = String(e && e.message || e); }
await b.close();
console.log(JSON.stringify(out));
