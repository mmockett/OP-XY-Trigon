#!/usr/bin/env python3
"""Measures every key of FELT's default kit and of each preset as it plays
(its model, controls and pad controls; its LEVEL applied): its loudness
(LUFS-M, the loudest 400 ms, K-weighted) and true peak (4x), dB re the
SDK's half-scale ceiling, against its role's target (-16.5 for THUD and
TOM, -19 for the rest). With --cuts, the whole-dB LEVEL each key over its
target by more than 0.75 LU needs (for tools/presets.py LEVELS). Not part
of the machine.

    python3 -I tools/keys.py --sdk <open-xy> [--kit attic] [--cuts]
"""
import argparse, pathlib, sys, tempfile
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import presets as P  # noqa: E402
import calibrate as C  # noqa: E402
import demo as D  # noqa: E402

NOTES = ["53 BD", "54 SD", "55 HT", "56 MT", "57 LT", "58 CP", "59 RS", "60 CB", "61 CH", "62 OH", "63 RC", "64 CC",
         "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--kit")
    ap.add_argument("--cuts", action="store_true")
    a = ap.parse_args()
    scratch = tempfile.mkdtemp(prefix="felt-keys-")
    binary = D.build(a.sdk, scratch)
    kits = [("default", P.default_kit()), *P.PRESETS.items()]
    cuts = {}
    for name, keys in kits:
        if a.kit and name != a.kit:
            continue
        xs = D.render(binary, [(k["model"], k["c"], 7, k["tune"], k["soft"]) for k in keys], scratch)
        print(f"== {name}")
        for i, (k, x) in enumerate(zip(keys, xs)):
            model = P.MODELS[k["model"]]
            x = x * 2 * 10 ** (k["gain"] / 20)   # re the half-scale ceiling, at the key's level
            ld, tp = C.loudness(x), C.true_peak(x)
            target = -16.5 if model in ("THUD", "TOM") else -19.0
            over = ld - target
            note = f" over by {over:.1f}" if over > 0.75 else (f" under by {-over:.1f}" if over < -1 else "")
            print(f"  {NOTES[i]:6s} {model:7s} {ld:6.1f} LUFS  tp {tp:5.1f}  level {k['gain']:3d} dB{note}")
            if over > 0.75:
                cuts.setdefault(name, {})[i] = k["gain"] - int(np.ceil(over - 0.75))
    if a.cuts:
        print("cuts:", cuts)


if __name__ == "__main__":
    main()
