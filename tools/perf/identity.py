#!/usr/bin/env python3
"""Compares two renders by tools/perf/host.cpp, bit for bit. Not part of any kit.
  python3 -I tools/perf/identity.py <label> <base index> <base bin> <head index> <head bin>
Prints one line per run that differs (its first differing block and voice,
and for raw runs the first differing sample and the difference's peak in
dBFS), appends a table to $GITHUB_STEP_SUMMARY, and exits 1 on any difference
or on runs missing from either side."""
import math, os, struct, sys

label, bi, bb, hi, hb = sys.argv[1:6]


def index(path):
    runs = {}
    for line in open(path):
        f = line.split()
        if len(f) == 6:
            runs[f[0]] = (f[1], int(f[2]), int(f[3]), int(f[4]), int(f[5]))
    return runs


base, head = index(bi), index(hi)
fb, fh = open(bb, "rb"), open(hb, "rb")
problems, same = [], 0
for name in base:
    if name not in head:
        problems.append((name, "missing from the head's render"))
        continue
    kb, vb, nb, ob, sb = base[name]
    kh, vh, nh, oh, sh = head[name]
    fb.seek(ob)
    fh.seek(oh)
    db, dh = fb.read(sb), fh.read(sh)
    if (kb, vb, nb) == (kh, vh, nh) and db == dh:
        same += 1
        continue
    if (kb, vb) != (kh, vh):
        problems.append((name, f"renders as {kh}/{vh} voices, was {kb}/{vb}"))
        continue
    unit = 256 if kb == "raw" else 4
    n = min(len(db), len(dh)) // unit
    first = next((i for i in range(n) if db[i * unit:(i + 1) * unit] != dh[i * unit:(i + 1) * unit]), n)
    block, voice = divmod(first, vb)
    what = f"first differs at block {block}, voice {voice}"
    if first == n:
        what = f"lasts {nh} blocks, was {nb}"
    elif kb == "raw":
        a = struct.unpack("<64i", db[first * 256:(first + 1) * 256])
        b = struct.unpack("<64i", dh[first * 256:(first + 1) * 256])
        s = next(i for i in range(64) if a[i] != b[i])
        peak = 0
        for i in range(first, n):
            x = struct.unpack("<64i", db[i * 256:(i + 1) * 256])
            y = struct.unpack("<64i", dh[i * 256:(i + 1) * 256])
            peak = max(peak, max(abs(p - q) for p, q in zip(x, y)))
        what += f", sample {s} ({a[s]} -> {b[s]}); peak difference {20 * math.log10(max(peak, 1) / 2 ** 31):.1f} dBFS"
    if nb != nh and first != n:
        what += f"; lasts {nh} blocks, was {nb}"
    problems.append((name, what))
for name in head:
    if name not in base:
        problems.append((name, "missing from the base's render"))

print(f"{label}: {same} runs identical, {len(problems)} differ")
for name, what in problems[:40]:
    print(f"  DIFFERS {name}: {what}")
if len(problems) > 40:
    print(f"  ... and {len(problems) - 40} more")
summary = os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary, "a") as s:
        s.write(f"\n#### {label}: {same} runs identical, {len(problems)} differ\n\n")
        if problems:
            s.write("| Run | First difference |\n| --- | --- |\n")
            for name, what in problems[:60]:
                s.write(f"| `{name}` | {what} |\n")
sys.exit(1 if problems else 0)
