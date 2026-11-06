#!/usr/bin/env python3
"""Sets definition.json's voiceBytes and display stateBytes to the compiled
types' sizes (the SDK's build fails when they differ), and its eight
parameters' defaults to the first model's (kit.h), as the Tetragon kits'
are. Not part of the machine.

    python3 -I tools/sync_definition.py --sdk <open-xy>
"""
import argparse, json, pathlib, re, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = r'''#include "engine.cpp"
#include "display.cpp"
#include <cstdio>
int main() { std::printf("%u %u\n", (unsigned)sizeof(felt::Voice), (unsigned)sizeof(felt::DisplayState)); return 0; }
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    a = ap.parse_args()
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="felt-sizes-"))
    (scratch / "probe.cpp").write_text(PROBE)
    subprocess.run(["c++", "-std=c++11", "-O0", f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(scratch / "probe.cpp"), str(ROOT / "tables.cpp"),
                    "-o", str(scratch / "probe")], check=True)
    voice, state = map(int, subprocess.run([str(scratch / "probe")], capture_output=True, text=True, check=True).stdout.split())
    path = ROOT / "definition.json"
    d = json.loads(path.read_text())
    d["resources"]["voiceBytes"] = voice
    d["resources"]["display"]["stateBytes"] = state
    first = re.search(r'\{"\w+", \{[^}]*\}, \{([^}]*)\}\}', (ROOT / "kit.h").read_text()).group(1)
    for p, v in zip(d["parameters"], (int(x) for x in first.split(","))):
        p["default"] = round(v / 127, 6)
    path.write_text(json.dumps(d, indent=2) + "\n")
    print(f"voiceBytes {voice}, stateBytes {state}")


if __name__ == "__main__":
    main()
