#!/usr/bin/env python3
"""DUST's kit presets, and the files a build installs for them.

    python3 tools/presets.py --out <build>/candidate     # content/presets/dust/*.json
    python3 tools/presets.py --check                     # each region decodes to its key's sound

A kit preset holds the 24 keys (53..76) as the drum kind stores them, the way
MNMD's tools/build_firmware.py writes MD KIT's: each key a region whose four
params carry its eight 7-bit controls and its model (the SDK's
drum_storage.hpp), marked "fm slap" (Drumline type 7, the SDK's mark), its
choke group as the play mode ("mute1" is group 1) and its own pan and level
(gain, in dB). The SDK's machine presets folder refuses regions, so these are
written into a build's content/presets/dust/ after it is built, as MNMD's
are. default.json is the kit drum_defaults gives (kit.h).

The pad controls (TUNE, TONE) and the per-key sends live in bits 16..30 of
stock's pad word +0x14 (the SDK's drum_storage.hpp): TUNE (5 bits, signed)
from bit 16, TONE (4 bits, signed) from 21, FX I (3) from 25, FX II (3) from
28. Stock's preset writer exports that word's top half as the region's
"random" and its reader restores it shifted back, as max(random << 16, 0);
so a region carries them as random, with its top bit (the word's bit 31)
clear: TUNE in random's bits 0..4, TONE 5..8, FX I 9..11, FX II 12..14.
Every region writes "random" explicitly, 0 when the key's pad fields are
neutral.

Each send is TRK (the track's send) or the key's own 0..100 in 1% steps
(SDK 1.15): its code above is the nearest of seven steps, and random's bits
16..22 (FX I) and 23..29 (FX II) carry the amount itself (send_code, below).
"""
import argparse, json, math, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODELS = ["KICK", "SNARE", "RIM", "HAT", "OPEN", "RIDE", "CRASH", "TOM", "CONGA", "BONGO", "TAMB", "COWBELL", "CLAP", "SHAKER", "BASS"]

# Each key's own FX I and FX II sends (SDK 1.15, etcetc000/open-xy#518): TRK,
# which follows the track's send, or the key's own amount, 0..100 in 1% steps.
# A region's "random" carries a send twice: its 3-bit code (FX I's at bits
# 9..11, FX II's at 12..14), 0 for TRK or 1..7 for the nearest of the seven
# steps firmware before SDK 1.15 plays (STEPS); and its amount + 1 (FX I's at
# bits 16..22, FX II's at 23..29), 0 for TRK, which SDK 1.15's preset import
# keeps in the key's record. Stock's import drops random's bits from 16, so
# older firmware reads the codes alone; SDK 1.15 plays the amount, as its code
# is always the amount's nearest step (the SDK's drum_send.hpp).
TRK = None
STEPS = (0, 17, 33, 50, 67, 83, 100)   # codes 1..7, as SDK 1.15 shows them


def send_code(amount):
    """A send's 3-bit code: 0 for TRK, else the amount's nearest step, halves up (drum_send_shadow)."""
    return 0 if amount is TRK else 1 + (6 * amount + 50) // 100


def send_index(amount):
    """A send's amount as random keeps it from bit 16: 0 for TRK, else the amount + 1."""
    return 0 if amount is TRK else amount + 1


def send_ok(amount):
    return amount is TRK or (type(amount) is int and 0 <= amount <= 100)


def sends_read(random):
    """Both sends as SDK 1.15 reads them back from random (drum_send_stored): the amount
    when its byte holds one and the code beside it is its step; else the old code, as
    ("code", c), or TRK."""
    sends = []
    for s in (0, 1):
        index, code = (random >> (16 + 7 * s)) & 0x7f, (random >> (9 + 3 * s)) & 7
        if 1 <= index <= 101 and code == send_code(index - 1):
            sends.append(index - 1)
        else:
            sends.append(("code", code) if code else TRK)
    return sends


def default_kit():
    text = (ROOT / "kit.h").read_text()
    models = [int(x) for x in re.search(r"key_model\[keys\] = \{([^}]*)\}", text).group(1).split(",")]
    values = [[int(x) for x in row.split(",")] for row in re.findall(r"\{([0-9, ]+)\},\s+// \d+", text)]
    choke = [int(x) for x in re.search(r"key_choke\[keys\] = \{([^}]*)\}", text).group(1).split(",")]
    assert len(models) == len(values) == len(choke) == 24
    return [{"model": m, "c": list(v), "choke": g, "pan": 0, "gain": LEVELS.get("default", {}).get(k, 0), "tune": 0, "tone": 0,
             "fx1": TRK, "fx2": TRK} for k, (m, v, g) in enumerate(zip(models, values, choke))]


def model_defaults(name):
    text = (ROOT / "kit.h").read_text()
    for n, d in re.findall(r'\{"(\w+)", DUST_LABELS, \{([^}]*)\}\}', text):
        if n == name:
            return [int(x) for x in d.split(",")]
    raise KeyError(name)


# The keys by role (index from key 53), as kit.h lays them out.
(BD, SD, HT, MT, LT, CP, RS, CB, CH, OH, RC, CC, TB, CH_, BG, SH, BD2, SD2, PH, CL, RS2, BS, BS2, RB) = range(24)
CONGA_HI, CONGA_LO = CH_, CL
# the eight controls by name
SPEED, PICK, LIFT, WEIGHT, CUE, WEAR, WARP, DRAG = range(8)


def speed(rpm):
    """SPEED (0..127) for a platter speed in rpm: 33 1/3 at 0, 45 at 64, 78 at 127, evenly in log between."""
    if rpm <= 45:
        return max(0, min(127, round(64 - 64 * math.log(45 / rpm) / math.log(45 / 33.3333333))))
    return max(0, min(127, round(64 + 63 * math.log(rpm / 45) / math.log(78 / 45))))


def bar(n, late=0.5):
    """PICK (0..127) for bar n (1..8), late in the bar or early."""
    return max(0, min(127, (n - 1) * 16 + round(late * 15)))


def kit(changes):
    """The default kit with changes: key -> {"model": name, "c": {slot: value}, "gain": dB, "pan": -100..100,
    "choke": 0/1, "tune": -16..15, "tone": -8..7, "fx1", "fx2": TRK or 0..100}; "all": changes every key takes first."""
    keys = default_kit()
    for key in keys:
        for slot, v in changes.get("all", {}).items():
            key["c"][slot] = v
    for k, ch in changes.items():
        if k == "all":
            continue
        key = keys[k]
        if "model" in ch:
            key["model"] = MODELS.index(ch["model"])
            key["c"] = model_defaults(ch["model"])
        for s, v in ch.get("c", {}).items():
            key["c"][s] = v
        for f in ("gain", "pan", "choke", "tune", "tone", "fx1", "fx2"):
            if f in ch:
                key[f] = ch[f]
    return keys


# Each key's LEVEL (dB, a cut): the engine brings every model's default hit
# to its role's loudness (tools/calibrate.py: kicks and toms -16.5 LUFS, the
# rest -19, re the ceiling), and holds it as the controls turn; a key whose
# own settings still sound over that by more than 0.75 LU is cut to it here,
# in whole dB, measured at its settings, pad controls included
# (tools/preset_levels.py). Keys not named play at 0 dB.
LEVELS = {
    "crate": {HT: -1, MT: -1, LT: -1, RS: -1, OH: -1, TB: -1, SD2: -1, BS: -1, RB: -3},
    "45": {BS2: -1},
    "basement": {SD: -1, HT: -2, MT: -2, LT: -2, CH: -2, OH: -4, RC: -2, CC: -1, TB: -2, CONGA_HI: -1, SD2: -2, CL: -1, BS: -1, BS2: -1, RB: -6},
    "bossa": {CL: -1},
    "boogaloo": {CL: -1, RS2: -1},
    "dubplate": {LT: -1, CC: -2, CL: -2, RB: -8},
    "library": {CL: -1},
    "78": {HT: -1, MT: -1, LT: -1, CONGA_HI: -1, CL: -1},
}

ALL_SLOW = {SPEED: speed(33.3333)}
PRESETS = {
    # the classic: a soul break played at 33, heavy and slow, a little worn:
    # boom bap
    "crate": kit({
        "all": {SPEED: speed(33.3333), WEIGHT: 92, WEAR: 18},
        BD: {"c": {PICK: bar(6), LIFT: 92}},
        SD: {"c": {PICK: bar(7, 0.2), LIFT: 90}, "fx2": 17},
        CH: {"c": {PICK: bar(5), LIFT: 84}, "pan": 15},
        OH: {"c": {PICK: bar(5), LIFT: 80}, "pan": 15},
        PH: {"c": {PICK: bar(2), LIFT: 40}, "pan": 15},
        RC: {"c": {PICK: bar(4)}, "pan": -20},
        SD2: {"c": {PICK: bar(1, 0.6), LIFT: 70}},
        BD2: {"c": {SPEED: 0, PICK: bar(4), LIFT: 104}},
        CP: {"c": {PICK: bar(6), LIFT: 70}},
        BS: {"c": {PICK: bar(1, 0.8), LIFT: 70}},
        BS2: {"c": {PICK: bar(4, 0.8), LIFT: 70}},
    }),
    # funk at 45: the hard bars, cut short, the hats bright
    "45": kit({
        "all": {SPEED: speed(45), WEIGHT: 70},
        BD: {"c": {PICK: bar(7, 0.3), LIFT: 80}},
        SD: {"c": {PICK: bar(7, 0.6), LIFT: 78}, "tone": 2},
        SD2: {"c": {PICK: bar(2, 0.4), LIFT: 70}},
        CH: {"c": {PICK: bar(6), LIFT: 70}, "tone": 3, "pan": 20},
        OH: {"c": {PICK: bar(6), LIFT: 76}, "tone": 3, "pan": 20},
        PH: {"c": {PICK: bar(3), LIFT: 36}, "pan": 20},
        CP: {"c": {PICK: bar(7), LIFT: 70}},
        CB: {"c": {PICK: bar(6), LIFT: 70}, "pan": -25},
        TB: {"c": {PICK: bar(6), LIFT: 80}, "pan": 30},
        BS: {"c": {PICK: bar(1, 0.9), LIFT: 64}},
        BS2: {"c": {PICK: bar(5, 0.9), LIFT: 64}},
    }),
    # lo-fi hip hop: a played-out copy on a warped disc, the brush down, the arm heavy
    "basement": kit({
        "all": {SPEED: speed(38), WEIGHT: 104, WEAR: 72, WARP: 46},
        BD: {"c": {PICK: bar(5), LIFT: 90, DRAG: 30}},
        BD2: {"c": {SPEED: 0, PICK: bar(4), DRAG: 70}},
        SD: {"c": {PICK: bar(5), LIFT: 86}, "tone": -2},
        SD2: {"c": {PICK: bar(1), LIFT: 80}, "tone": -2},
        CH: {"c": {PICK: bar(3), LIFT: 76}, "tone": -3, "pan": 20},
        OH: {"c": {PICK: bar(3), LIFT: 70}, "tone": -3, "pan": 20},
        RS: {"c": {PICK: bar(2)}},
        SH: {"c": {PICK: bar(3)}, "pan": -20},
        BS: {"c": {PICK: bar(1, 0.5), LIFT: 90, DRAG: 20}},
        BS2: {"c": {PICK: bar(4, 0.5), LIFT: 90, DRAG: 20}},
        RB: {"c": {PICK: bar(8, 0.9), DRAG: 60}},
    }),
    # bossa nova: the cross-stick, bongos, a shaker and a ghosted snare, light hands
    "bossa": kit({
        "all": {WEIGHT: 80},
        BD: {"c": {PICK: bar(2), LIFT: 84}},
        RS: {"c": {PICK: bar(2, 0.3), LIFT: 70}, "pan": -10},
        SD: {"c": {PICK: bar(1, 0.4), LIFT: 70}},
        SD2: {"c": {PICK: bar(1, 0.1), LIFT: 60}},
        CH: {"c": {PICK: bar(2), LIFT: 70}, "pan": 25},
        SH: {"c": {PICK: bar(3), LIFT: 80}, "pan": 30},
        BG: {"c": {PICK: bar(4), LIFT: 80}, "pan": -30},
        CONGA_HI: {"model": "BONGO", "c": {SPEED: speed(38), PICK: bar(4)}, "pan": -35},
        RC: {"c": {PICK: bar(2), LIFT: 90}, "pan": 20},
        BS: {"c": {PICK: bar(1, 0.4), LIFT: 76}},
        BS2: {"c": {PICK: bar(4, 0.4), LIFT: 76}},
    }),
    # boogaloo: Latin soul: congas, cowbell, tambourine on the backbeat, the band in
    "boogaloo": kit({
        "all": {WEIGHT: 76, WEAR: 10},
        BD: {"c": {PICK: bar(6)}},
        SD: {"c": {PICK: bar(6)}},
        CP: {"c": {PICK: bar(8, 0.9)}},
        CONGA_HI: {"c": {SPEED: speed(50), PICK: bar(5)}, "pan": 30},
        CL: {"c": {SPEED: speed(41), PICK: bar(5)}, "pan": 40},
        BG: {"c": {PICK: bar(7)}, "pan": -30},
        CB: {"c": {PICK: bar(7), LIFT: 84}, "pan": -25},
        TB: {"c": {PICK: bar(7)}, "pan": 20},
        HT: {"c": {SPEED: speed(56), PICK: bar(7)}, "pan": -15},
        MT: {"c": {PICK: bar(7)}, "pan": 10},
        SH: {"c": {PICK: bar(6)}, "pan": -40},
    }),
    # dubplate: a heavy arm, a deep kick on the platter slowed by the brush, rim and bass
    "dubplate": kit({
        "all": {WEIGHT: 120, WEAR: 14},
        BD: {"c": {SPEED: speed(36), PICK: bar(6), LIFT: 104, DRAG: 52}},
        BD2: {"c": {SPEED: 0, PICK: bar(5), LIFT: 110, DRAG: 90}},
        RS: {"c": {PICK: bar(7, 0.4), LIFT: 76}, "fx1": 83},
        RS2: {"c": {PICK: bar(8), LIFT: 70}, "fx1": 100},
        SD: {"c": {PICK: bar(6), LIFT: 96, DRAG: 30}, "fx1": 67},
        CH: {"c": {PICK: bar(4), LIFT: 80}, "pan": 25},
        OH: {"c": {PICK: bar(4), LIFT: 70}, "pan": 25, "fx1": 50},
        BS: {"c": {SPEED: speed(41), PICK: bar(1, 0.6), LIFT: 104, DRAG: 24}},
        BS2: {"c": {SPEED: speed(41), PICK: bar(3, 0.6), LIFT: 104, DRAG: 24}},
        CB: {"model": "BASS", "c": {SPEED: speed(41), PICK: bar(4, 0.6), LIFT: 104, DRAG: 24}},
        CC: {"c": {PICK: bar(6), DRAG: 80}, "fx1": 67},
    }),
    # library: a clean, roomy 1960s library record: nothing worn, every hit let ring
    "library": kit({
        "all": {LIFT: 112},
        BD: {"c": {PICK: bar(4)}},
        SD: {"c": {PICK: bar(5)}, "fx2": 33},
        HT: {"c": {PICK: bar(5)}, "pan": -25, "fx2": 17},
        MT: {"c": {PICK: bar(5)}, "pan": 0, "fx2": 17},
        LT: {"c": {PICK: bar(5)}, "pan": 25, "fx2": 17},
        CH: {"c": {PICK: bar(4)}, "pan": 30},
        OH: {"c": {PICK: bar(4)}, "pan": 30},
        RC: {"c": {PICK: bar(4)}, "pan": -30, "fx2": 17},
        CC: {"c": {PICK: bar(6)}, "pan": -30, "fx2": 33},
        RB: {"pan": -30},
        BG: {"c": {PICK: bar(5)}, "pan": 35},
        CONGA_HI: {"c": {PICK: bar(5)}, "pan": 40},
    }),
    # 78: the break sped up, thin and fast, a light arm crunching the loud
    # bars: jungle
    "78": kit({
        "all": {SPEED: speed(78), WEIGHT: 40, WEAR: 26},
        BD: {"c": {PICK: bar(7), LIFT: 104}},
        BD2: {"c": {SPEED: speed(62), PICK: bar(6), LIFT: 104}},
        SD: {"c": {PICK: bar(7, 0.8), LIFT: 100}},
        SD2: {"c": {PICK: bar(3), LIFT: 90}},
        RS2: {"c": {PICK: bar(8), LIFT: 90}},
        CH: {"c": {PICK: bar(6), LIFT: 84}, "pan": 20},
        OH: {"c": {PICK: bar(6), LIFT: 84}, "pan": 20},
        RC: {"c": {PICK: bar(5), LIFT: 104}, "pan": -20},
        BS: {"c": {SPEED: speed(45), PICK: bar(1, 0.8), LIFT: 104}, "tune": -12},
        BS2: {"c": {SPEED: speed(45), PICK: bar(4, 0.8), LIFT: 104}, "tune": -12},
    }),
}
for _name, _keys in PRESETS.items():
    for _k, _key in enumerate(_keys):
        _key["gain"] = LEVELS.get(_name, {}).get(_k, 0)


def kit_value(model, controls):
    """The 60 bits a key stores in a kit of up to 16 models (the SDK's
    drum_storage.hpp): the eight controls, 7 bits each, then the model."""
    return sum(c << (7 * i) for i, c in enumerate(controls)) | model << 56


def pad_random(key):
    """The key's pad fields as the region's random: the pad word's bits 16..30, shifted
    down, the sends' codes among them; then the sends' amounts from bit 16."""
    assert -16 <= key["tune"] <= 15 and -8 <= key["tone"] <= 7 and send_ok(key["fx1"]) and send_ok(key["fx2"])
    return ((key["tune"] & 31) | (key["tone"] & 15) << 5 | send_code(key["fx1"]) << 9 | send_code(key["fx2"]) << 12
            | send_index(key["fx1"]) << 16 | send_index(key["fx2"]) << 23)


def unpack_random(random):
    """What every firmware decodes from the word stock's reader restores, max(random << 16, 0)
    as a 32-bit int: the pad controls and the sends' codes (bits 16 and up of random drop out)."""
    word = (random << 16) & 0xffffffff
    word = 0 if word >> 31 else word
    field = lambda low, bits: (word >> low) & ((1 << bits) - 1)
    tune, tone = field(16, 5), field(21, 4)
    return {"tune": tune - 32 if tune & 16 else tune, "tone": tone - 16 if tone & 8 else tone, "fx1": field(25, 3), "fx2": field(28, 3)}


def region(index, key):
    value = kit_value(key["model"], key["c"])
    r = {"hikey": 53 + index, "model": "fm slap", "params": [(value >> (15 * w)) & 0x7fff for w in range(4)]}
    if key["choke"]:
        r["playmode"] = "mute1"
    if key["pan"]:
        r["pan"] = key["pan"]
    r["gain"] = key["gain"]
    r["random"] = pad_random(key)   # written out, 0 when neutral
    return r


def preset(keys, params):
    # the SDK's preset template (tooling/default_preset.json)
    p = {
        "engine": {"bendrange": 8191, "highpass": 0,
                   "modulation": {s: {"amount": 16384, "target": 0} for s in ("aftertouch", "modwheel", "pitchbend", "velocity")},
                   "playmode": "poly", "portamento.amount": 0, "portamento.type": 0, "transpose": 0, "tuning.root": 0,
                   "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0},
        "envelope": {"amp": {"attack": 128, "decay": 8192, "release": 32767, "sustain": 32767},
                     "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767}},
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0, 0, 0, 0, 0, 0, 0, 0], "type": "value"},
        "octave": 0, "platform": "OP-XY", "regions": [], "version": 4}
    machine = json.loads((ROOT / "machine.json").read_text())
    p["engine"]["params"] = params
    p["envelope"]["amp"].update(machine.get("presetDefaults", {}).get("amp", {}))
    p["type"] = "dust"
    p["regions"] = [region(i, k) for i, k in enumerate(keys)]
    return p


def check():
    """Each region decodes, as the SDK's drum_unpack does, to its key's model and controls."""
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        for i, k in enumerate(keys):
            r = region(i, k)
            value = sum(p << (15 * w) for w, p in enumerate(r["params"]))
            assert value >> 56 == k["model"] < 15, (name, i)
            assert [(value >> (7 * s)) & 127 for s in range(8)] == k["c"], (name, i)
            assert all(0 <= c <= 127 for c in k["c"]) and -30 <= r["gain"] <= 0 and -100 <= r.get("pan", 0) <= 100, (name, i)
            # the pad fields come back as they went in, and the word's bit 31 stays clear
            random = r.get("random", 0)
            assert 0 <= random < 1 << 30 and not random & 0x8000, (name, i, random)
            back = unpack_random(random)
            assert back == {"tune": k["tune"], "tone": k["tone"], "fx1": send_code(k["fx1"]), "fx2": send_code(k["fx2"])}, (name, i, back)
            # SDK 1.15 reads each send back as the amount written. The kits were
            # voiced on the seven steps, so each amount is the percent SDK 1.15
            # shows for (and turns on from) the code these presets carried before
            # SDK 1.15 (drum_send_percent), and the code is unchanged.
            assert sends_read(random) == [k["fx1"], k["fx2"]], (name, i, random)
            assert all(a is TRK or a == STEPS[send_code(a) - 1] for a in (k["fx1"], k["fx2"])), (name, i)
            assert "random" in r, (name, i)
    carried = sum(1 for keys in PRESETS.values() for k in keys if pad_random(k))
    print(f"{len(PRESETS) + 1} kits: every key decodes to its own sound, pan, level and pad fields ({carried} keys carry pad fields)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    check()
    if not a.out:
        return
    definition = json.loads((ROOT / "definition.json").read_text())
    params = [round(p["default"] * 32767) for p in definition["parameters"]]
    folder = pathlib.Path(a.out) / "content" / "presets" / "dust"
    folder.mkdir(parents=True, exist_ok=True)
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        (folder / f"{name}.json").write_text(json.dumps(preset(keys, params), indent=2) + "\n")
        print("wrote", folder / f"{name}.json")


if __name__ == "__main__":
    main()
