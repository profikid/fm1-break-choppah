#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Prepare breaks for CHOPPAH (PRD A-61): WAV in -> whole bars -> slices -> resample -> IMA ADPCM -> a bank image.

    tools/prep_breaks.py amen.wav:136.9:4 think.wav --bars 2 -o build/bank.bin
    tools/prep_breaks.py --dry-run loops/*.wav           # what fits, without writing anything
    tools/prep_breaks.py --header app/bank_data.h ...    # the bank as a C array, built into the firmware
    tools/prep_breaks.py --list breaks/breaks.txt --header app/bank_data.h   # what ./build.sh does

Each input is FILE[:BPM[:BARS[:NAME]]]. Without a BPM, the tempo comes from the length and --bars (the file is taken to be
whole bars). Slices are an equal grid (--slices 16 a bar by default, at most 32 a break) or, with --transients,
the grid's markers moved to the nearest drum hit. Python 3 standard library only.

--list reads a catalogue (breaks/breaks.txt): one WAV a line, "IN FILE BPM BARS OF NAME", FILE relative to the
catalogue; the lines whose IN is * become the bank, in order. Paths in the manifest stay relative, so nothing generated
points outside the repo.

The bank layout is app/dsp/brk_bank.h's. Uploading it to the FM-1 arrives in milestone M5 (tools/fm1_breaks.py).
"""
import argparse
import array
import json
import math
import struct
import sys
import wave
import zlib
from pathlib import Path

BANK_MAGIC = 0x31424843          # "CHB1"
BANK_VERSION = 1
BANK_SLOTS = 16
BANK_SLICES = 32
SLOT_FMT = "<12sIIIIIBBH33I"      # brk_bank.h bank_slot_t
HDR_FMT = "<IHHII"
# flash for breaks (PRD 8.2): 0x97000..0xD9FFF and PLAT_XDATA1 0xE5000..0xFBFFF, less a sector of headroom
BANK_CAPACITY = (0xDA000 - 0x97000) + (0xFC000 - 0xE5000) - 0x1000
# a bank built into the firmware (--header) shares fm1-base's app slot (581,564 B) with the code (~110 KB): 448 KB
# keeps ~20 KB for the code to grow. The build checks the image against the slot as well.
IMAGE_CAPACITY = 0x70000
# a break decodes into the firmware's PCM buffer (app.h CH_PCM_MAX): no break longer than that
BANK_MAX_LEN = 81920

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
        4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767]
ADJ = [-1, -1, -1, -1, 2, 4, 6, 8]


def read_wav(path):
    """mono floats -1..1 and the rate"""
    with wave.open(str(path), "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        a = array.array("h", raw)
        scale = 32768.0
    elif width == 3:
        a = [int.from_bytes(raw[i:i + 3], "little", signed=True) for i in range(0, len(raw), 3)]
        scale = 8388608.0
    elif width == 4:
        a = array.array("i", raw)
        scale = 2147483648.0
    elif width == 1:
        a = [b - 128 for b in raw]
        scale = 128.0
    else:
        raise SystemExit(f"{path}: {8 * width}-bit WAV not supported")
    if sys.byteorder == "big" and width in (2, 4):
        a.byteswap()
    mono = [sum(a[i:i + ch]) / (ch * scale) for i in range(0, len(a) - ch + 1, ch)]
    return mono, rate


def resample(x, src, dst):
    """windowed-sinc (8 zero crossings each side), with a low-pass at the lower Nyquist"""
    if src == dst:
        return list(x)
    ratio = dst / src
    cut = min(1.0, ratio)
    n_out = int(len(x) * ratio)
    half = 8
    out = []
    for i in range(n_out):
        t = i / ratio
        c = int(t)
        acc = 0.0
        norm = 0.0
        for k in range(c - int(half / cut), c + int(half / cut) + 1):
            if 0 <= k < len(x):
                d = (t - k) * cut
                s = 1.0 if d == 0 else math.sin(math.pi * d) / (math.pi * d)
                win = 0.5 + 0.5 * math.cos(math.pi * d / half) if abs(d) < half else 0.0
                acc += x[k] * s * win
                norm += s * win
        out.append(acc / norm if norm else 0.0)
    return out


def onsets(x, rate):
    """an energy-flux curve, one value per 256 samples"""
    hop = 256
    env = []
    prev = 0.0
    for i in range(0, len(x) - hop, hop):
        e = sum(v * v for v in x[i:i + hop])
        env.append(max(0.0, e - prev))
        prev = e
    return env, hop


def slice_points(x, rate, nslices, transients):
    n = len(x)
    pts = [n * i // nslices for i in range(nslices + 1)]
    if transients:
        env, hop = onsets(x, rate)
        reach = n // nslices // 3            # a marker moves at most a third of a slice
        for i in range(1, nslices):
            lo, hi = max(0, (pts[i] - reach) // hop), min(len(env) - 1, (pts[i] + reach) // hop)
            if hi > lo:
                best = max(range(lo, hi + 1), key=lambda j: env[j])
                pts[i] = best * hop
        pts = sorted(set(pts))
        if len(pts) != nslices + 1:          # two markers met: back to the grid
            pts = [n * i // nslices for i in range(nslices + 1)]
    return pts


def adpcm_encode(x):
    pred, idx = 0, 0
    out = bytearray((len(x) + 1) // 2)
    for i, v in enumerate(x):
        s = max(-32768, min(32767, int(round(v * 32767.0))))
        d, step, code = s - pred, STEP[idx], 0
        diff = step >> 3
        if d < 0:
            code, d = 8, -d
        if d >= step:
            code |= 4; d -= step; diff += step
        if d >= step >> 1:
            code |= 2; d -= step >> 1; diff += step >> 1
        if d >= step >> 2:
            code |= 1; diff += step >> 2
        pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
        idx = max(0, min(88, idx + ADJ[code & 7]))
        out[i >> 1] |= code << (4 * (i & 1))
    return bytes(out)


def adpcm_decode(data, n):
    pred, idx, out = 0, 0, []
    for i in range(n):
        code = (data[i >> 1] >> (4 * (i & 1))) & 15
        step = STEP[idx]
        diff = step >> 3
        if code & 4: diff += step
        if code & 2: diff += step >> 1
        if code & 1: diff += step >> 2
        pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
        idx = max(0, min(88, idx + ADJ[code & 7]))
        out.append(pred)
    return out


def read_list(path):
    """the catalogue's lines marked *, as specs FILE:BPM:BARS:NAME (FILE resolved) and the FILE as written"""
    specs = []
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split(None, 5)
        if len(f) < 6 or f[0] not in ("*", "-"):
            raise SystemExit(f"{path}:{n}: expected IN FILE BPM BARS OF NAME, IN * or -")
        if f[0] == "*":
            specs.append((f"{Path(path).parent / f[1]}:{f[2]}:{f[3]}:{f[5].strip()}", f[1]))
    if not specs:
        raise SystemExit(f"{path}: no line marked *")
    return specs


def prepare(spec, args, shown=None):
    parts = spec.split(":")
    path = Path(parts[0])
    bpm = float(parts[1]) if len(parts) > 1 and parts[1] else None
    bars = int(parts[2]) if len(parts) > 2 and parts[2] else args.bars
    given = parts[3] if len(parts) > 3 else None
    x, rate = read_wav(path)
    if bpm is None:                          # the file is whole bars
        bpm = 240.0 * bars / (len(x) / rate)
    bar_len = rate * 240.0 / bpm
    x = x[:int(round(bar_len * bars))]       # exactly the bars asked for
    if len(x) < int(bar_len * bars) - 2:
        raise SystemExit(f"{path}: shorter than {bars} bar(s) at {bpm:.2f} BPM")
    peak = max(1e-9, max(abs(v) for v in x))
    x = [v * (0.98 / peak) for v in x]       # normalise
    x = resample(x, rate, args.rate)
    if len(x) > BANK_MAX_LEN:
        raise SystemExit(f"{shown or path}: {bars} bar(s) at {bpm:.2f} BPM are {len(x)} samples, the firmware's buffer "
                         f"holds {BANK_MAX_LEN} (fewer bars)")
    nslices = min(BANK_SLICES, args.break_slices or args.slices * bars)
    pts = slice_points(x, args.rate, nslices, args.transients)
    data = adpcm_encode(x)
    name = (given or (args.name_prefix + path.stem)).upper()[:11]
    return {"name": name, "file": shown or str(path), "bpm": round(bpm, 2), "bars": bars, "rate": args.rate,
            "len": len(x), "nslices": nslices, "slices": pts, "data": data, "pcm": x}


def build_bank(slots):
    table = bytearray()
    data = bytearray()
    off = 16 + 168 * len(slots)
    for s in slots:
        pad = (-len(data)) % 4
        data += b"\0" * pad
        sl = s["slices"] + [s["len"]] * (BANK_SLICES + 1 - len(s["slices"]))
        table += struct.pack(SLOT_FMT, s["name"].encode("ascii", "replace"), int(round(s["bpm"] * 100)), s["rate"],
                             s["len"], off + len(data), len(s["data"]), s["bars"], s["nslices"], 0, *sl)
        data += s["data"]
    body = bytes(table + data)
    return struct.pack(HDR_FMT, BANK_MAGIC, BANK_VERSION, len(slots), len(body), zlib.crc32(body)) + body


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="*", help="FILE[:BPM[:BARS[:NAME]]]")
    ap.add_argument("--list", type=Path, help="a catalogue (breaks/breaks.txt): its lines marked * instead of inputs")
    ap.add_argument("--bars", type=int, default=1, help="bars per break when not given (default 1)")
    ap.add_argument("--slices", type=int, default=16, help="slices a bar (default 16)")
    ap.add_argument("--break-slices", type=int, default=0,
                    help="slices a break whatever its bars (16: one per white key) instead of --slices a bar")
    ap.add_argument("--rate", type=int, default=22050, help="sample rate in the bank (default 22050, PRD Q4)")
    ap.add_argument("--transients", action="store_true", help="move markers to the nearest hit")
    ap.add_argument("--name-prefix", default="", help="put before each name")
    ap.add_argument("-o", "--out", type=Path, default=Path("build/bank.bin"))
    ap.add_argument("--dry-run", action="store_true", help="report what fits, write nothing")
    ap.add_argument("--header", type=Path, help="also write the bank as a C header (CH_BANK[]) for the firmware")
    ap.add_argument("--verify", action="store_true", help="decode again and report the ADPCM's SNR")
    args = ap.parse_args()
    specs = read_list(args.list) if args.list else [(i, None) for i in args.inputs]
    if not specs:
        ap.error("inputs or --list")
    if len(specs) > BANK_SLOTS:
        raise SystemExit(f"{len(specs)} breaks: at most {BANK_SLOTS}")
    slots = [prepare(s, args, shown) for s, shown in specs]
    capacity = IMAGE_CAPACITY if args.header else BANK_CAPACITY
    bank = build_bank(slots)
    for s in slots:
        line = (f"{s['name']:<12} {s['bpm']:7.2f} BPM  {s['bars']} bar(s)  {s['nslices']:2} slices  "
                f"{s['len'] / s['rate']:5.2f} s  {len(s['data']) / 1024:6.1f} KB")
        if args.verify:
            d = adpcm_decode(s["data"], s["len"])
            sig = sum((v * 32767.0) ** 2 for v in s["pcm"])
            err = sum((v * 32767.0 - q) ** 2 for v, q in zip(s["pcm"], d)) + 1.0
            line += f"  SNR {10 * math.log10(sig / err):5.1f} dB"
        print(line)
    print(f"bank: {len(bank) / 1024:.1f} KB of {capacity / 1024:.0f} KB "
          f"({100 * len(bank) / capacity:.0f} %){' (built into the firmware)' if args.header else ''}")
    if len(bank) > capacity:
        raise SystemExit("does not fit: fewer bars, a lower --rate, or fewer breaks")
    if args.dry_run:
        return
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bank)
    manifest = [{k: v for k, v in s.items() if k not in ("data", "pcm")} for s in slots]
    args.out.with_suffix(".json").write_text(json.dumps(manifest, indent=2))
    print(f"wrote {args.out} and {args.out.with_suffix('.json')}")
    if args.header:
        rows = ",\n".join("    " + ",".join(f"0x{b:02x}" for b in bank[i:i + 24]) for i in range(0, len(bank), 24))
        names = ", ".join(s["name"] for s in slots)
        args.header.write_text(
            "/* Generated by tools/prep_breaks.py: don't edit (./build.sh makes it again from breaks/breaks.txt, and\n"
            f" * git ignores it). Slots: {names} */\n"
            "#pragma once\n#include <stdint.h>\n"
            f"#define CH_BANK_SIZE {len(bank)}u\n"
            "static const uint8_t CH_BANK[CH_BANK_SIZE] __attribute__((aligned(4))) = {\n" + rows + "\n};\n")
        print(f"wrote {args.header} ({len(bank)} bytes)")


if __name__ == "__main__":
    main()
