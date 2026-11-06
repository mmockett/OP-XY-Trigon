#!/usr/bin/env python3
"""Tables the Blackfin simulator's counts of the profiler's drum workload
(tools/perf/sim.cpp), base against head, for CI's blackfin-count job. Not
part of any kit.
  python3 -I tools/perf/simreport.py <kit> <base counts> <head counts> [--strict] [--json out.json]

Each run's mean and worst block as a share of the OP-XY's 580,352-cycle
block. The simulator counts an instruction a cycle plus its fixed branch,
call and multiply delays, and no stalls: on Tetragon the OP-XY ran 1.2-1.9x
such counts. Writes the table to $GITHUB_STEP_SUMMARY. With --strict (a
commit marked "Sound: identical"), fails when any run's mean grows by more
than 0.5%."""
import json, os, sys

BLOCK = 580352
kit, bp, hp = sys.argv[1:4]
strict = "--strict" in sys.argv


def read(path):
    runs = {}
    if not os.path.exists(path):
        return runs
    for line in open(path):
        f = line.split()
        if len(f) == 8 and f[0] == "run":
            total = int(f[4]) + (int(f[5]) << 32)
            runs[int(f[1])] = dict(label=f[2], voices=int(f[3]), mean=total / 1500, worst=int(f[6]))
    return runs


base, head = read(bp), read(hp)


def pct(x):
    return f"{100 * x / BLOCK:.2f}"


def summary(runs):
    singles = [r for i, r in runs.items() if r["voices"] == 1]
    heavy = max(singles, key=lambda r: r["mean"]) if singles else None
    kitrun = runs.get(0)
    ceiling = runs.get(max(runs)) if runs else None
    return heavy, kitrun, ceiling


lines = [f"\n#### {kit}: Blackfin simulator counts, % of the block (mean / worst block), base → head\n"]
hb, kb, cb = summary(base)
hh, kh, ch = summary(head)


def pair(a, b):
    left = f"{pct(a['mean'])} / {pct(a['worst'])}" if a else "–"
    right = f"{pct(b['mean'])} / {pct(b['worst'])}" if b else "–"
    return f"{left} → {right}"


lines += ["| | Base → head |", "| --- | --- |",
          f"| Heaviest single voice ({hb['label'] if hb else '–'} → {hh['label'] if hh else '–'}) | {pair(hb, hh)} |",
          f"| The kit's keys, {kh['voices'] if kh else '?'} voices | {pair(kb, kh)} |",
          f"| Full polyphony of the heaviest ({cb['label'] if cb else '–'} → {ch['label'] if ch else '–'}) | {pair(cb, ch)} |", "",
          "<details><summary>Every run</summary>\n", "| Run | Voices | Base mean / worst | Head mean / worst | Change |", "| --- | --- | --- | --- | --- |"]
problems = []
for i in sorted(set(base) | set(head)):
    b, h = base.get(i), head.get(i)
    label = (h or b)["label"]
    change = f"{100 * (h['mean'] / b['mean'] - 1):+.1f}%" if b and h and b["mean"] else "–"
    lines.append(f"| {i} {label} | {(h or b)['voices']} | {pct(b['mean']) + ' / ' + pct(b['worst']) if b else '–'} | "
                 f"{pct(h['mean']) + ' / ' + pct(h['worst']) if h else '–'} | {change} |")
    if strict and b and h and h["mean"] > b["mean"] * 1.005:
        problems.append(f"run {i} {label}: mean {h['mean']:.0f} cycles, was {b['mean']:.0f}")
lines += ["", "</details>", ""]
if problems:
    lines += ["**Slower in a commit marked Sound: identical:**", *[f"- {p}" for p in problems], ""]
text = "\n".join(lines)
print(text)
if "--json" in sys.argv:
    json.dump(dict(base=base, head=head), open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)
s = os.environ.get("GITHUB_STEP_SUMMARY")
if s:
    with open(s, "a") as f:
        f.write(text + "\n")
sys.exit(1 if problems else 0)
