#!/usr/bin/env python3
"""A count of FELT's worst audio block per voice, in Blackfin instructions,
from the compiled code: it builds the module with the SDK's pinned compiler
(sdk.py compile), disassembles the voice, and measures each of the engine's
countable functions (its mode loops, its per-sample stages, its note-on
set-up: engine.cpp keeps each out of line for this). A host build of the
same engine traced block by block (tools/cpu_trace.cpp) says which of them
each block calls; their sum, plus the render's own work, is that block's
count. Every model is traced at its defaults, all 0, all 64 and all 127,
with both pad controls at their tops.

An estimate, and a generous one: a set-up function counts every instruction
it holds once a call (add_string its loop once a partial), as though every
branch were taken; a hardware loop counts its body 64 times. It leaves out pipeline
stalls and memory waits; only `sdk.py profile` on an OP-XY measures cycles.

    python3 -I tools/cpu.py --sdk <open-xy> [--out <scratch folder>]
"""
import argparse, os, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PY = os.environ.get("PY", sys.executable)   # a Python with the SDK's requirements
BLOCK = 580352
NAMES = ["THUD", "RATTLE", "TOM", "LID", "TACK", "BELL", "BRUSH", "PLANK", "CHAIN", "BOLT", "PLUCK", "BOW"]


def functions(text):
    funcs, name = {}, None
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
        if m:
            name = m.group(1)
            funcs[name] = []
            continue
        m = re.match(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S.*)$", line)
        if name and m:
            funcs[name].append((int(m.group(1), 16), m.group(2)))
    return funcs


def loops(code):
    out = []
    for _a, ins in code:
        m = re.match(r"LSETUP\(0x([0-9a-f]+) .*?, 0x([0-9a-f]+) .*?\) (LC[01])", ins)
        if m:
            s, e = int(m.group(1), 16), int(m.group(2), 16)
            out.append(sum(1 for x, _i in code if s <= x <= e))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--out", default=tempfile.mkdtemp(prefix="felt-cpu-"))
    ap.add_argument("--cxx", default="clang++")
    ap.add_argument("--detail", action="store_true", help="what each worst block is made of")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    subprocess.run([str(PY), "sdk.py", "compile", "--machine", str(ROOT), "--out", str(out / "module"), "--overwrite", "--profiling", "off"],
                   cwd=a.sdk, check=True, capture_output=True)
    objdump = pathlib.Path(a.sdk) / "local" / "toolchain" / "bin" / "bfin-elf-objdump"
    text = subprocess.run([str(objdump), "-d", "-C", str(out / "module" / "felt" / "voice.o")], capture_output=True, text=True, check=True).stdout
    funcs = functions(text)
    size, body, span = {}, {}, {}
    for fn, code in funcs.items():
        m = re.match(r"(?:felt::)?(\w+)", fn)
        key = m.group(1)
        if key in size:
            continue
        size[key], body[key] = len(code), loops(code)
        # a software loop: the instructions between its widest backward branch's target and itself
        widest = 0
        for x, ins in code:
            j = re.search(r"JUMP(?:\.S|\.L)? 0x([0-9a-f]+)", ins)
            if j and int(j.group(1), 16) < x:
                widest = max(widest, sum(1 for y, _i in code if int(j.group(1), 16) <= y <= x))
        span[key] = widest
    render = next(k for k in size if k.startswith("render_voice"))

    def cost(name, n, calls, blk):
        s, ls = size[name], body[name]
        if name.startswith("run_") or name.startswith("stage_"):
            return calls * (s - sum(ls) + 64 * sum(ls))
        if name == "add_string":   # its loop over the partials (the widest backward branch), once a partial
            return s - span[name] + span[name] * max(1, blk["partials"])
        if name == "strike":
            return calls * (s - sum(ls) + sum(ls) * n["n"])
        if name == "bend_modes":
            return calls * (s - sum(ls) + sum(ls) * n["n"])
        return calls * s

    # the render's own work (the SDK's wrapper, engine.cpp render and
    # begin), from its loops: the output gain and clamp, the limiter's peak
    # and ramp, a note's word-wise clear and its eight trims and controls;
    # and block_model's own switch and buffer clears
    rl = body[render]
    gain_loop = max(l for l in rl if l < 30)
    own = 350 + 64 * gain_loop + 64 * (4 + 7)          # every block (the limiter's loops counted always)
    start = 940 // 4 + 9 * max(rl) + 8 * 24             # a note's first block: the clear, the trims, the controls
    model_own = 300 + 3 * 64
    trace = pathlib.Path(out) / "trace"
    subprocess.run([a.cxx, "-std=c++11", "-O2", "-finstrument-functions-after-inlining", f"-I{a.sdk}/sdk/include", f"-I{ROOT}",
                    str(ROOT / "tools/cpu_trace.cpp"), str(ROOT / "tables.cpp"), "-o", str(trace)], check=True)
    rows = subprocess.run([str(trace)], capture_output=True, text=True, check=True).stdout.split("\n")
    worst = {}
    for row in rows:
        if not row:
            continue
        f = row.split()
        m, setting, blk = int(f[0]), int(f[1]), int(f[2])
        n = {k: int(v) for k, v in (x.split("=") for x in f[3:7])}
        total = own + model_own + (start if blk == 0 else 0)
        parts = []
        for item in f[7:]:
            k, v = item.split("=")
            c = cost(k, n, int(v), n)
            total += c
            parts.append(f"{k} {c}")
        key = (m, setting)
        if total > worst.get(key, (0, 0))[0]:
            worst[key] = (total, blk, parts)
    print("Blackfin instructions in a voice's worst block (counted from the compiled code; an estimate)")
    print(f"{'model':8s} {'defaults':>9s} {'all 0':>8s} {'all 64':>8s} {'all 127':>8s}   worst, share of the 580,352-cycle block")
    top = 0
    for m, name in enumerate(NAMES):
        r = {s: worst[(m, s)][0] for s in (-1, 0, 64, 127)}
        w = max(r.values())
        top = max(top, w)
        print(f"{name:8s} {r[-1]:9,d} {r[0]:8,d} {r[64]:8,d} {r[127]:8,d}   {w:,d}  {100 * w / BLOCK:.2f}%")
    print(f"worst {top:,d} instructions, {100 * top / BLOCK:.2f}% of a block")
    if a.detail:
        for (m, setting), (total, blk, parts) in sorted(worst.items()):
            print(f"  {NAMES[m]} {setting}: block {blk}, {total:,d}: render {own + model_own + (start if blk == 0 else 0)}, " + ", ".join(parts))
    print("per call: " + ", ".join(f"{k} {size[k] - sum(body[k]) + 64 * sum(body[k])}" for k in ("run_free", "run_driven", "run_free_high", "run_driven_high")))


if __name__ == "__main__":
    main()
