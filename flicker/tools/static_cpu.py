#!/usr/bin/env python3
"""A static count of FLICKER's per-voice cost on the Blackfin, the worst block.

Builds the module with the SDK's pinned compiler (sdk.py compile), disassembles
the voice code with the pinned objdump and counts, for each drawing
(models.h's draw_<model>, each a function of its own) and for the render
around it (the projector: the ink, the dust, the slit, the valve, the
capacitor, the limiter and the knee), the instructions in every loop body
(a hardware loop, LSETUP, or a backward jump) times its trip count (64, a
block's samples; a loop nested in another, times both), and every other
instruction once. Every branch inside a loop counts as taken and every path
as run, so this is an upper bound on the instructions of the worst block,
every part of the drawing sounding at once. A Blackfin instruction (a
parallel issue counts once) takes at least a cycle; multiplies of 32 by 32
bits (R0 *= R1) take several more, counted here as 5, and the pipeline's
stalls are not counted.

    python3 -I tools/static_cpu.py [--sdk <open-xy>] [--keep <scratch folder>]

A count, not a measurement: only `sdk.py profile` on an OP-XY gives the
figure a catalog record needs.
"""
import argparse, os, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PY = os.environ.get("PY", sys.executable)   # a Python with the SDK's requirements
BLOCK = 580352
ap = argparse.ArgumentParser()
ap.add_argument("--sdk", default=os.environ.get("OPEN_XY", str(ROOT.parent.parent / "open-xy")))
ap.add_argument("--keep", default=None)
a = ap.parse_args()
out = pathlib.Path(a.keep or tempfile.mkdtemp(prefix="flicker-cpu-")) / "module"
subprocess.run([PY, "sdk.py", "compile", "--machine", str(ROOT), "--out", str(out), "--overwrite", "--profiling", "off"],
               cwd=a.sdk, check=True, capture_output=True)
objdump = pathlib.Path(a.sdk) / "local" / "toolchain" / "bin" / "bfin-elf-objdump"
text = subprocess.run([str(objdump), "-d", "-C", "-r", str(out / "flicker" / "voice.o")], capture_output=True, text=True, check=True).stdout

functions, calls, name = {}, {}, None
for line in text.splitlines():
    m = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
    if m:
        name = re.sub(r"\(.*", "", m.group(1)).replace("flicker::", "")
        functions[name] = []
        calls[name] = []
        continue
    m = re.match(r"^\s+([0-9a-f]+): (R_BFIN_PCREL24) (\S+)", line)
    if name and m and functions[name] and functions[name][-1][1].startswith("CALL"):
        calls[name].append((functions[name][-1][0], re.sub(r"\(.*", "", m.group(3)).replace("flicker::", "")))
        continue
    m = re.match(r"^\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(\S.*)$", line)
    if name and m:
        functions[name].append((int(m.group(1), 16), m.group(3)))


def cost(ins):
    """An instruction's cycles at least: a 32 x 32 multiply takes 5."""
    return 5 if re.search(r"R\d \*= R\d", ins) else 1


def loops_only(code, exclusive=False):
    """Only the function's hardware loops, each 64 times (the per-sample work)."""
    bodies = []
    for addr, ins in code:
        m = re.match(r"LSETUP\s*\(0x([0-9a-f]+)\s.*?,\s*0x([0-9a-f]+)\s", ins)
        if m:
            a, b = int(m.group(1), 16), int(m.group(2), 16)
            bodies.append(sum(cost(i) for x, i in code if a <= x <= b))
    if not bodies:
        return 0
    return 64 * (max(bodies) if exclusive else sum(bodies))


def count(code, trips_back, exclusive=False):
    """The function's instructions in its worst block: those in a hardware
    loop (LSETUP: the per-sample loops) 64 times; those in a tight loop closed
    by a backward conditional jump (at most 16 instructions: a search, a loop
    with an early exit) trips_back times; every other instruction once, so
    every branch counts as taken and every case of a switch as run."""
    lsetup, tight = [], []
    for addr, ins in code:
        m = re.match(r"LSETUP\s*\(0x([0-9a-f]+)\s.*?,\s*0x([0-9a-f]+)\s", ins)
        if m:
            lsetup.append((int(m.group(1), 16), int(m.group(2), 16)))
    for addr, ins in code:
        m = re.search(r"IF !?CC JUMP(?:\.[SL])?\s+0x([0-9a-f]+)", ins)
        if m and int(m.group(1), 16) < addr and not any(a <= addr <= b for a, b in lsetup):
            t0 = int(m.group(1), 16)
            if len([1 for x, _ in code if t0 <= x <= addr]) <= 16:
                tight.append((t0, addr))
    total = 0
    for addr, ins in code:
        n = 64 if any(a <= addr <= b for a, b in lsetup) else trips_back if any(a <= addr <= b for a, b in tight) else 1
        total += cost(ins) * n
    if exclusive:
        # only one of the hardware loops runs a block (inked: the ink bleeding
        # or not): the others' 63 further passes come off
        bodies = [sum(cost(i) for x, i in code if a <= x <= b) for a, b in lsetup]
        total -= 63 * (sum(bodies) - max(bodies))
    return total, len(lsetup)


SEEN = {}


def full(name, trips_back):
    """A function's count with every function it calls counted in, once a call
    (a call within a loop as many times as the loop runs)."""
    if name in SEEN:
        return SEEN[name]
    SEEN[name] = 0
    code = functions.get(name, [])
    total, _ = count(code, trips_back)
    for addr, callee in calls.get(name, []):
        if callee in functions:
            total += full(callee, 16)
    SEEN[name] = total
    return total


rows = {}
for short, code in functions.items():
    if short.startswith("draw_"):
        rows[short] = (full(short, 64), 0)
    elif short.startswith("render_voice") or short in ("inked", "projected", "dust", "limit", "kneed"):
        rows[short] = (count(code, 64, short == "inked")[0], 0)
    elif short.startswith("begin") or short.startswith("setup_"):
        rows[short] = (full(short, 16), 0)
parts = {k: rows.pop(k)[0] for k in [k for k in rows if k.startswith("render_voice") or k in ("inked", "projected", "dust", "limit", "kneed")]}
render = sum(parts.values())
start = rows.pop([k for k in rows if k.startswith("begin")][0])[0]
print("Static count of Blackfin instructions a voice's worst block (an upper bound; stalls not counted)")
print(f"the projector around every drawing (ink, dust, slit, valve, capacitor, limiter, knee) and the SDK's voice")
print(f"adapter (render_voice: pan, level, sends, the track filter's hooks): {render:,} a block")
print("    " + ", ".join(f"{k.split('.')[0]} {v:,}" for k, v in parts.items()))
print(f"a note's start, in its first block (begin, searches 16 times round): {start:,}, and the model's own setup")
render += start
print(f"{'model':9s} {'drawing':>8s} {'+render':>8s}  share of a {BLOCK:,}-cycle block (drawing: its block and its setup)")
worst = 0
limited = [int(v) for v in re.search(r"limit\[12\] = \{([^}]*)\}", (ROOT / "levels.h").read_text()).group(1).split(",")]
hot_render = sum(loops_only(functions[k], k == "inked") for k in functions
                 if k.startswith("render_voice") or k in ("inked", "projected", "dust", "kneed"))
hot_limit = loops_only(functions["limit"]) if "limit" in functions else 0
print(f"the same block's per-sample loops alone (projector, knee, dust, the SDK's voice): {hot_render:,}, the limiter's {hot_limit:,}")
hot = {}
for mi, name in enumerate(("sweep", "scratch", "ring", "strokes", "dot", "card", "hatch", "ornament", "stipple", "boop", "wave", "claw")):
    n, loops = rows[f"draw_{name}"]
    n += rows[f"setup_{name}"][0]
    t = n + render - (0 if limited[mi] else parts.get("limit", 0))
    # the hot path: the drawing's and the projector's per-sample loops, its
    # setup and the note's start counted whole, nothing else
    hot[name] = loops_only(functions[f"draw_{name}"]) + rows[f"setup_{name}"][0] + start + hot_render + (hot_limit if limited[mi] else 0)
    worst = max(worst, t)
    print(f"{name.upper():9s} {n:8,} {t:8,}  {t / BLOCK * 100:.2f}%")
print(f"worst {worst:,} instructions, {worst / BLOCK * 100:.2f}% of a block")
print("per-sample loops, a note's start and its setup only (the hot path; an estimate below the bound):")
print("  " + ", ".join(f"{k.upper()} {v:,} ({v / BLOCK * 100:.2f}%)" for k, v in hot.items()))
