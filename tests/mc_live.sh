#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# MATHCORE live from the hub's editor (docs/PRD_PACKS.md 10.10) in the host simulator: PUTSET writes scene A's phrase
# and the chain and a black key into RAM (stopped, and again while playing), MCCTL plays a scene round and round,
# the chain, launches a fill as a black key would, sets the macros, mutes a lane and stops; STATUS says what plays;
# a write outside the scenes and the chain is refused. Needs ./build.sh --host first.
#   tests/mc_live.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/mc_live
mkdir -p "$O"
H=app/build/host/choppah_host
f() {  # a frame: f CMD HEX-PAYLOAD
python3 - "$1" "$2" <<'P'
import sys; sys.path.insert(0, 'tools'); import cu_frames as c
print("sysex " + c.hexs(c.frame(int(sys.argv[1]), bytes.fromhex(sys.argv[2]))))
P
}
le() { python3 -c "import sys; print(int(sys.argv[1]).to_bytes(4, 'little').hex())" "$1"; }
SC0=$(( 4 + 96 ))           # scene A's phrase (bars)
CHN=$(( 1604 + 4 ))         # the chain: n, then (4 on) q[64], (68 on) key[11]
{
printf 'wait 2600\ntap HOME\nwait 300\n'
f 8 "$(le $SC0)08"
f 8 "$(le $CHN)02000000030105"                       # n 2 (rsv kept), q: D B (and a third, past n)
f 8 "$(le $(( CHN + 68 )))0f"                        # black key 1: scene P
cat <<S
wait 100
expect sc0_bars 8
expect mq_n 2
expect mq_first 3
expect mq_last 1
expect mq_key0 15
expect playing 0
S
f 10 "010200"                                       # play scene C round and round
cat <<S
wait 400
expect playing 1
expect mc_on 2
expect mc_scene 2
expect mc_edit 2
S
f 8 "$(le $SC0)02"                                   # written while playing
f 10 "0450323200"                                    # the macros: DENSITY 80, MATH 50, SPEED 50
cat <<S
wait 100
expect sc0_bars 2
expect mc_math 50
expect playing 1
sysex_log $PWD/$O/status.syx
sysex_out
S
f 9 ""
printf 'wait 100\nsysex_log off\n'
f 10 "02"                                            # the chain, from the next bar
cat <<S
wait 2600
expect mc_on 1
expect mc_scene 3
S
f 10 "0305"                                          # F (a fill) launched: on the next beat
cat <<S
wait 700
expect mc_scene 5
S
f 10 "0600ff"                                        # lane 1 muted
cat <<S
wait 100
expect mc_state0 -1
S
f 10 "00"                                            # stop
cat <<S
wait 300
expect playing 0
expect mc_on 0
S
f 8 "$(le 0)00"                                      # the scenes' magic: refused
printf 'expect_sysex F0 7D 43 48 48 02 F7\nwait 100\n'
f 8 "$(le 2000)00"                                   # past the chain: refused
printf 'expect_sysex F0 7D 43 48 48 02 F7\nwait 100\n'
} > "$O/s.txt"
$H "$O/s.txt" app/build/scenarios > "$O/run.log" 2>&1 || { grep -E "FAIL|host:" "$O/run.log"; exit 1; }
grep -q "EXPECTATIONS FAILED" "$O/run.log" && { grep FAIL "$O/run.log"; exit 1; }
python3 - "$O/status.syx" <<'P'
import sys
raw = open(sys.argv[1], "rb").read()
fr = [f.split(b"\xf7")[0] for f in raw.split(b"\xf0")[1:] if f[:5] == bytes([0x7D, 0x43, 0x48, 0x49, 0])]
assert fr, "no STATUS answer"
p, v = fr[-1][5:], bytearray()
for i in range(0, len(p), 8):
    v += bytes(p[i + j] | ((p[i] >> (j - 1)) & 1) << 7 for j in range(1, min(8, len(p) - i)))
assert len(v) == 32 and v[0] == 1, f"STATUS: 32 bytes, version 1: {list(v)}"
assert v[1] == 1 and v[2] == 2 and v[3] == 2 and v[5] == 2, f"playing scene C round and round: {list(v[:8])}"
assert list(v[21:25]) == [80, 50, 50, 0], f"the macros: {list(v[21:25])}"
assert v[20] == 2, "the chain's length"
P
echo "mc_live: PUTSET wrote scene A, the chain and a black key (stopped and playing); MCCTL played a scene, the chain, launched a fill on the beat, set the macros, muted a lane, stopped; STATUS said so; writes outside refused"
