#!/usr/bin/env python3
"""The jobs tools/perf/host.cpp renders for CI, from a kit folder (the base's,
so base and head play the same jobs). Not part of any kit.
  python3 -I tools/perf/scenarios.py <kit folder> [identity|compare]

identity: the profiler's whole workload (opxy-drum-callback-v1: the kit's
keys at full polyphony, every model at 64, 0 and 127 with the pad controls at
their tops, retriggers and full polyphony of the heaviest), then full
polyphony and retriggers of every model at every level; every key of the
default kit and of every preset; the kit's own sweep (its tools/ub.sh
scenarios); and sweeps of each model's controls (each at 0, 16, .., 112, 127,
the others at the model's defaults) and pad controls (each through its whole
range).
compare: one hit of every key of the default kit and every preset, and of
every model at its defaults and with all eight controls at 0, 64 and 127."""
import importlib.util, pathlib, subprocess, sys

kit = pathlib.Path(sys.argv[1]).resolve()
mode = sys.argv[2] if len(sys.argv) > 2 else "identity"
name = kit.name
# The heaviest model and level the audit counted (OP-XY-Dev media/kit-perf-audit/
# trigon/AUDIT.md); the OP-XY picks its own by measuring, the extra runs cover the rest.
HEAVY = {"dust": ("OPEN", 127), "felt": ("BOW", 64), "flicker": ("ORNAMENT", 127)}
SWEEPS = {"dust": "ub_jobs.py", "felt": "presets.py --scenarios", "flicker": "ub_scenarios.py"}
HIT = 44100 * 4 // 64  # blocks: four seconds, or until the voice finishes

sys.path.insert(0, str(kit / "tools"))
spec = importlib.util.spec_from_file_location("presets", kit / "tools" / "presets.py")
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
pad_names = None


def pads(key):
    global pad_names
    if pad_names is None:
        pad_names = [k for k in ("tune", "tone", "soft", "slit") if k in key]
    return [int(key.get(k, 0)) for k in pad_names]


def hit(label, model, c, p, blocks=HIT):
    return f"hit {label} {model} {' '.join(str(int(x)) for x in c)} {p[0]} {p[1]} {blocks}"


lines = []
keys = [("default", P.default_kit()), *P.PRESETS.items()]
for preset, kk in keys:
    for i, k in enumerate(kk):
        lines.append(hit(f"key:{preset}:{i}:{P.MODELS[k['model']]}", k["model"], k["c"], pads(k)))
if mode == "compare":
    for m, mn in enumerate(P.MODELS):
        d = P.model_defaults(mn)
        lines.append(hit(f"model:{mn}:defaults", m, d, [0, 0]))
        for v in (0, 64, 127):
            lines.append(hit(f"model:{mn}:all{v}", m, [v] * 8, [0, 0]))
    print("\n".join(lines))
    sys.exit(0)

model, level = HEAVY[name]
out = [f"bench {model} {level}"]
for mn in P.MODELS:
    for lv in (64, 0, 127):
        out.append(f"ceiling {mn} {lv}")
        out.append(f"choke {mn} {lv}")
out += lines
script, *args = SWEEPS[name].split()
sweep = subprocess.run([sys.executable, "-I", str(kit / "tools" / script), *args], check=True,
                       capture_output=True, text=True, cwd=kit).stdout.split("\n")
for i, line in enumerate(x for x in sweep if x.strip()):
    f = line.split()
    out.append(hit(f"sweep:{i}", int(f[0]), f[1:9], f[9:11]))
bits = {"dust": ((-16, 15), (-8, 7)), "felt": ((-16, 15), (0, 15)), "flicker": ((-16, 15), (-8, 7))}[name]
for m, mn in enumerate(P.MODELS):
    d = P.model_defaults(mn)
    for c in range(8):
        for v in (0, 16, 32, 48, 64, 80, 96, 112, 127):
            x = list(d)
            x[c] = v
            out.append(hit(f"control:{mn}:{c}:{v}", m, x, [0, 0]))
    for f, (lo, hi) in enumerate(bits):
        for v in range(lo, hi + 1):
            p = [0, 0]
            p[f] = v
            out.append(hit(f"pad:{mn}:{f}:{v}", m, d, p))
print("\n".join(out))
