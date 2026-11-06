#!/usr/bin/env python3
"""The scenarios tools/ub.sh plays through the engine and the screen, one a
line: model c0..c7 tune slit. Every preset's every key (the init kit's too);
every model at its defaults, with each control at 0, 64 and 127, with every
control at 0 and at 127; and the pad controls at their ends. Not part of the
machine."""
import pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import presets  # noqa: E402

lines = set()
out = []


def add(m, c, tune=0, slit=0):
    line = f"{m} {' '.join(map(str, c))} {tune} {slit}"
    if line not in lines:
        lines.add(line)
        out.append(line)


for name, keys in [("default", presets.default_kit()), *presets.PRESETS.items()]:
    for k in keys:
        add(k["model"], k["c"], k["tune"], k["slit"])
for m, name in enumerate(presets.MODELS):
    d = presets.model_defaults(name)
    add(m, d)
    for s in range(8):
        for v in (0, 64, 127):
            c = list(d)
            c[s] = v
            add(m, c)
    add(m, [0] * 8)
    add(m, [127] * 8)
    for tune, slit in ((-16, 0), (15, 0), (0, -8), (0, 7), (-16, 7), (15, -8)):
        add(m, d, tune, slit)
        add(m, [127] * 8, tune, slit)
print("\n".join(out))
