#!/usr/bin/env python3
"""The loops of a kit's compiled voice, from `bfin-elf-objdump -d -r -C` of
its voice.o, for CI's blackfin-count job. Not part of any kit.
  python3 -I tools/perf/loops.py <kit> <base listing> <head listing> [--strict]

Per function: its size, its hardware loops (LSETUP, with their bodies'
lengths), its software loops (a conditional jump back, with its span) and
any helper call (libgcc's divides, memset, memcpy) inside a loop, which runs
per iteration. Writes base and head side by side to $GITHUB_STEP_SUMMARY.
Fails when the head calls a helper inside a loop more often than the base
does. With --strict (a commit marked "Sound: identical"), also when a
function in both turns a hardware loop into a software one; a longer longest loop is noted."""
import os, re, sys

HELPERS = re.compile(r"__(u?div|u?mod)si3|memset|memcpy|__mulsi3|__ashldi3|__lshrdi3|__muldi3")


def parse(path):
    funcs, order, name = {}, [], None
    for line in open(path):
        m = re.match(r"^([0-9a-f]+) <(.*)>:$", line)
        if m:
            name = m.group(2)
            funcs[name] = {"ins": [], "rel": []}
            order.append(name)
            continue
        if name is None:
            continue
        m = re.match(r"^\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(\S.*)$", line)
        if m:
            funcs[name]["ins"].append((int(m.group(1), 16), m.group(3)))
            continue
        m = re.match(r"^\s+([0-9a-f]+):\s+R_BFIN_\w+\s+(\S+)", line)
        if m:
            funcs[name]["rel"].append((int(m.group(1), 16), m.group(2)))
    result = {}
    for fn in order:
        ins, rel = funcs[fn]["ins"], funcs[fn]["rel"]
        hw, sw = [], []
        for a, i in ins:
            m = re.match(r"LSETUP\s*\(0x([0-9a-f]+)\s.*?,\s*0x([0-9a-f]+)\s.*?\)\s*(LC[01])", i)
            if m:
                s, e = int(m.group(1), 16), int(m.group(2), 16)
                hw.append((s, e, sum(1 for x, _ in ins if s <= x <= e)))
        for a, i in ins:
            j = re.search(r"JUMP(?:\.S|\.L)?\s+0x([0-9a-f]+)", i)
            if j and int(j.group(1), 16) < a and "IF" in i.upper():
                t = int(j.group(1), 16)
                sw.append((t, a, sum(1 for x, _ in ins if t <= x <= a)))
        inside = 0
        for a, r in rel:
            if HELPERS.search(r) and any(s <= a <= e for s, e, _ in hw + sw):
                inside += 1
        result[re.sub(r"\(.*", "", fn)] = dict(size=len(ins), hw=sorted(n for _, _, n in hw), sw=sorted(n for _, _, n in sw),
                                                inside=inside)
    return result


kit, base_path, head_path = sys.argv[1:4]
strict = "--strict" in sys.argv
base, head = parse(base_path), parse(head_path)
problems, notes = [], []
rows = []
for fn in sorted(set(base) | set(head)):
    b, h = base.get(fn), head.get(fn)
    if not ((b and (b["hw"] or b["sw"])) or (h and (h["hw"] or h["sw"]))):
        continue

    def cell(x):
        if not x:
            return "–"
        s = f"{x['size']} ins; hw {x['hw'] or '–'}; sw {x['sw'] or '–'}"
        return s + (f"; **{x['inside']} helper calls in loops**" if x["inside"] else "")
    rows.append(f"| `{fn}` | {cell(b)} | {cell(h)} |")
    if h and h["inside"] > (b["inside"] if b else 0):
        problems.append(f"{fn}: {h['inside']} helper calls inside loops (base {b['inside'] if b else 0})")
    if strict and b and h:
        if len(h["hw"]) < len(b["hw"]) and len(h["sw"]) > len(b["sw"]):
            problems.append(f"{fn}: a hardware loop became a software loop (hw {len(b['hw'])} -> {len(h['hw'])}, "
                            f"sw {len(b['sw'])} -> {len(h['sw'])})")
        lb, lh = max(b["hw"] + b["sw"] or [0]), max(h["hw"] + h["sw"] or [0])
        if lh > lb:
            notes.append(f"{fn}: longest loop body {lh} instructions, was {lb}")
text = [f"\n#### {kit}: compiled loops (voice.o), base → head\n", "| Function | Base | Head |", "| --- | --- | --- |", *rows, ""]
if problems:
    text += ["**Problems:**", *[f"- {p}" for p in problems], ""]
if notes:
    text += ["Notes:", *[f"- {p}" for p in notes], ""]
print("\n".join(text))
summary = os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary, "a") as s:
        s.write("\n".join(text) + "\n")
sys.exit(1 if problems else 0)
