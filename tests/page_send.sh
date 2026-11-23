#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The break manager's Send over a bad USB link: breaks.html in Chrome against a stand-in FM-1 (tests/page_send/fake_fm1.js,
# ui/main.c's app_sysex: NOR flash, COMMIT's CRC-32 check) that loses frames on the way in, loses answers, answers late
# and restarts once mid-send. A pack of seven breaks (both banks) must arrive byte for byte every time, with the
# firmware's offset echo and without it (CHOPPAH before 1.6). Needs node and Chrome (CHROME=path to pick one);
# puppeteer-core is installed into app/build/page_send the first time.
#   tests/page_send.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/page_send
mkdir -p "$O"
[ -d "$O/node_modules/puppeteer-core" ] || (cd "$O" && npm init -y >/dev/null && npm i --silent puppeteer-core@23)
cp tests/page_send/run.js tests/page_send/fake_fm1.js "$O/"
C=breaks/classics
[ -f "$O/pack.chb" ] || python3 tools/cu_frames.py --out "$O/pack.chb" $C/lyn_collins_think_about_it.wav 113.78 THINK \
    $C/incredible_bongo_band_apache.wav 114.78 APACHE $C/lightnin_rod_sport.wav 120.69 SPORT \
    $C/the_winstons_amen_brother.wav 141.82 AMEN $C/jimmy_mcgriff_the_worm.wav 109.79 WORM \
    $C/bobby_byrd_hot_pants.wav 114.78 "HOT PANTS" $C/james_brown_cold_sweat.wav 114 "COLD SWEAT" > /dev/null
PORT=8767
python3 -m http.server $PORT -d app/site >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV' EXIT
sleep 1
U=http://localhost:$PORT/breaks.html
fails=0
for F in '{"seed":1}' '{"seed":11,"pIn":0.02,"pOut":0.02}' '{"seed":12,"pIn":0.05,"pOut":0.05,"pSlow":0.01,"rebootAt":300}' \
         '{"seed":13,"pIn":0.04,"pOut":0.04,"pSlow":0.02,"echo":false}'; do
    R=$(node "$O/run.js" "$U" "$F" "$(pwd)/$O/pack.chb" || true)
    echo "$R" | python3 -c "
import json, sys
r = json.loads(sys.stdin.read()); s = r.get('st', {})
ok = 'on the FM-1' in r.get('status', '') and r.get('same') == [True, True]
print(('  ok' if ok else 'FAIL'), sys.argv[1], '->', r.get('status') or r.get('error'),
      '| lost in/out %s/%s, late %s, restarts %s, %s s' % (s.get('dropIn'), s.get('dropOut'), s.get('slow'), s.get('reboots'), r.get('secs')))
sys.exit(0 if ok else 1)" "$F" || fails=$((fails + 1))
done
[ $fails = 0 ] && echo "page_send: the pack arrived byte for byte over every link" || { echo "page_send: $fails FAILED"; exit 1; }
