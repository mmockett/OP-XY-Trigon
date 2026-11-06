#!/usr/bin/env python3
"""Writes dust/levels.h: each model's output gain and its level trims, set
by loudness. Renders the real engine on the host. Not part of the machine.

    python3 -I tools/calibrate.py --sdk <open-xy> [--rounds 6] [--only TOM ...]

Loudness here is the hit's short-term loudness: BS.1770 K-weighting, the
loudest 400 ms window (LUFS-M max), measured against the SDK's half-scale
ceiling (0 dB = the most a voice may put out). Every model's default hit is
brought to its role's loudness, kicks and toms at -16.5, everything else at
-19, unless that would take its true peak (4x oversampled, the mean over six
seeds) over -1 dB or its loudest seed's over -0.5 dB: then the peak sets it.

The trims are log2 gains at each control's 0, 32, 64, 96 and 127, with the
other controls at their defaults; the engine adds them up and interpolates.
`loud` holds the hit at the default's loudness as a control turns; `rise` is
how far that lifts its true peak; `room` is how far the default's peak sits
under its limit. The engine takes a hit down by whatever its controls' rises
together pass the room, so a hard beater's click or a combination of
controls is held by its peak, and only as far as it must be. These are
measured with a renderer built without the limiter, the knee and that cap
(a copy of engine.cpp without its lines marked "level:"), so they see the
hit's own peaks; a rise is the loudest seed's.

A clap, a pour, the shackle and the bearing, whose peaks stand far over their
body, run the engine's transient limiter (engine.cpp limit), which may take
up to 3 dB off a peak (4 on the clap's snaps): their room is that much more.
A hard beater's click is held by the limiter, not by the cap: where HELD
names BEATER, its rise doesn't count against the room and its loudness is
held as the limiter leaves it, so turning it changes the stick, and the
body keeps its level. Each run starts from the
levels.h it finds; --only recalibrates the models named and keeps every
other model's rows as they are.
"""
import argparse, os, pathlib, re, subprocess, tempfile
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
SR = 44100
TARGET = {"low": -16.5, "rest": -19.0}
ROLE = {"KICK": "low", "TOM": "low"}
TP_MEAN, TP_MOST = -1.0, -0.5   # true peak, dB re the ceiling: mean over seeds, loudest seed
# A setting held by its peak (a steel rod's click) is held a little lower, its
# loudest seed's true peak at -1.5 dB, so the knee barely touches it: a click
# the knee rounds over overshoots between samples.
CAP_MOST = -1.5
# Models whose peaks stand far over their body get the engine's transient
# limiter (engine.cpp limit), which may take at most this many dB off their
# peaks (on the mean over seeds; a dB more on the loudest seed): a stick's
# crack, a hand's slap, the claps' first hands, a jingle's strike.
LIMIT = {"KICK": 2.0, "SNARE": 2.5, "RIM": 3.0, "HAT": 4.0, "OPEN": 1.5, "RIDE": 2.0, "CRASH": 1.5, "TOM": 2.5, "CONGA": 2.5,
         "BONGO": 2.5, "TAMB": 3.0, "COWBELL": 2.5, "CLAP": 3.0, "SHAKER": 2.0, "BASS": 1.0}
# Controls whose loudness change is their sound: none in DUST.
FREE = set()
# A hard stick's crack held by the transient limiter rather than the cap.
HELD = set()
POINTS = [0, 32, 64, 96, 127]
SEEDS = [1, 2]
CEILING = 2.0 ** 30
N = 15


def kit():
    text = (ROOT / "kit.h").read_text()
    return [(n, [int(x) for x in d.split(",")]) for n, d in re.findall(r'\{"(\w+)", DUST_LABELS, \{([^}]*)\}\}', text)]


def biquad_ir(b, a, n):
    y, x1, x2, y1, y2 = np.zeros(n), 0.0, 0.0, 0.0, 0.0
    for i in range(n):
        xi = 1.0 if i == 0 else 0.0
        yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, xi, y1, yi
        y[i] = yi
    return y


def k_ir():
    """BS.1770's K-weighting at 44.1 kHz (its two stages by bilinear transform)."""
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


def loudness(x):
    """LUFS-M max: the loudest 400 ms of the K-weighted hit, dB re the ceiling."""
    x = np.pad(x, (0, int(0.4 * SR)))
    n = 1 << int(np.ceil(np.log2(len(x) + len(KW))))
    y = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(KW, n), n)[:len(x)]
    c = np.concatenate([[0], np.cumsum(y * y)])
    block = int(0.4 * SR)
    s = np.arange(0, len(y) - block + 1, SR // 100)
    return -0.691 + 10 * np.log10(max(((c[s + block] - c[s]) / block).max(), 1e-20))


def true_peak(x):
    """4x oversampled peak, dB re the ceiling."""
    n = 1 << int(np.ceil(np.log2(len(x) + 256)))
    y = np.fft.irfft(np.fft.rfft(x, n), 4 * n) * 4
    return 20 * np.log10(max(np.max(np.abs(y)), 1e-9))


def table(t):
    return ["    {" + ", ".join("{" + ", ".join(str(int(np.clip(round(v * 32), -127, 127))) for v in t[m][s]) + "}" for s in range(8)) + "},"
            for m in range(N)]


def treble_share(binary, m, c, scratch):
    """The share of a hit's K-weighted energy above 2 kHz (TONE's shelf holds the level by it)."""
    f = f"{scratch}/t.raw"
    subprocess.run([str(binary)], input=f"{f} {m} {' '.join(map(str, c))} 0 0 8 1\n", capture_output=True, text=True, check=True)
    x = np.fromfile(f, "<i4").astype(np.float64) / CEILING
    os.unlink(f)
    n = 1 << int(np.ceil(np.log2(len(x) + len(KW))))
    y = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(KW, n), n)[:len(x)]
    Y = np.abs(np.fft.rfft(y)) ** 2
    hz = np.fft.rfftfreq(len(y), 1 / SR)
    return float(Y[hz > 2000].sum() / max(Y.sum(), 1e-30))


def write(gain, room, loud, rise, treble):
    names = [n for n, _d in kit()]
    q16 = lambda v: ", ".join(str(int(round(g * 65536))) for g in v)
    lines = ["#pragma once", "// Generated by tools/calibrate.py: each model's output gain and level trims.",
             "#include <opxy/types.hpp>", "", "namespace dust {", "namespace levels {",
             "// log2 of each model's output gain, Q16.",
             f"const opxy::i32 gain[{N}] = {{" + q16(gain) + "};",
             "// log2 of the room its default hit leaves under its peak limit, Q16.",
             f"const opxy::i32 room[{N}] = {{" + q16(room) + "};",
             "// The share of each model's default hit's loudness above 2 kHz, Q16 (TONE holds the level by it).",
             f"const opxy::i32 treble[{N}] = {{" + ", ".join(str(int(round(t * 65536))) for t in treble) + "};",
             "// The models the transient limiter runs on (engine.cpp limit).",
             f"const unsigned char limit[{N}] = {{" + ", ".join("1" if n in LIMIT else "0" for n in names) + "};",
             "// log2 trims in 1/32 octave at each control's 0, 32, 64, 96 and 127: the loudness held,",
             f"const signed char loud[{N}][8][5] = {{", *table(loud), "};",
             "// and how far that lifts the hit's true peak.",
             f"const signed char rise[{N}][8][5] = {{", *table(rise), "};", "}", "}", ""]
    (ROOT / "levels.h").write_text("\n".join(lines))


def build(sdk, scratch, *strip):
    """The host renderer. strip names the engine's lines marked "level: <part>" to leave
    out ("limiter", "knee", "cap"), to measure the peaks they take."""
    src = pathlib.Path(scratch) / ("src" + "".join("-" + p for p in strip))
    (src / "tools").mkdir(parents=True, exist_ok=True)
    for f in list(ROOT.glob("*.h")) + list(ROOT.glob("*.cpp")):
        text = f.read_text()
        if f.name == "engine.cpp":
            text = "\n".join(l for l in text.split("\n") if not any(l.rstrip().endswith("// level: " + p) for p in strip))
            if strip:
                # the player's own levels stay where the real gain puts them (the copy only plays down)
                line = "const i32 base = levels::gain[model];"
                assert line in text
                text = text.replace(line, f"const i32 base = levels::gain[model] + {DOWN} * 65536;")
        if f.name == "levels.h" and strip:
            # DOWN octaves quieter, so a click held only by the cap stays under the output's clamp (+6 dB)
            g = re.search(r"gain\[15\] = \{([^}]*)\}", text)
            text = text.replace(g.group(0), "gain[15] = {" + ", ".join(str(int(v) - DOWN * 65536) for v in g.group(1).split(",")) + "}")
        (src / f.name).write_text(text)
    (src / "tools" / "render.cpp").write_text((ROOT / "tools/render.cpp").read_text())
    b = src / "render"
    subprocess.run([CXX, "-std=c++11", "-O2", f"-I{sdk}/sdk/include", f"-I{src}", str(src / "tools/render.cpp"), str(src / "tables.cpp"), "-o", str(b)], check=True)
    return b


CXX = "c++"
DOWN = 2   # octaves the measuring copies without the cap play down (build), given back by measure


def measure(binary, jobs, scratch, down=0):
    """Each job's (loudness, mean true peak, loudest seed's true peak) over the seeds, dB;
    down: octaves the renderer plays down, added back."""
    lines = []
    for i, (m, c) in enumerate(jobs):
        for s in SEEDS:
            lines.append(f"{scratch}/c{i}_{s}.raw {m} {' '.join(map(str, c))} 0 0 8 {s}")
    subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    out = []
    for i in range(len(jobs)):
        ld, tp = [], []
        for s in SEEDS:
            f = f"{scratch}/c{i}_{s}.raw"
            x = np.fromfile(f, "<i4").astype(np.float64) / CEILING
            os.unlink(f)
            ld.append(loudness(x) if len(x) else -120)
            tp.append(true_peak(x) if len(x) else -120)
        up = 6.0206 * down
        out.append((10 * np.log10(np.mean(10 ** (np.array(ld) / 10))) + up, float(np.mean(tp)) + up, float(np.max(tp)) + up))
    return out


def rezero(t, d):
    """No trim at the control's default value itself."""
    at = d / 127 * 4
    i = min(int(at), 3)
    z = t[i] + (t[i + 1] - t[i]) * (at - i)
    return [float(np.clip(v - z, -127 / 32, 127 / 32)) for v in t]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--only", nargs="*", default=None, help="models to recalibrate (default: all)")
    ap.add_argument("--cxx", default="c++", help="the host compiler (run it with clang++ and g++: the results must match)")
    a = ap.parse_args()
    global CXX
    CXX = a.cxx
    scratch = tempfile.mkdtemp(prefix="dust-cal-")
    models = kit()
    text = (ROOT / "levels.h").read_text()
    vec = lambda name: [int(v) / 65536 for v in re.search(name + r"\[15\] = \{([^}]*)\}", text).group(1).split(",")]
    tab = lambda name: [[[int(v) / 32 for v in row] for row in rows[m * 8:(m + 1) * 8]]
                        for rows in [re.findall(r"\{(-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+)\}", text.split(name + "[15][8][5]")[1].split("};")[0])]
                        for m in range(N)]
    gain, room, loud, rise = vec("gain"), vec("room"), tab("loud"), tab("rise")
    treble = vec("treble")
    for r in range(a.rounds):
        binary, bare = build(a.sdk, scratch), build(a.sdk, scratch, "limiter", "knee", "cap")
        unlimited = build(a.sdk, scratch, "limiter", "knee")
        report = []
        for m, (name, d) in enumerate(models):
            if a.only and name not in a.only:
                continue
            jobs = [(m, d)] + [(m, d[:s] + [p] + d[s + 1:]) for s in range(8) for p in POINTS]
            treble[m] = treble_share(binary, m, d, scratch)
            res, off = measure(binary, jobs, scratch), measure(bare, jobs, scratch, DOWN)
            (l0, tp0, most0), (_l, tpo, mosto) = res[0], off[0]
            target, allow = TARGET[ROLE.get(name, "rest")], LIMIT.get(name, 0.0)
            step = min(target - l0, TP_MEAN - tp0, TP_MOST - most0)
            if allow:   # what the limiter takes (the peak without it, less with it) grows dB for dB
                step = min(step, allow - (tpo - tp0), allow + 1 - (mosto - most0))
            step *= 0.9
            gain[m] += step / 6.0206
            # the room the default then leaves its peaks, with the limiter's share
            room[m] = max(0.0, min(TP_MEAN + allow - (tpo + step), CAP_MOST + allow + (1 if allow else 0) - (mosto + step))) / 6.0206
            for s in range(8):
                lo, ri = loud[m][s], rise[m][s]
                for j in range(5):
                    l, _tp, most = off[1 + s * 5 + j]
                    if (name, s) in FREE:
                        change = -lo[j] * 6.0206
                    elif (name, s) in HELD:   # the loudness heard: through the limiter that holds the click
                        change = 0.8 * (res[0][0] - res[1 + s * 5 + j][0])
                    else:
                        change = 0.8 * (off[0][0] - l)
                    lo[j] += change / 6.0206
                    ri[j] = (most + change - mosto) / 6.0206 if (name, s) not in HELD else 0.0   # the loudest seed's peak
                loud[m][s], rise[m][s] = rezero(lo, d[s]), rezero(ri, d[s])
            spread = [res[1 + k][0] - l0 for k in range(40)]
            worst = max(res[1 + k][2] for k in range(40))
            if allow:
                capped = measure(unlimited, jobs, scratch, DOWN)
                taken = f", limiter takes {tpo - tp0:.1f} dB (most in a sweep {max(o[1] - q[1] for o, q in zip(capped, res)):.1f})"
            else:
                taken = ""
            report.append(f"{name} {l0:.1f} LUFS ({l0 - target:+.1f}) tp {tp0:.1f}/{most0:.1f}, "
                          f"sweep {min(spread):+.1f}..{max(spread):+.1f} LU, worst tp {worst:.1f}{taken}")
        write(gain, room, loud, rise, treble)
        print(f"round {r + 1} (before its update):\n  " + "\n  ".join(report), flush=True)


if __name__ == "__main__":
    main()
