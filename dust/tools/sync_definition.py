#!/usr/bin/env python3
"""Writes the sizes definition.json declares from the compiled code: the
voice's bytes, the display state's and the artwork's node count. Not part of
the machine.

    python3 tools/sync_definition.py --sdk <open-xy> [--cxx clang++]
"""
import argparse, json, pathlib, re, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("--sdk", required=True)
ap.add_argument("--cxx", default="c++")
a = ap.parse_args()
d = pathlib.Path(tempfile.mkdtemp(prefix="dust-sizes-"))
(d / "sizes.cpp").write_text('#include "engine.cpp"\n#include "display.cpp"\n#include <cstdio>\n'
                             'int main() { std::printf("%u %u\\n", unsigned(sizeof(dust::Voice)), unsigned(sizeof(dust::DisplayState))); }\n')
subprocess.run([a.cxx, "-std=c++11", "-O2", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(d / "sizes.cpp"), str(ROOT / "tables.cpp"), "-o", str(d / "sizes")], check=True)
voice, state = map(int, subprocess.run([str(d / "sizes")], capture_output=True, text=True, check=True).stdout.split())
nodes = int(re.search(r"node_count = (\d+);", (ROOT / "art.h").read_text()).group(1))
p = ROOT / "definition.json"
j = json.loads(p.read_text())
j["resources"]["voiceBytes"] = voice
j["resources"]["display"]["stateBytes"] = state
j["resources"]["display"]["nodes"] = nodes
p.write_text(json.dumps(j, indent=2) + "\n")
print(f"voice {voice} bytes, display state {state}, {nodes} nodes")
