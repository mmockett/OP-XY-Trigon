#!/usr/bin/env python3
"""A kit's adapter settings and sources, from its definition.json and
machine.json, for tools/perf/bench.h. Not part of any kit.
  python3 tools/perf/kit.py <kit folder> [defines|sources|name]
defines: the -D flags bench.h reads; sources: the kit's extra C++ sources,
one a line, relative to its folder; name: its namespace."""
import json, pathlib, sys

kit = pathlib.Path(sys.argv[1])
what = sys.argv[2] if len(sys.argv) > 2 else "defines"
definition = json.loads((kit / "definition.json").read_text())
machine = json.loads((kit / "machine.json").read_text())
r = definition["resources"]
pads = r.get("padControls", [])
if what == "defines":
    flags = {
        "KIT_NS": kit.resolve().name,
        "KIT_CAPACITY": r["voiceCapacity"],
        "KIT_MODELS": r["modelCount"],
        "KIT_CHOKE": int(bool(r.get("chokeSameKey", 0))),
        "KIT_PADMIX": int(bool(r.get("padMix", 0))),
        "KIT_PAD_COUNT": len(pads),
        "KIT_PAD_BITS": ",".join(str(p["bits"]) for p in pads) or "1",
        "KIT_PAD_SIGNS": sum(1 << i for i, p in enumerate(pads) if p.get("signed")),
    }
    print(" ".join(f"-D{k}={v}" for k, v in flags.items()))
elif what == "sources":
    for s in machine.get("sources", []):
        print(s)
elif what == "name":
    print(kit.resolve().name)
