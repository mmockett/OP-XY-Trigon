#!/usr/bin/env python3
"""Every key's level, as tools/calibrate.py measures a model's: LUFS-M (the
loudest 400 ms, K-weighted) and true peak (4x), both against the SDK's
half-scale ceiling, for the default kit and every preset, at each key's own
TUNE and TONE (its LEVEL is a cut on top). With --cuts it prints each preset
key's LEVEL cut, in whole dB, where it sounds more than 0.75 LU over its
role's loudness (kicks and toms -16.5, the rest -19): presets.py's LEVELS.
Not part of the machine.

    python3 -I tools/key_levels.py --sdk <open-xy> [--cxx clang++] [--cuts]
"""
import argparse, pathlib, subprocess, sys, tempfile
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import presets as P  # noqa: E402
import calibrate as C  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--sdk", required=True)
ap.add_argument("--cxx", default="c++")
ap.add_argument("--cuts", action="store_true")
a = ap.parse_args()
scratch = pathlib.Path(tempfile.mkdtemp(prefix="dust-keys-"))
binary = scratch / "render"
subprocess.run([a.cxx, "-std=c++11", "-O2", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/render.cpp"), str(ROOT / "tables.cpp"),
                "-o", str(binary)], check=True)
ROLE = {"KICK": -16.5, "TOM": -16.5}
KEYS = ["BD", "SD", "HT", "MT", "LT", "CP", "RS", "CB", "CH", "OH", "RC", "CC", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76"]
kits = [("default", P.default_kit())] + list(P.PRESETS.items())
lines, jobs = [], []
for name, keys in kits:
    for k, key in enumerate(keys):
        jobs.append((name, k, key))
        lines.append(f"{scratch}/k{len(jobs)}.raw {key['model']} {' '.join(map(str, key['c']))} {key['tune']} {key['tone']} 8 1")
subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
cuts = {}
for i, (name, k, key) in enumerate(jobs):
    x = np.fromfile(f"{scratch}/k{i + 1}.raw", "<i4").astype(np.float64) / C.CEILING
    lufs, tp = C.loudness(x), C.true_peak(x)
    model = P.MODELS[key["model"]]
    target = ROLE.get(model, -19.0)
    over = lufs - target
    if name == "default" or not a.cuts:
        print(f"{name:9s} {53 + k} {KEYS[k]:3s} {model:8s} {lufs:6.1f} LUFS ({over:+.1f})  true peak {tp:5.1f} dB  LEVEL {key['gain']}")
    if name != "default" and over > 0.75:
        cuts.setdefault(name, {})[k] = -int(np.ceil(over - 0.25))
if a.cuts:
    print(cuts)
