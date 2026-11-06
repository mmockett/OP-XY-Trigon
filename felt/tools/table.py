#!/usr/bin/env python3
"""Measures every model's eight controls at 0, 25, 50, 75 and 100 percent of
their travel, the others at their defaults, for the README's controls table:
the caption the screen shows, the spectral centroid (Hz), the time to fall
40 dB (s) and the loudness (LUFS-M re the ceiling). Not part of the machine.

    python3 -I tools/table.py --sdk <open-xy> [--model THUD]
"""
import argparse, pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sweep as W  # noqa: E402
import calibrate as C  # noqa: E402

AT = [0, 32, 64, 95, 127]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--model")
    a = ap.parse_args()
    scratch = tempfile.mkdtemp(prefix="felt-table-")
    binary = W.build(a.sdk, scratch)
    cap = pathlib.Path(scratch) / "captions"
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/captions.cpp"),
                    str(ROOT / "tables.cpp"), "-o", str(cap)], check=True)
    captions = {}
    for line in subprocess.run([str(cap)], capture_output=True, text=True, check=True).stdout.split("\n"):
        if line:
            f = line.split()
            captions[(f[0], f[1])] = f[2:]
    for mi, (name, labels, dflt) in enumerate(W.kit()):
        if a.model and name != a.model:
            continue
        print(f"== {name}")
        for s in range(8):
            jobs = []
            for v in AT:
                c = list(dflt)
                c[s] = v
                jobs.append((mi, c, 0, 0, 6))
            xs = W.render(binary, jobs, scratch)
            ms = [W.measure(x) for x in xs]
            loud = [C.loudness(x * 2) for x in xs]
            caps = captions[(name.lower() if name.lower() in [k[0] for k in captions] else name.lower(), labels[s])] if (name.lower(), labels[s]) in captions else []
            print(f"  {labels[s]:8s} {' '.join(caps):32s} cen {' '.join(f'{m['cen']:.0f}' for m in ms):28s} "
                  f"t40 {' '.join(f'{m['t40']:.2f}' for m in ms):28s} LUFS {' '.join(f'{l:.1f}' for l in loud)}")


if __name__ == "__main__":
    main()
