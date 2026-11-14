#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""CHOPPAH's look for the site fm1-base's tools/make_site.py builds (run it after make_site.py):

  tools/site_style.py SITE_DIR

- install/index.html (fm1-base's installer): site.css and app/site/install.css in place of its style, and the
  site's header above it; its markup and script stay as they are;
- emu/index.html (fm1-base's emulator): site.css and app/site/emu.css in place of its style (its Barlow stays for the
  panel's printed labels), the site's
  header and an introduction in place of its title, and a "where to start" row under the panel; its panel, markup
  and script stay as they are;
- breaks/index.html: the break manager (app/site/breaks.html: a break of your own over Web MIDI, PRD A-60) with
  site.css and the site's header;
- index.html and manual/index.html: app/site/site.css (with its fonts) in place of fm1-base's style, a class on
  <body> for each page, and a "Controls" link in the site's header (to the manual's control map);
- manual/index.html: the control map (app/site/controlmap.html: explorer, buttons, the whole map, who owns the
  knobs) after the heading of the section "The control map", in place of that section's text (the Markdown's text
  is for readers on GitHub); and the contents follow the reading (the section on screen marked).

The pages keep everything else make_site.py gave them: their text, links, images, tables and the contents."""
import re
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1] / "app"
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700'
         '&family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap">\n')
SPY = """<script>
(function () {                         /* the contents follow the reading: the section on screen marked */
  var links = {}, nav = document.querySelector("nav.contents");
  if (!nav || !("IntersectionObserver" in window)) return;
  nav.querySelectorAll("a[href^='#']").forEach(function (a) { links[a.getAttribute("href").slice(1)] = a; });
  var seen = new Set();
  var io = new IntersectionObserver(function (es) {
    es.forEach(function (e) { if (e.isIntersecting) seen.add(e.target.id); else seen.delete(e.target.id); });
    var first = Object.keys(links).find(function (id) { return seen.has(id); });
    if (!first) return;
    Object.keys(links).forEach(function (id) {
      if (id === first) links[id].setAttribute("aria-current", "true"); else links[id].removeAttribute("aria-current");
    });
  }, { rootMargin: "-80px 0px -55% 0px" });
  Object.keys(links).forEach(function (id) { var h = document.getElementById(id); if (h) io.observe(h); });
})();
</script>
"""


def restyle(path, cls, controls_href, extra=""):
    s = path.read_text(encoding="utf-8")
    css = (APP / "site" / "site.css").read_text(encoding="utf-8") + extra
    s, n = re.subn(r"<style>.*?</style>", lambda m: FONTS + "<style>" + css + "</style>", s, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: no <style> to replace")
    s = s.replace("<body>", f'<body class="{cls}">', 1)
    nav = re.search(r'(<header class="site">.*?<nav>)(.*?)(</nav>)', s, re.S)
    if nav:                                          # "Controls" after "Manual"
        breaks = "breaks/" if controls_href.startswith("manual/") else "../breaks/"   # (the front page, the manual)
        links = re.sub(r'(<a href="[^"]*manual/"[^>]*>Manual</a>)',
                       r'\1<a href="' + controls_href + '">Controls</a><a href="' + breaks + '">Your break</a>',
                       nav.group(2), count=1)
        s = s[:nav.start(2)] + links + s[nav.end(2):]
    return s


def header(current):
    conf = (APP / "fm1.toml").read_text(encoding="utf-8")
    name = re.search(r'^name\s*=\s*"([^"]+)"', conf, re.M)
    repo = re.search(r'^repo\s*=\s*"([^"]+)"', conf, re.M)
    links = [("../emu/", "Emulator"), ("../install/", "Install"), ("../manual/", "Manual"), ("../manual/#controls", "Controls"),
             ("../breaks/", "Your break")]
    if repo:
        links.append((repo.group(1), "Source"))
    return ('<div class="page"><header class="site"><a class="home" href="../">' + (name.group(1) if name else "CHOPPAH") +
            "</a><nav>" + "".join(f'<a href="{h}"' + (' aria-current="page"' if t == current else "") + f">{t}</a>"
                                  for h, t in links) + "</nav></header></div>\n")


EMU_START = """<div class="start-here" aria-label="Where to start">
  <div><b>Switch on, then PLAY</b><p>The break plays at 170 BPM. The white keys are its sixteen slices: press one and it lands on the next snap point; hold it and it loops.</p></div>
  <div><b>Throw the pads</b><p>The black keys are momentary effects: rolls, reverse, freeze, half speed, tape stop, gate, random, filter, dub. Hold one and turn K1–K4 to tune it.</p></div>
  <div><b>Find your way</b><p>Each button opens a page; press it again for its next part. <span class="k">SEL</span> held a second lists every combination. All of it is in the <a href="../manual/#controls">control map</a>.</p></div>
</div>
"""


def emu(site):
    """the emulator: CHOPPAH's style and fonts, the site's header, an introduction, a row of where to start"""
    page = site / "emu" / "index.html"
    if not page.exists():
        return
    s = restyle(page, "emu", "", (APP / "site" / "emu.css").read_text(encoding="utf-8"))
    m = re.search(r"<header>\s*<h1>([^<]*)</h1>\s*</header>\s*<main>", s)
    if not m:
        raise SystemExit(f"{page}: no <header><h1> then <main> to replace")
    s = (s[:m.start()] + header("Emulator") + '<main>\n<div class="intro"><h1>' + m.group(1) + " in the browser</h1><p>The "
         "same code the FM-1 runs, with sound. Play it with the mouse, a touch screen or the keyboard.</p></div>" + s[m.end():])
    s = s.replace('<details class="speak" id="speak">', EMU_START + '<details class="speak" id="speak">', 1)
    page.write_text(s, encoding="utf-8")


def install(site):
    """the installer: CHOPPAH's style, and the site's header (the same pages as the manual's) above its main"""
    page = site / "install" / "index.html"
    if not page.exists():
        return
    s = restyle(page, "install", "", (APP / "site" / "install.css").read_text(encoding="utf-8"))
    s = s.replace('<body class="install">', '<body class="install">\n' + header("Install"), 1)
    page.write_text(s, encoding="utf-8")


def library(out):
    """the break manager's library: Cyberworm's Amens in breaks/ (cw/, huge/; the classics are left out: the firmware
    has them; breaks/breaks.txt: the tempo, the bars it holds, the name) as 16-bit mono WAV at its own rate in
    breaks/lib/, and lib/index.json listing them. The page makes a break of one as it would of a file (no loss: it
    ends at 22.05 kHz)"""
    import array
    import json
    import struct
    import sys
    sys.path.insert(0, str(Path(__file__).parent))
    import prep_breaks
    root = APP.parent / "breaks"
    records = {}
    for m in re.finditer(r'^\| ([A-Z][A-Z0-9 -]*) \| ([^|]+?) \| [0-9.]+ \|', (root / "README.md").read_text(encoding="utf-8"), re.M):
        records[m.group(1).strip()] = m.group(2).strip()
    items = []
    for line in (root / "breaks.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^([*-])\s+(\S+)\s+([0-9.]+)\s+(\d+)\s+(\d+)\s+(.+?)\s*$", line)
        if not m:
            continue
        mark, rel, bpm, _, of, name = m.groups()
        if rel.startswith("classics/"):
            continue
        x, rate = prep_breaks.read_wav(root / rel)
        pcm = array.array("h", (max(-32768, min(32767, int(round(v * 32767)))) for v in x))
        dst = out / "lib" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(b"RIFF" + struct.pack("<I", 36 + 2 * len(pcm)) + b"WAVEfmt " +
                        struct.pack("<IHHIIHH", 16, 1, 1, rate, 2 * rate, 2, 16) + b"data" +
                        struct.pack("<I", 2 * len(pcm)) + pcm.tobytes())
        items.append({"file": "lib/" + rel, "name": name, "bpm": float(bpm), "bars": int(of),
                      "group": "classics" if rel.startswith("classics/") else "amens",
                      "builtin": mark == "*", "record": records.get(name, "") if rel.startswith("classics/") else ""})
    (out / "lib" / "index.json").write_text(json.dumps(items, indent=0), encoding="utf-8")
    return len(items)


def breaks(site):
    """the break manager (app/site/breaks.html): the site's style and header, at breaks/, and its library"""
    page = (APP / "site" / "breaks.html").read_text(encoding="utf-8")
    css = (APP / "site" / "site.css").read_text(encoding="utf-8")
    if "/*SITE_CSS*/" not in page or "<!--HEADER-->" not in page:
        raise SystemExit("app/site/breaks.html: no /*SITE_CSS*/ or <!--HEADER--> to fill")
    page = page.replace("<style>/*SITE_CSS*/", FONTS + "<style>" + css, 1).replace("<!--HEADER-->", header("Your break"), 1)
    (site / "breaks").mkdir(parents=True, exist_ok=True)
    (site / "breaks" / "index.html").write_text(page, encoding="utf-8")
    print(f"site_style: breaks/: the break manager, {library(site / 'breaks')} breaks in its library")


def main(site):
    site = Path(site)
    install(site)
    emu(site)
    breaks(site)
    home = site / "index.html"
    if home.exists():
        home.write_text(restyle(home, "home", "manual/#controls"), encoding="utf-8")
    man = site / "manual" / "index.html"
    if not man.exists():
        print(f"site_style: {site}: front page restyled (no manual)")
        return
    s = restyle(man, "manual", "#controls")
    h = re.search(r'<h2 id="(s\d+)"><span class="num">\d+</span>The control map</h2>', s)
    if not h:
        raise SystemExit(f"{man}: no section \"The control map\" (app/docs/MANUAL.md)")
    end = re.compile(r"<h2 |</main>").search(s, h.end())
    widget = (APP / "site" / "controlmap.html").read_text(encoding="utf-8")
    widget = re.sub(r"<!--.*?-->\s*", "", widget, count=1, flags=re.S)
    s = (s[:h.start()] + '<span id="controls"></span>' + s[h.start():h.end()] + "\n" + widget + "\n" + s[end.start():])
    s = s.replace("</body>", SPY + "</body>", 1)
    man.write_text(s, encoding="utf-8")
    print(f"site_style: {site}: the front page and the manual restyled, the control map in section {h.group(1)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
