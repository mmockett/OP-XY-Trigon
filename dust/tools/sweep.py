#!/usr/bin/env python3
"""Measures what each of DUST's controls does across its travel: a model's
hit with one control at 0, 25, 50, 75 and 100 (raw 0, 32, 64, 96, 127) and
the others at their defaults, reported as its spectral centroid, the time it
takes to fall 40 dB from its peak, its strongest partial over its first
100 ms and its loudness (LUFS-M against the half-scale ceiling). These are
the README's control table's figures. Not part of the machine.

    python3 -I tools/sweep.py --sdk <open-xy> [--cxx clang++] [--model KICK] [--control SPEED]
"""
import argparse, pathlib, re, subprocess, sys, tempfile
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import calibrate as C  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--sdk", required=True)
ap.add_argument("--cxx", default="c++")
ap.add_argument("--model", nargs="*")
ap.add_argument("--control", nargs="*")
a = ap.parse_args()
S = pathlib.Path(tempfile.mkdtemp(prefix="dust-sweep-"))
subprocess.run([a.cxx, "-std=c++11", "-O2", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/render.cpp"), str(ROOT / "tables.cpp"),
                "-o", str(S / "render")], check=True)
LABELS = ["SPEED", "PICK", "LIFT", "WEIGHT", "CUE", "WEAR", "WARP", "DRAG"]
models = [(n, [int(x) for x in d.split(",")]) for n, d in re.findall(r'\{"(\w+)", DUST_LABELS, \{([^}]*)\}\}', (ROOT / "kit.h").read_text())]
SR = 44100


def measure(m, c):
    f = S / "x.raw"
    subprocess.run([str(S / "render")], input=f"{f} {m} {' '.join(map(str, c))} 0 0 8 1\n", capture_output=True, text=True, check=True)
    x = np.fromfile(f, "<i4").astype(np.float64) / C.CEILING
    X = np.abs(np.fft.rfft(x, 1 << 18)) ** 2
    hz = np.fft.rfftfreq(1 << 18, 1 / SR)
    centroid = (X * hz).sum() / max(X.sum(), 1e-30)
    env = np.sqrt(np.convolve(x * x, np.ones(441) / 441, "same"))
    top = env.max()
    fall = (len(env) - np.argmax(env[::-1] > top * 0.01)) / SR
    seg = x[:4410] * np.hanning(min(4410, len(x))) if len(x) >= 4410 else x
    Y = np.abs(np.fft.rfft(seg, 1 << 16))
    f0 = np.fft.rfftfreq(1 << 16, 1 / SR)[np.argmax(Y[3:]) + 3]
    return centroid, fall, f0, C.loudness(x)


for n, (name, d) in enumerate(models):
    if a.model and name not in a.model:
        continue
    for s, label in enumerate(LABELS):
        if a.control and label not in a.control:
            continue
        rows = []
        for v in (0, 32, 64, 96, 127):
            c = list(d)
            c[s] = v
            rows.append(measure(n, c))
        print(f"{name} {label}: centroid " + " ".join(f"{r[0]:.0f}" for r in rows) + " Hz; -40 dB at " +
              " ".join(f"{r[1]:.2f}" for r in rows) + " s; strongest " + " ".join(f"{r[2]:.0f}" for r in rows) +
              " Hz; " + " ".join(f"{r[3]:.1f}" for r in rows) + " LUFS")
