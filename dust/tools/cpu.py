#!/usr/bin/env python3
"""DUST's worst audio block per voice, counted in Blackfin instructions.

Two halves. First each part of the voice is compiled alone with the SDK's
pinned Blackfin compiler and flags (a probe calling just that part) and its
instructions counted: its straight code once, each of its per-sample loops
64 times (the room's 16, a pass every four samples; the note's setup's word
clears as many times as the voice has words). Then the real engine runs on
the host through every model at every control's ends and middle, every
control at once at its ends, and the kit's and presets' keys, and each block
reports which parts it ran (Voice::work); a block's count is the sum of its
parts'. The worst block of each model is printed against the 580,352-cycle
block (3.5% is 20,312).

Instructions, not cycles: a loop's every instruction is counted every pass
(branches not taken as taken), and stalls are not. Not part of the machine;
an estimate, not a device measurement.

    python3 tools/cpu.py [--sdk <open-xy>] [--cxx clang++]
"""
import argparse, json, os, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("--sdk", default=os.environ.get("OPEN_XY", str(ROOT.parent.parent / "open-xy")))
ap.add_argument("--cxx", default="c++")
ap.add_argument("--verbose", action="store_true")
a = ap.parse_args()
TC = pathlib.Path(a.sdk) / "local" / "toolchain" / "bin"
S = pathlib.Path(tempfile.mkdtemp(prefix="dust-cpu-"))
FLAGS = ["-mcpu=bf707", "-O2", "-std=gnu++11", "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
         "-fno-use-cxa-atexit", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-fno-common",
         "-fno-tree-loop-distribute-patterns", "-fomit-frame-pointer", "-nostdinc++"]
# each part: (name, the probe's call, trips for its loops)
PARTS = [
    ("begin", "begin(v, s, p);", 700),
    ("strike", "strike_block(v, e, x);", 64),
    ("mode", "mode_pair(v.mode[0], v.mode[1], e, 64, x);", 64),
    ("mode_ringing", "mode_pair(v.mode[0], v.mode[1], e, 0, x);", 64),
    ("layer", "layer_noise(v.layer[0], x, 0, 64, 1, 1);", 64),
    ("grain", "layer_grains(v.layer[0], x, 0, 64, 1, 1);", 64),
    ("metal", "metal_block(v, x, true);", 64),
    ("room", "room_block(v, x);", 8),
    ("chain", "chain_block(v, x, y);", 64),
    ("light", "light_block(v, y);", 64),
    ("heavy", "heavy_block(v, y);", 64),
    ("wear", "wear_block(v, y);", 64),
    ("tone", "tone_block(v, y);", 64),
    ("brush", "brush_block(v, y);", 64),
    ("limit", "i32 g, st, to; limit_gain(v, y, 1 << 30, g, st, to); for (unsigned i = 0; i < frames; ++i) { g = st < 0 ? imax(g + st, to) : imin(g + st, to); y[i] = multiply(y[i], g); }", 64),
    ("knee", "for (unsigned i = 0; i < frames; ++i) y[i] = knee(y[i]);", 64),
    ("render", "render(v, s, 1, y, p, pitch);", 64),
]


def disassemble(k, call):
    src = S / f"probe{k}.cpp"
    src.write_text('#include "engine.cpp"\nnamespace dust {\n'
                   'void probe(Voice& v, Shared& s, i32* e, i32* x, i32* y, const i32* p, u32* pitch) {\n'
                   '    (void)s; (void)e; (void)x; (void)y; (void)p; (void)pitch;\n    ' + call + "\n}\n}\n")
    subprocess.run([str(TC / "bfin-elf-g++"), *FLAGS, "-g", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", "-S", str(src), "-o", str(S / f"p{k}.s")], check=True)
    subprocess.run([str(TC / "bfin-elf-as"), str(S / f"p{k}.s"), "-o", str(S / f"p{k}.o")], check=True)
    text = subprocess.run([str(TC / "bfin-elf-objdump"), "-d", "-l", "-C", str(S / f"p{k}.o")], capture_output=True, text=True, check=True).stdout
    functions, name, where = {}, None, None
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
        if m:
            name = m.group(1)
            functions[name] = []
            continue
        m = re.match(r"^(/\S+):(\d+)", line)
        if m:
            where = (pathlib.Path(m.group(1)).name, int(m.group(2)))
            continue
        m = re.match(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S.*)$", line)
        if m and name:
            functions[name].append((len(functions[name]), int(m.group(1), 16), m.group(2), where))
    return functions


def loops(code):
    """Each loop as (first, last, instructions): hardware loops and conditional jumps back."""
    at = {addr: i for i, addr, _ins, _w in code}
    out = []
    for i, _addr, ins, _w in code:
        m = re.match(r"LSETUP\s*\(0x([0-9a-f]+)\s*<[^>]*>,\s*0x([0-9a-f]+)", ins)
        if m and int(m.group(1), 16) in at and int(m.group(2), 16) in at:
            lo, hi = at[int(m.group(1), 16)], at[int(m.group(2), 16)]
            out.append((lo, hi, hi - lo + 1))
            continue
        m = re.search(r"IF !?CC JUMP(?:\.[SL])?\s+0x([0-9a-f]+)", ins)
        if m and int(m.group(1), 16) in at and at[int(m.group(1), 16)] < i:
            lo = at[int(m.group(1), 16)]
            # a loop over samples walks a pointer; a jump back without one is
            # the compiler's placing of an if's branches
            if any(re.search(r"\+\+\]|--\]|\+= 0x|-= 0x|P\d \+= |I\d \+= ", code[j][2]) for j in range(lo, i + 1)):
                out.append((lo, i, i - lo + 1))
    return out


def function_lines():
    """Each inline function of voice.h and engine.cpp: (file, first line, last line, name)."""
    spans = []
    for f in ("voice.h", "engine.cpp"):
        lines = (ROOT / f).read_text().split("\n")
        starts = [(i + 1, re.match(r"(?:inline|template).*?(\w+)\(", l).group(1)) for i, l in enumerate(lines)
                  if re.match(r"(?:inline|template)[^(]*\w+\(", l)]
        for k, (at, name) in enumerate(starts):
            spans.append((f, at, starts[k + 1][0] - 1 if k + 1 < len(starts) else len(lines), name))
    return spans


SPANS = function_lines()
# where one function holds several copies of a loop (inlined at several
# calls, or unswitched), only one runs a pass: count the largest; where it
# holds several loops that run one after another, count them all
SEQUENTIAL = {"metal_partials", "mode_pair", "mode_one", "strike_block", "room_block", "limit"}
ONCE = {"metal_filters"}


def owner(lines):
    """The function a loop's lines belong to (most of them)."""
    votes = {}
    for f, l in lines:
        for sf, a, b, name in SPANS:
            if sf == f and a <= l <= b:
                votes[name] = votes.get(name, 0) + 1
    return max(votes, key=votes.get) if votes else "?"


def count(functions, trips):
    """The probe's instructions: every dust function it reached, its straight
    code once, its innermost loops by their trips (one source loop's copies
    counted once, at the largest)."""
    total, rows = 0, []
    for fn, code in functions.items():
        if not (fn.startswith("dust::") or fn.startswith("opxy::")):
            continue
        if fn.startswith("dust::drum_defaults") or fn.startswith("dust::model_name") or fn.startswith("dust::parameter_"):
            continue
        ls = loops(code)
        inner = [l for l in ls if not any(o != l and l[0] <= o[0] and o[1] <= l[1] for o in ls)]
        by_owner = {}
        for lo, hi, n in inner:
            lines = [code[j][3] for j in range(lo, hi + 1) if code[j][3] and code[j][3][0] in ("voice.h", "engine.cpp")]
            by_owner.setdefault(owner(lines), []).append(n)
        # metal_filters' loop runs over the six partials once a block
        body = sum((sum(v) if k in SEQUENTIAL else max(v)) * (6 if k in ONCE else trips) for k, v in by_owner.items())
        by_source = {k: (sum(v) if k in SEQUENTIAL else max(v)) for k, v in by_owner.items()}
        total += len(code) + body
        rows.append(f"    {fn[:70]}: {len(code)} straight, loops {by_source}")
    return total, rows


costs = {}
for k, (name, call, trips) in enumerate(PARTS):
    functions = disassemble(k, call)
    n, rows = count(functions, trips)
    if name == "begin":
        # the setup runs once: its straight code, the calls it makes (eight
        # modes, two layers, the metal, the room, the strike), one model's
        # share of the models' setups, and its clears word by word
        straight = {re.sub(r"\(.*", "", f): len(c) for f, c in functions.items()}
        n = (straight.get("dust::begin", 0) + 8 * straight.get("dust::set_mode", 0) + 2 * straight.get("dust::set_layer", 0)
             + straight.get("dust::set_metal", 0) + straight.get("dust::metal_filters", 0) + straight.get("dust::set_room", 0)
             + straight.get("dust::strike", 0) + (straight.get("dust::setup_model", 0) + straight.get("dust::setup_snare", 0)) // 10
             + 3 * 700 + 2 * 128)
    costs[name] = n
    if a.verbose:
        print(f"{name}: {n}")
        print("\n".join(rows))
# the output loop: render's own work less its parts' (render's probe holds them all)
print("parts (Blackfin instructions a call): " + ", ".join(f"{k} {v}" for k, v in costs.items() if k != "render"))

# the host half: which parts each block ran
harness = S / "blocks"
subprocess.run([a.cxx, "-std=c++11", "-O2", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/cpu_blocks.cpp"), str(ROOT / "tables.cpp"),
                "-o", str(harness)], check=True)
jobs = subprocess.run([sys.executable, str(ROOT / "tools/ub_jobs.py")], capture_output=True, text=True, check=True).stdout
out = subprocess.run([str(harness)], input=jobs, capture_output=True, text=True, check=True).stdout
W = ["begin", "strike", "room", "tail", "valve", "quiet", "light", "dark", "heavy", "wear", "tone", "brush", "needle", "limit", "knee", "metal", "split"]
# (dark is now inside heavy and wear; a block both limited and kneed runs one pass: the knee's)
names = re.findall(r'\{"(\w+)", DUST_LABELS', (ROOT / "kit.h").read_text())
# the output loop and render's own block work, as one figure: what render's probe
# counts beyond the largest parts it inlines would double count, so a fixed
# 600 instructions a block covers the output loop (about 8 a sample) and the
# block's own work (rate, envelopes, retuning)
BASE = 600
worst = {}
for line in out.splitlines():
    job, model, work, struck, ringing, noise, grain = map(int, line.split()[:7])
    if work & 1:
        n = costs["begin"]
    else:
        # modes run in pairs (a pair's figure, half of it a mode), and each has
        # its coefficient's work a block (about 60 instructions)
        n = BASE + (struck * costs["mode"] + ringing * costs["mode_ringing"] + 1) // 2 + 60 * (struck + ringing) + noise * costs["layer"] + grain * costs["grain"]
        for bit, part in enumerate(W):
            if part in ("begin", "split") or not work >> bit & 1:
                continue
            if part == "valve" or part == "quiet":
                n += costs["chain"] // (2 if part == "quiet" else 1)
            elif part == "limit" and work >> W.index("knee") & 1:
                n += costs["limit"] // 4   # its gain only; the knee's pass applies it
            elif part == "needle" or part == "tail":
                n += 64 * 3
            elif part in costs:
                n += costs[part]
    if n > worst.get(model, (0, 0))[0]:
        parts = [W[b] for b in range(len(W)) if work >> b & 1]
        worst[model] = (n, job, f"{struck}+{ringing} modes, {noise}+{grain} layers, " + " ".join(parts))
total = 0
for m in sorted(worst):
    n, job, what = worst[m]
    print(f"{names[m]:8s} worst block about {n:6d} instructions, {100 * n / 580352:.2f}%" + (f"  (job {job}: {what})" if a.verbose else ""))
    total = max(total, n)
print(f"the kit's worst block: about {total} instructions, {100 * total / 580352:.2f}% of the 580,352-cycle block")
