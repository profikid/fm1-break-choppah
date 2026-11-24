#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The pack builder (app/site/breaks.html, beats.js, chpk.js) in Chrome: a made-up track (a pad, then FUNKY DRUMMER
# twice over it) in; its tempo, bars and loops found with the stems split; a loop cut into the pack; an overlay made
# from the rest; the pack's name and a break's loop settings; the .chpk out and back in. Needs node and Chrome
# (CHROME=path); puppeteer-core as tests/page_send.sh installs it.
#   tests/page_pack.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/page_send
mkdir -p "$O"
[ -d "$O/node_modules/puppeteer-core" ] || (cd "$O" && npm init -y >/dev/null && npm i --silent puppeteer-core@23)
cp tests/page_pack/run.mjs "$O/page_pack.mjs"
python3 - "$O/track.wav" <<'P'
import sys, math, struct
sys.path.insert(0, "tools")
import prep_breaks as pb
x, rate = pb.read_wav("breaks/classics/james_brown_funky_drummer.wav")
intro, outro = int(3.37 * rate), int(4 * rate)
n = intro + 2 * len(x) + outro
y = [0.08 * (math.sin(2 * math.pi * 220 * i / rate) + math.sin(2 * math.pi * 277.2 * i / rate) + math.sin(2 * math.pi * 329.6 * i / rate)) for i in range(n)]
for k in range(2):
    for i, v in enumerate(x): y[intro + k * len(x) + i] += v
pcm = b"".join(struct.pack("<h", max(-32768, min(32767, int(v * 32767)))) for v in y)
with open(sys.argv[1], "wb") as f:
    f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, 2 * rate, 2, 16) + b"data" + struct.pack("<I", len(pcm)) + pcm)
P
PORT=8768
python3 -m http.server $PORT -d app/site >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV' EXIT
sleep 1
R=$(cd "$O" && node page_pack.mjs "http://localhost:$PORT/breaks.html" "$(pwd)/track.wav" || true)
echo "$R"
echo "$R" | grep -q '"ok":true' && echo "page_pack: a track found, cut, an overlay, the pack out and back in" || { echo "page_pack: FAILED"; exit 1; }
