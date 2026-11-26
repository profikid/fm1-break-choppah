#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# A pack into the browser emulator from another page of the same site (PRD_PACKS PH-035, choppah.app's Install):
# the emulator's parts midi, port and data (fm1.toml emu) carry the hub's SysEx (fm1link.js, the same as over USB)
# to app_sysex; the pack (both banks, an overlay) must read back byte for byte, show as the pack on the emulated
# FM-1, and still be there after the emulator's page reloads. Needs Emscripten (./build.sh --web), node and Chrome
# (CHROME=path); puppeteer-core is installed into app/build/emu_install the first time.
#   tests/emu_install.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/emu_install
[ -f app/build/emu/parts/port_client.js ] || ./build.sh --web
mkdir -p "$O/root/tests"
[ -d "$O/node_modules/puppeteer-core" ] || (cd "$O" && npm init -y >/dev/null && npm i --silent puppeteer-core@23)
cp tests/emu_install/run.mjs "$O/"
ln -sfn "$(pwd)/app/build/emu" "$O/root/emu"
ln -sfn "$(pwd)/app/site" "$O/root/site"
ln -sfn "$(pwd)/tests/emu_install" "$O/root/tests/emu_install"
CHROME=${CHROME:-$(command -v google-chrome || command -v chromium || echo "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")}
PORT=8771
python3 -m http.server $PORT -d "$O/root" >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV' EXIT
sleep 1
node "$O/run.mjs" "http://localhost:$PORT" "$CHROME" | python3 -c "
import json, sys
r = json.loads(sys.stdin.read()); i = r.get('install') or {}; a = i.get('after') or {}; z = r.get('reloaded') or {}
want = 'EMU PACK'
ok = (not r.get('error') and all(i.get('same', [False])) and (a.get('pack') or {}).get('name') == want
      and a.get('n') == 6 and z.get('n') == 6 and (z.get('pack') or {}).get('name') == want)
print('  port      ', i.get('port'), '| INFO', a.get('proto'), '|', i.get('secs'), 's')
print('  installed ', a.get('n'), 'breaks', (a.get('pack') or {}).get('name'), '| byte for byte', i.get('same'))
print('  reloaded  ', z.get('n'), 'breaks', (z.get('pack') or {}).get('name'))
if r.get('error'): print('  error     ', r['error'])
print('emu_install: ' + ('the pack went into the emulator and stayed' if ok else 'FAILED'))
sys.exit(0 if ok else 1)"
