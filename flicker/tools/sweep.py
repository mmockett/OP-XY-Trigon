#!/usr/bin/env python3
"""Measures each of FLICKER's controls across its travel, from the real
engine and the real screen code on the host: for every model and control, at
0, 25, 50, 75 and 100 (raw 0, 32, 64, 95, 127) with the other controls at the
model's defaults, the caption the header shows, the hit's loudness (LUFS-M
against the half-scale ceiling, after the level trims), its spectral
centroid and the time it takes to fall 40 dB. Not part of the machine.

    python3 -I tools/sweep.py --sdk <open-xy> [--only HATCH] [--markdown]
"""
import argparse, json, os, pathlib, subprocess, sys, tempfile
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import presets as P  # noqa: E402
import calibrate as C  # noqa: E402

SR = 44100
STEPS = [0, 32, 64, 95, 127]


def build(sdk, scratch, name):
    b = pathlib.Path(scratch) / name
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{sdk}/sdk/include", f"-I{ROOT}", str(ROOT / f"tools/{name}.cpp"), "-o", str(b)], check=True)
    return b


def sounds(binary, jobs, scratch):
    lines = [f"{scratch}/s{i}.raw {m} {' '.join(map(str, c))} 0 0 8 3" for i, (m, c) in enumerate(jobs)]
    subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    out = []
    for i in range(len(jobs)):
        f = f"{scratch}/s{i}.raw"
        out.append(np.fromfile(f, "<i4").astype(np.float64) / 2 ** 30)
        os.unlink(f)
    return out


def captions(binary, jobs):
    """The header's value for each job's control (the row it is on)."""
    lines = []
    for m, c, s in jobs:
        lines.append(" ".join(map(str, [m, *c, s // 4, 0, 0, 0, 0, 0, 0, 0, -1])))
    out = subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout.split("\n")
    caps = []
    for (m, c, s), line in zip(jobs, out):
        texts = json.loads(line)["texts"]
        caps.append(texts[4 + s % 4][3])   # the bright row's values are text slots 4..7
    return caps


def centroid(x):
    if not len(x):
        return 0
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 17)) ** 2
    f = np.fft.rfftfreq(1 << 17, 1 / SR)
    return float((X * f).sum() / max(X.sum(), 1e-30))


def length(x):
    if not len(x) or np.abs(x).max() == 0:
        return 0
    env = np.convolve(np.abs(x), np.ones(441) / 441, "same")
    idx = np.where(env > env.max() * 0.01)[0]
    return idx[-1] / SR


def fmt_hz(v):
    return f"{v / 1000:.1f}k" if v >= 1000 else f"{v:.0f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--only", default=None)
    a = ap.parse_args()
    scratch = tempfile.mkdtemp(prefix="flicker-sweep-")
    render, frames = build(a.sdk, scratch, "render"), build(a.sdk, scratch, "frames")
    for m, name in enumerate(P.MODELS):
        if a.only and name != a.only:
            continue
        d = P.model_defaults(name)
        labels = __import__("re").findall(r'\{"' + name + r'", \{([^}]*)\}', (ROOT / "kit.h").read_text())[0]
        labels = [l.strip().strip('"') for l in labels.split(",")]
        jobs = [(m, d[:s] + [v] + d[s + 1:], s) for s in range(8) for v in STEPS]
        caps = captions(frames, jobs)
        xs = sounds(render, [(j[0], j[1]) for j in jobs], scratch)
        base = C.loudness(sounds(render, [(m, d)], scratch)[0])
        print(f"{name}: default {base:.1f} LUFS")
        for s in range(8):
            row = []
            for k in range(5):
                x = xs[s * 5 + k]
                row.append(f"{caps[s * 5 + k]:>6s} {C.loudness(x) - base:+5.1f}LU {fmt_hz(centroid(x)):>5s}Hz {length(x):4.2f}s")
            print(f"  {labels[s]:8s} " + " | ".join(row))


if __name__ == "__main__":
    main()
