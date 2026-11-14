#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The screen reader's audit (fm1-reader's tools/audit.mjs): every text CHOPPAH can send a reader (the walk,
# tools/sr_walk.py, and every scenario's messages), put in words with the words file as the reader page will, then
# checked against fm1-reader's rules. Needs ./build.sh --host first, node, and fm1-reader next to this repo
# (../fm1-reader) or in FM1_READER.
#   tools/sr_audit.sh [--spoken]       --spoken: print everything as it will be heard
set -e
cd "$(dirname "$0")/.."
R="${FM1_READER:-../fm1-reader}"
[ -f "$R/tools/audit.mjs" ] || { echo "sr_audit: no fm1-reader at $R (git clone https://github.com/charlesvestal/fm1-reader $R)" >&2; exit 1; }
H=app/build/host/choppah_host
O=app/build/sr
mkdir -p "$O" app/build/scenarios
rm -f "$O/said.tsv"
python3 tools/sr_walk.py > "$O/walk.txt"
FM1B_SAID_LOG="$PWD/$O/said.tsv" "$H" "$O/walk.txt" "$O" > /dev/null
for f in tests/scenarios/*.txt; do
    FM1B_SAID_LOG="$PWD/$O/said.tsv" "$H" "$f" app/build/scenarios > /dev/null || true
done
sort -u "$O/said.tsv" > "$O/all.tsv"
ID="$(sed -n 's|.*words/\([^"]*\)\.json.*|\1|p' app/build/gen/ann_words.h | head -1)"
W="app/web/words/$ID.json"
[ -f "$W" ] || W="$(ls app/build/*/words/"$ID".json app/web/words/*.json 2>/dev/null | head -1)"
echo "sr_audit: $(wc -l < "$O/all.tsv") texts, words file $W"
D=""; for f in /usr/share/dict/words /usr/share/hunspell/en_US.dic; do [ -f "$f" ] && { cut -d/ -f1 "$f" > "$O/dict.txt"; D="--dict $O/dict.txt"; break; }; done
exec node "$R/tools/audit.mjs" "$O/all.tsv" --words "$W" --allow tools/sr_allow.txt $D $1
