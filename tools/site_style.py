#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""CHOPPAH's look for the site fm1-base's tools/make_site.py builds (run it after make_site.py):

  tools/site_style.py SITE_DIR

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


def restyle(path, cls, controls_href):
    s = path.read_text(encoding="utf-8")
    css = (APP / "site" / "site.css").read_text(encoding="utf-8")
    s, n = re.subn(r"<style>.*?</style>", lambda m: FONTS + "<style>" + css + "</style>", s, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: no <style> to replace")
    s = s.replace("<body>", f'<body class="{cls}">', 1)
    nav = re.search(r'(<header class="site">.*?<nav>)(.*?)(</nav>)', s, re.S)
    if nav:                                          # "Controls" after "Manual"
        links = re.sub(r'(<a href="[^"]*manual/"[^>]*>Manual</a>)', r'\1<a href="' + controls_href + '">Controls</a>',
                       nav.group(2), count=1)
        s = s[:nav.start(2)] + links + s[nav.end(2):]
    return s


def main(site):
    site = Path(site)
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
