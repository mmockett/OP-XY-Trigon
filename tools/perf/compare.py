#!/usr/bin/env python3
"""Compares two kits' sound from renders by tools/perf/host.cpp (compare
jobs, raw hits), for CI's compare job: writes every hit of both as WAV files
and a report. It reports and never fails. Not part of any kit.
  python3 -I tools/perf/compare.py <kit> <base index> <base bin> <head index> <head bin> <out folder>

For each hit, base and head: loudness (LUFS-M max, the kits' calibrate.py
measure, dB re the half-scale ceiling), sample peak (dB re the ceiling; over
0 is past what a voice may put out), the time its 10 ms level takes to fall
40 dB under its loudest, its spectral centroid over the first 0.5 s, and the
largest change in its octave-band energies (bands within 50 dB of its
loudest). Writes <out>/<kit>/{base,head}/<hit>.wav (16-bit, 44.1 kHz, the
ceiling at -6 dBFS as the OP-XY plays it), <out>/<kit>/report.md and
<out>/<kit>/report.json, and appends the per-model table to
$GITHUB_STEP_SUMMARY."""
import json, os, pathlib, struct, sys
import numpy as np

SR = 44100
CEILING = 2.0 ** 30
kit, bi, bb, hi, hb, out = sys.argv[1:7]
out = pathlib.Path(out) / kit


def runs(index, binary):
    data = open(binary, "rb").read()
    result = {}
    for line in open(index):
        f = line.split()
        if len(f) == 6 and f[1] == "raw" and f[2] == "1":
            o, n = int(f[4]), int(f[5])
            result[f[0]] = np.frombuffer(data[o:o + n], dtype="<i4").astype(np.float64)
    return result


def biquad_ir(b, a, n):
    y, x1, x2, y1, y2 = np.zeros(n), 0.0, 0.0, 0.0, 0.0
    for i in range(n):
        xi = 1.0 if i == 0 else 0.0
        yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, xi, y1, yi
        y[i] = yi
    return y


def k_ir():
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    K, Vh = np.tan(np.pi * fc / SR), 10 ** (G / 20)
    Vb, a0 = Vh ** 0.4996667741545416, 1 + K / Q + K * K
    shelf = biquad_ir([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                      [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 8192)
    Q, fc = 0.5003270373253953, 38.13547087613982
    K = np.tan(np.pi * fc / SR)
    a0 = 1 + K / Q + K * K
    hp = biquad_ir([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 16384)
    return np.convolve(shelf, hp)[:16384]


KW = k_ir()
BANDS = [31.25 * 2 ** i for i in range(10)]  # octave centres 31 Hz .. 16 kHz


def measure(x):
    x = x / CEILING
    if not len(x) or not np.any(x):
        return dict(lufs=None, peak=None, decay40=None, centroid=None, bands=None, seconds=len(x) / SR)
    p = np.pad(x, (0, int(0.4 * SR)))
    n = 1 << int(np.ceil(np.log2(len(p) + len(KW))))
    y = np.fft.irfft(np.fft.rfft(p, n) * np.fft.rfft(KW, n), n)[:len(p)]
    c = np.concatenate([[0], np.cumsum(y * y)])
    block = int(0.4 * SR)
    s = np.arange(0, len(y) - block + 1, SR // 100)
    lufs = -0.691 + 10 * np.log10(max(((c[s + block] - c[s]) / block).max(), 1e-20))
    peak = 20 * np.log10(max(np.max(np.abs(x)), 1e-12))
    w = int(0.01 * SR)
    e = np.concatenate([[0], np.cumsum(x * x)])
    starts = np.arange(0, max(len(x) - w, 1), w // 2)
    env = 10 * np.log10(np.maximum((e[np.minimum(starts + w, len(x))] - e[starts]) / w, 1e-30))
    top = int(np.argmax(env))
    above = np.nonzero(env >= env[top] - 40)[0]
    decay = (starts[above[-1]] + w) / SR if len(above) else 0.0
    head = x[:int(0.5 * SR)]
    spec = np.abs(np.fft.rfft(head * np.hanning(len(head)))) if len(head) > 16 else np.zeros(1)
    freqs = np.fft.rfftfreq(len(head), 1 / SR) if len(head) > 16 else np.zeros(1)
    centroid = float((spec * freqs).sum() / spec.sum()) if spec.sum() > 0 else None
    full = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    bands = []
    for fc in BANDS:
        sel = (f >= fc / np.sqrt(2)) & (f < fc * np.sqrt(2))
        bands.append(10 * np.log10(max(full[sel].sum(), 1e-30)))
    return dict(lufs=float(lufs), peak=float(peak), decay40=float(decay), centroid=centroid,
                bands=[float(b) for b in bands], seconds=len(x) / SR)


def wav(path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = np.clip(np.floor(np.asarray(x) / 65536), -32768, 32767).astype("<i2").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def band_change(a, b):
    if not a or not b:
        return None
    top = max(max(a), max(b))
    d = [abs(y - x) for x, y in zip(a, b) if max(x, y) > top - 50]
    return max(d) if d else 0.0


base, head = runs(bi, bb), runs(hi, hb)
rows = []
for name in base:
    if name not in head:
        continue
    safe = name.replace(":", "_")
    wav(out / "base" / f"{safe}.wav", base[name])
    wav(out / "head" / f"{safe}.wav", head[name])
    mb, mh = measure(base[name]), measure(head[name])
    rows.append(dict(name=name, model=name.split(":")[-1] if name.startswith("key:") else name.split(":")[1],
                     base=mb, head=mh, bands=band_change(mb["bands"], mh["bands"])))


def fmt(v, f="{:.1f}"):
    return "–" if v is None else f.format(v)


def delta(a, b, f="{:+.1f}"):
    return "–" if a is None or b is None else f.format(b - a)


models = []
for r in rows:
    if r["model"] not in models:
        models.append(r["model"])
lines = [f"### {kit}: sound, base → head\n",
         "Per model: its defaults hit (`model:<M>:defaults`); the spread is over every key and preset hit on it.\n",
         "| Model | LUFS base → head | Δ LU (keys: min..max) | Peak dB re ceiling, head (max over keys) | −40 dB decay s | Centroid Hz | Band Δ dB max |",
         "| --- | --- | --- | --- | --- | --- | --- |"]
for m in models:
    mine = [r for r in rows if r["model"] == m]
    d = next((r for r in mine if r["name"] == f"model:{m}:defaults"), mine[0])
    dl = [r["head"]["lufs"] - r["base"]["lufs"] for r in mine if r["head"]["lufs"] is not None and r["base"]["lufs"] is not None]
    peaks = [r["head"]["peak"] for r in mine if r["head"]["peak"] is not None]
    bandmax = [r["bands"] for r in mine if r["bands"] is not None]
    lines.append(f"| {m} | {fmt(d['base']['lufs'])} → {fmt(d['head']['lufs'])} | "
                 f"{delta(d['base']['lufs'], d['head']['lufs'])} ({fmt(min(dl) if dl else None, '{:+.1f}')}..{fmt(max(dl) if dl else None, '{:+.1f}')}) | "
                 f"{fmt(d['head']['peak'])} ({fmt(max(peaks) if peaks else None)}) | "
                 f"{fmt(d['base']['decay40'], '{:.2f}')} → {fmt(d['head']['decay40'], '{:.2f}')} | "
                 f"{fmt(d['base']['centroid'], '{:.0f}')} → {fmt(d['head']['centroid'], '{:.0f}')} | "
                 f"{fmt(d['bands'])} (keys up to {fmt(max(bandmax) if bandmax else None)}) |")
over = [r["name"] for r in rows if r["head"]["peak"] is not None and r["head"]["peak"] > 0.05
        and (r["base"]["peak"] is None or r["base"]["peak"] <= 0.05)]
lines.append("")
lines.append(f"New hits over the ceiling: {', '.join(over) if over else 'none'}.")
report = "\n".join(lines) + "\n"
out.mkdir(parents=True, exist_ok=True)
(out / "report.md").write_text(report)
(out / "report.json").write_text(json.dumps(rows, indent=1))
print(report)
summary = os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary, "a") as s:
        s.write("\n" + report)
