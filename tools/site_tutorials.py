#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Add CHOPPAH's YouTube feature video and playlist tutorial grid to the generated site."""
from pathlib import Path
import re

VIDEO_ID = "0LbcofrnxeE"
PLAYLIST_ID = "PLJdwivwuLNoU"


def add_nav_link(path: Path, href: str):
    s = path.read_text(encoding="utf-8")
    if ">Tutorials</a>" in s:
        return
    # site_style.py has already inserted the Controls link on each page.
    s, n = re.subn(
        r'(<a href="[^"]*"[^>]*>Controls</a>)',
        r'\1<a href="' + href + '">Tutorials</a>',
        s, count=1
    )
    if n:
        path.write_text(s, encoding="utf-8")


def main(site_dir):
    site = Path(site_dir)
    home = site / "index.html"
    if home.exists():
        s = home.read_text(encoding="utf-8")
        if "choppah-feature-video" not in s:
            embed = f"""
<section class="choppah-feature-video" aria-label="CHOPPAH video">
  <div class="choppah-video-frame">
    <iframe src="https://www.youtube-nocookie.com/embed/{VIDEO_ID}?rel=0&amp;list={PLAYLIST_ID}"
      title="CHOPPAH video" loading="lazy"
      allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share"
      referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>
  </div>
</section>
"""
            s, n = re.subn(r"(</h1>)", r"\1" + embed, s, count=1)
            if n:
                s = s.replace("</style>", """
.choppah-feature-video { margin: 28px 0 38px; }
.choppah-video-frame { position: relative; width: 100%; aspect-ratio: 16 / 9; overflow: hidden;
  border: 1px solid var(--line); border-radius: 10px; background: #050505;
  box-shadow: 0 18px 50px rgba(0,0,0,.35); }
.choppah-video-frame iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
</style>""", 1)
                home.write_text(s, encoding="utf-8")
        add_nav_link(home, "tutorials/")

    # Add Tutorials to the navigation on the existing sub-pages.
    for page in site.glob("*/index.html"):
        if page.parent.name != "tutorials":
            add_nav_link(page, "../tutorials/")

    tutorials = site / "tutorials"
    tutorials.mkdir(parents=True, exist_ok=True)
    page = tutorials / "index.html"
    page.write_text(f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0e0d0b">
<title>Video Tutorials · CHOPPAH</title>
<meta name="description" content="CHOPPAH video tutorials for the M-VAVE FM-1.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700&family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap" rel="stylesheet">
<style>
:root{{--bg:#0e0d0b;--panel:#171510;--raise:#201d16;--line:#2f2a20;--fg:#ede6d4;--dim:#a39a85;--amber:#ffb21a;--hot:#fff3d3;--display:"Chakra Petch",sans-serif;--body:"IBM Plex Sans",sans-serif;--mono:"IBM Plex Mono",monospace;color-scheme:dark}}
*{{box-sizing:border-box}}body{{margin:0;padding:0 20px 64px;background:var(--bg);color:var(--fg);font:16px/1.6 var(--body)}}
.wrap{{max-width:1160px;margin:auto}}header.site{{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;align-items:center;gap:12px 20px;padding:14px 0;margin-bottom:38px;background:var(--bg);border-bottom:1px solid var(--line)}}
.home{{font:700 21px var(--display);color:var(--fg);text-decoration:none}}nav{{margin-left:auto;display:flex;flex-wrap:wrap;gap:6px}}nav a{{font:600 12px var(--mono);letter-spacing:.08em;text-transform:uppercase;text-decoration:none;color:var(--hot);background:#2a261d;padding:9px 12px;border-radius:5px}}nav a[aria-current]{{background:var(--amber);color:#1a1406}}
h1{{font:600 clamp(36px,6vw,64px)/1.1 var(--display);margin:0 0 10px}}.lede{{color:var(--dim);font-size:18px;margin:0 0 26px}}
.player-wrap{{position:relative;aspect-ratio:16/9;width:100%;background:#050505;border:1px solid var(--line);border-radius:10px;overflow:hidden;box-shadow:0 18px 50px #0008}}
#player{{position:absolute;inset:0;width:100%;height:100%}}.section-head{{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin:38px 0 16px}}
h2{{font:600 24px var(--display);margin:0}}.playlist-link{{font:500 12px var(--mono);color:var(--amber);text-decoration:none}}
.video-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,260px),1fr));gap:16px}}
.video-card{{appearance:none;text-align:left;cursor:pointer;padding:0;background:var(--panel);border:1px solid var(--line);border-radius:9px;overflow:hidden;color:var(--fg);transition:border-color .15s,transform .15s}}
.video-card:hover,.video-card:focus-visible{{border-color:var(--amber);transform:translateY(-2px)}}.video-card[aria-current="true"]{{border-color:var(--amber);box-shadow:0 0 0 1px var(--amber)}}
.thumb{{display:block;width:100%;aspect-ratio:16/9;object-fit:cover;background:#090806}}.video-label{{display:block;padding:12px 14px;font:600 15px var(--display)}}.video-number{{display:block;color:var(--amber);font:500 11px var(--mono);letter-spacing:.1em;margin-bottom:3px}}
#status{{color:var(--dim);font:13px var(--mono);margin:16px 0}}@media(max-width:620px){{header.site{{position:static}}nav{{margin-left:0}}body{{padding-left:14px;padding-right:14px}}}}
</style>
</head>
<body><div class="wrap">
<header class="site"><a class="home" href="../">CHOPPAH</a><nav>
<a href="../emu/">Emulator</a><a href="../install/">Install</a><a href="../manual/">Manual</a><a href="../manual/#controls">Controls</a><a href="./" aria-current="page">Tutorials</a><a href="https://github.com/profikid/fm1-break-choppah">Source</a>
</nav></header>
<main>
<h1>Video Tutorials</h1>
<p class="lede">Learn CHOPPAH, one chop at a time. Select a video below to play it.</p>
<div class="player-wrap"><div id="player" aria-label="CHOPPAH tutorial playlist player"></div></div>
<div class="section-head"><h2>All videos</h2><a class="playlist-link" href="https://www.youtube.com/playlist?list={PLAYLIST_ID}" target="_blank" rel="noopener">OPEN PLAYLIST ON YOUTUBE ↗</a></div>
<div id="video-grid" class="video-grid" aria-live="polite"></div>
<p id="status">Loading playlist…</p>
</main></div>
<script>
const PLAYLIST_ID = "{PLAYLIST_ID}";
let player;
let rendered = false;
function renderPlaylist() {{
  if (rendered || !player || typeof player.getPlaylist !== "function") return;
  const ids = player.getPlaylist() || [];
  if (!ids.length) return;
  rendered = true;
  const grid = document.getElementById("video-grid");
  ids.forEach((id, index) => {{
    const card = document.createElement("button");
    card.type = "button";
    card.className = "video-card";
    card.setAttribute("aria-current", index === 0 ? "true" : "false");
    card.innerHTML = '<img class="thumb" loading="lazy" alt="" src="https://i.ytimg.com/vi/' + id + '/hqdefault.jpg">' +
      '<span class="video-label"><span class="video-number">TUTORIAL ' + String(index + 1).padStart(2, "0") + '</span>Video ' + String(index + 1).padStart(2, "0") + '</span>';
    card.addEventListener("click", () => {{
      player.loadVideoById(id);
      grid.querySelectorAll(".video-card").forEach(el => el.setAttribute("aria-current", "false"));
      card.setAttribute("aria-current", "true");
      document.querySelector(".player-wrap").scrollIntoView({{behavior:"smooth",block:"start"}});
    }});
    grid.appendChild(card);
  }});
  document.getElementById("status").textContent = ids.length + " videos in this playlist.";
}}
function onYouTubeIframeAPIReady() {{
  player = new YT.Player("player", {{
    playerVars: {{listType:"playlist",list:PLAYLIST_ID,rel:0}},
    events: {{
      onReady: () => {{ renderPlaylist(); setTimeout(renderPlaylist, 1200); }},
      onStateChange: () => renderPlaylist()
    }}
  }});
}}
(function() {{
  const tag = document.createElement("script");
  tag.src = "https://www.youtube.com/iframe_api";
  document.head.appendChild(tag);
}})();
</script>
</body></html>
''', encoding="utf-8")


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        raise SystemExit("usage: site_tutorials.py SITE_DIR")
    main(sys.argv[1])
