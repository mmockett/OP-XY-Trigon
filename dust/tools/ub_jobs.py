#!/usr/bin/env python3
"""The sweep tools/ub.sh renders with every build: every key of the default kit
and of every preset, every model with each control at 0, 64 and 127, every
control at 0 and at 127 together, TUNE at its ends and TONE at its ends.
Lines: model c0..c7 tune tone. Not part of the machine."""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import presets as P  # noqa: E402

jobs = []
for name, keys in [("default", P.default_kit()), *P.PRESETS.items()]:
    for k in keys:
        jobs.append((k["model"], k["c"], k["tune"], k["tone"]))
models = [[int(x) for x in d.split(",")] for _n, d in re.findall(r'\{"(\w+)", DUST_LABELS, \{([^}]*)\}\}', (ROOT / "kit.h").read_text())]
for m, d in enumerate(models):
    for s in range(8):
        for v in (0, 64, 127):
            c = list(d)
            c[s] = v
            jobs.append((m, c, 0, 0))
    jobs.append((m, [0] * 8, 0, 0))
    jobs.append((m, [127] * 8, 0, 0))
    for tune, tone in ((-16, 0), (15, 0), (0, -8), (0, 7), (-16, -8), (15, 7)):
        jobs.append((m, d, tune, tone))
for m, c, tune, tone in jobs:
    print(m, *c, tune, tone)
