#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# The manual's and the README's screenshots, from the host scenarios: runs every tests/scenarios/*.txt in the host
# simulator, then copies each picture the docs show from the scenario shot it comes from (MAP), builds the README's
# two composites and the Firmware Hub card. Run it after any change to what the screen draws, so every picture shows
# the same firmware. Needs ./build.sh --host first.
#   tools/doc_shots.py            refresh them all
#   tools/doc_shots.py --check    say which pictures differ from the shots, change nothing
# Not from the scenarios (left alone): screen-demo.png (the demo's scenes side by side), screen-updating.png (the
# update loader's screen).
import os
import subprocess
import sys

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, "app/build/scenarios")
IMG = os.path.join(ROOT, "app/docs/img")
HOST = os.path.join(ROOT, "app/build/host/choppah_host")

# app/docs/img/<name>.png  <-  app/build/scenarios/<shot>.png (the scenario that takes it in the comment)
MAP = {
    "pad-dub": "p_dub",                  # pads.txt
    "pad-roll-swell": "r_swell",         # pitch.txt
    "pad-roll8": "p_roll8",              # pads.txt
    "pad-stop": "p_stop",                # pads.txt
    "screen-arp": "a_four",              # arp.txt
    "screen-channel": "mm_channel_set",  # midimap.txt
    "screen-fx-more": "v_eq_more",       # fxviews.txt
    "screen-fx-tape": "v_tape",          # fxviews.txt
    "screen-fxchain": "v_filter_mod",    # fxviews.txt
    "screen-global": "m_clock_in",       # midi.txt
    "screen-held": "u6_held",            # ui.txt
    "screen-keyloop": "k_pingpong",      # keys.txt
    "screen-kick": "k_playing",          # kick.txt
    "screen-lobotomized": "r_lobotomized",   # reset.txt
    "screen-mbcomp-band": "c_mb_low",    # mbcomp.txt
    "screen-mbcomp-solo": "c_mb_solo",   # mbcomp.txt
    "screen-mbcomp": "c_mb_amount",      # mbcomp.txt
    "screen-mixer": "x_levels2",         # mixer.txt
    "screen-perc": "p_playing",          # perc.txt
    "screen-roll": "u7_roll",            # ui.txt
    "screen-selhelp": "sel_help",        # controls.txt
    "screen-seq": "q_hat",               # seq.txt
    "screen-slicefx": "x_key_fx",        # slicefx.txt
    "screen-slicer": "u5_playing",       # ui.txt
    "screen-splash": "u0_splash",        # ui.txt
    "screen-start": "u3_intro_full",     # ui.txt
    "screen-tap": "t_tap_page",          # tap.txt
    "screen-split": "sp_half_b",         # split.txt
    "screen-split-global": "sp_on",      # split.txt
    "screen-split-layer": "sp_layer",    # split.txt
}
# the README's composites (docs/img): tiles of 240 x 240, 10 apart on the site's grey
COMPOSITES = {
    "docs/img/screens.png": (3, ["u0_splash", "u3_intro_full", "u4_slicer_stopped", "u5_playing", "u6_held",
                                 "u7_roll"]),
    "docs/img/pads.png": (4, ["p_roll8", "p_rev", "p_frz", "p_stop", "p_gate", "p_rnd", "p_flt", "p_dub"]),
}
CARD = ("docs/img/card.png", "u5_playing")   # the Firmware Hub's card picture
BG = (40, 40, 46)


def shot(name):
    return Image.open(os.path.join(SHOTS, name + ".png")).convert("RGB")


def composite(cols, names):
    rows = (len(names) + cols - 1) // cols
    out = Image.new("RGB", (10 + cols * 250, 10 + rows * 250), BG)
    for k, n in enumerate(names):
        out.paste(shot(n), (10 + (k % cols) * 250, 10 + (k // cols) * 250))
    return out


def same(path, img):
    if not os.path.exists(path):
        return False
    old = Image.open(path).convert("RGB")
    return old.size == img.size and ImageChops.difference(old, img).getbbox() is None


def main():
    check = "--check" in sys.argv
    if not os.path.exists(HOST):
        sys.exit("doc_shots: no host simulator (./build.sh --host)")
    os.makedirs(SHOTS, exist_ok=True)
    scen = os.path.join(ROOT, "tests/scenarios")
    for f in sorted(os.listdir(scen)):
        if f.endswith(".txt"):
            subprocess.run([HOST, os.path.join(scen, f), SHOTS], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    want = {os.path.join(IMG, d + ".png"): shot(s) for d, s in MAP.items()}
    for path, (cols, names) in COMPOSITES.items():
        want[os.path.join(ROOT, path)] = composite(cols, names)
    want[os.path.join(ROOT, CARD[0])] = shot(CARD[1])
    changed = [p for p, img in want.items() if not same(p, img)]
    for p in changed:
        print(("differs " if check else "updated ") + os.path.relpath(p, ROOT))
        if not check:
            want[p].save(p)
    print("doc_shots: %d pictures, %d %s" % (len(want), len(changed), "differ" if check else "updated"))
    sys.exit(1 if check and changed else 0)


if __name__ == "__main__":
    main()
