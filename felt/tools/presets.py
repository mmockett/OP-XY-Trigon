#!/usr/bin/env python3
"""FELT's kit presets, and the files a build installs for them.

    python3 tools/presets.py --out <build>/candidate     # content/presets/felt/*.json
    python3 tools/presets.py --check                     # each region decodes to its key's sound

A kit preset holds the 24 keys (53..76) as the drum kind stores them, the way
MNMD's tools/build_firmware.py writes MD KIT's: each key a region whose four
params carry its eight 7-bit controls and its model (the SDK's
drum_storage.hpp), marked "fm slap" (Drumline type 7, the SDK's mark), its
choke group as the play mode ("mute1" is group 1) and its own pan and level
(gain, in dB). The SDK's machine presets folder refuses regions, so these are
written into a build's content/presets/felt/ after it is built, as MNMD's
are. default.json is the kit drum_defaults gives (kit.h).

The pad controls (TUNE, SOFT) and the per-key sends live in bits 16..30 of
stock's pad word +0x14 (the SDK's drum_storage.hpp): TUNE (5 bits, signed)
from bit 16, SOFT (4) from 21, FX I (3) from 25, FX II (3) from 28. Stock's
preset writer exports that word's top half as the region's "random" and its
reader restores it shifted back, as max(random << 16, 0); so a region
carries them as random, with its top bit (the word's bit 31) clear: TUNE in
random's bits 0..4, SOFT 5..8, FX I 9..11, FX II 12..14. Every region here
writes "random" explicitly, 0 when every field is neutral.

Each send is TRK (the track's send) or the key's own 0..100 in 1% steps
(SDK 1.15): its code above is the nearest of seven steps, and random's bits
16..22 (FX I) and 23..29 (FX II) carry the amount itself (send_code, below).
"""
import argparse, json, math, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODELS = [n for n in re.findall(r'\{"(\w+)", \{"TUNE"', (ROOT / "kit.h").read_text())]

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
    return [{"model": m, "c": list(v), "choke": g, "pan": 0, "gain": LEVELS.get("default", {}).get(k, 0), "tune": 0, "soft": 0,
             "fx1": TRK, "fx2": TRK} for k, (m, v, g) in enumerate(zip(models, values, choke))]


def model_defaults(name):
    text = (ROOT / "kit.h").read_text()
    for n, d in re.findall(r'\{"(\w+)", \{[^}]*\}, \{([^}]*)\}\}', text):
        if n == name:
            return [int(x) for x in d.split(",")]
    raise KeyError(name)


# The keys by role (index from key 53), as the default kit lays them out.
BD, SD, HT, MT, LT, CP, RS, CB, CH, OH, RC, CC, SH, GL, BS, BW, BD2, SD2, PH, FT, WB, GH, PL2, SL = range(24)


def pitch_range(model):
    """A model's TUNE range (shape.h): its pitch at TUNE 0 and 127, Hz."""
    text = (ROOT / "shape.h").read_text()
    block = text.split("const Range pitch[12] = {")[1].split("};")[0]
    pairs = re.findall(r"\{L\(([\d.]+)\), L\(([\d.]+)\)\}", block)
    lo, hi = pairs[MODELS.index(model)]
    return float(lo), float(hi)


def note_tune(model, hz):
    """The TUNE (0..127) that sets a model's string nearest a pitch."""
    lo, hi = pitch_range(model)
    return max(0, min(127, round(127 * math.log(hz / lo) / math.log(hi / lo))))


def tuned(model, hz):
    """TUNE and the pad's TUNE (semitones) that put a model's string exactly on a pitch."""
    lo, hi = pitch_range(model)
    c = note_tune(model, hz)
    at = lo * (hi / lo) ** (c / 127)
    return c, max(-16, min(15, round(12 * math.log2(hz / at))))


def hz(note):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    n, octave = note[:-1], int(note[-1])
    return 440 * 2 ** ((names.index(n) + 12 * (octave + 1) - 69) / 12)


def kit(changes):
    """The default kit with changes: key -> {"model": name, "c": {slot: value}, "gain": dB, "pan": -100..100,
    "choke": 0/1, "tune": -16..15, "soft": 0..15, "fx1", "fx2": TRK or 0..100}."""
    keys = default_kit()
    for k, ch in changes.items():
        key = keys[k]
        if "model" in ch:
            key["model"] = MODELS.index(ch["model"])
            key["c"] = model_defaults(ch["model"])
        for s, v in ch.get("c", {}).items():
            key["c"][s] = v
        for f in ("gain", "pan", "choke", "tune", "soft", "fx1", "fx2"):
            if f in ch:
                key[f] = ch[f]
    return keys


# Each key's LEVEL (dB, a cut): the engine brings every model's default hit
# to its role's loudness (tools/calibrate.py: kicks and toms -16.5 LUFS, the
# rest -19, re the ceiling), and holds it as the controls turn; a key whose
# own settings still sound over that by more than 0.75 LU is cut to it here,
# in whole dB, measured at its settings, pad controls included (tools/keys.py
# --cuts; keys index 0..23 from 53). The default kit plays every key at 0 dB;
# keys not named play at 0 dB.
LEVELS = {
    'bacchanale': {5: -1, 8: -1, 18: -1, 21: -1},
    'attic': {14: -1, 21: -1},
    'feltpiano': {1: -1, 4: -2, 18: -1, 21: -1},
    'honkytonk': {18: -1, 21: -1},
    'sonatas': {8: -1, 18: -1},
    'clusters': {0: -1, 4: -1, 8: -1, 10: -1, 16: -3, 18: -1, 19: -3, 21: -1},
    'musicbox': {18: -1, 21: -1},
    'afterhours': {17: -1, 19: -2, 21: -1},
}

# D minor pentatonic for the tuned sets (D F G A C), and its gamelan cousin,
# a pelog-like set (C Db Eb G Ab)
PENTA = ["D", "F", "G", "A", "C"]


def at(model, note, extra=None, **fields):
    """A key on model, its string tuned to note: TUNE plus the pad's TUNE."""
    c, st = tuned(model, hz(note))
    ch = {"model": model, "c": {0: c, **(extra or {})}, "tune": st}
    ch.update(fields)
    return ch


PRESETS = {
    # Cage's own: bolts and screws, rubber-muted toms tuned to a pentatonic
    # set, a bolted gong, a dry ritual pulse
    "bacchanale": kit({
        BD: {"c": {0: 30, 1: 46, 3: 96, 4: 40, 5: 64}},
        SD: {"c": {1: 40, 3: 70, 4: 30, 5: 70}, "pan": 10},
        HT: at("TOM", "A3", {1: 50, 3: 90, 4: 10}, pan=-25),
        MT: at("TOM", "G3", {1: 54, 3: 90, 4: 10}, pan=-10),
        LT: at("TOM", "F3", {1: 58, 3: 90, 4: 10}, pan=10),
        FT: at("TOM", "D3", {1: 62, 3: 90, 4: 10}, pan=25),
        CB: at("BELL", "D5", {1: 40, 2: 100, 3: 70, 4: 60}, pan=30),
        RC: at("BELL", "A5", {1: 70, 2: 90, 3: 50, 4: 40, 5: 30}, pan=-30),
        GL: at("BOLT", "D2", {1: 100, 2: 30, 3: 100, 4: 60, 6: 30}, fx2=33),
        GH: at("BOLT", "A3", {1: 80, 2: 60, 3: 80, 4: 40}, pan=20),
        RS: {"c": {0: 80, 1: 20, 2: 110, 3: 90}},
        WB: {"c": {0: 100, 1: 20, 2: 60, 3: 120}},
        CP: {"c": {0: 50, 1: 30, 2: 40, 3: 30, 4: 90, 5: 70}},
    }),
    # lo-fi boom bap: a worn felt kick, a paper snare, tack hats, the brush
    # open, everything aged
    "attic": kit({
        BD: {"c": {0: 34, 1: 70, 2: 30, 3: 60, 4: 64, 5: 70, 7: 50}, "soft": 4},
        BD2: {"c": {0: 22, 1: 96, 2: 20, 3: 40, 4: 80, 5: 50, 7: 60}},
        SD: {"c": {0: 54, 1: 56, 2: 50, 3: 110, 4: 40, 5: 50, 7: 60}},
        SD2: {"c": {0: 64, 1: 30, 2: 70, 3: 120, 4: 70, 5: 20, 7: 60}},
        CH: {"model": "TACK", "c": {0: 110, 1: 4, 2: 110, 3: 120, 4: 20, 5: 60, 7: 50}, "choke": 1},
        OH: {"c": {0: 50, 1: 70, 2: 50, 3: 30, 4: 70, 7: 60}},
        PH: {"c": {0: 50, 1: 6, 2: 6, 3: 30, 4: 70, 7: 60}},
        CP: {"c": {1: 50, 4: 40, 5: 70, 7: 60}},
        BS: {"c": {0: 30, 1: 50, 2: 30, 4: 60, 7: 50}},
        SH: {"c": {1: 40, 2: 60, 3: 60, 5: 60, 7: 60}},
        RS: {"c": {1: 30, 7: 60}},
    }),
    # soft, damped, with the pedal's halo: ambient beats
    "feltpiano": kit({
        BD: {"c": {0: 44, 1: 60, 2: 10, 3: 60, 4: 20, 5: 20, 6: 50}, "soft": 10},
        SD: {"c": {0: 70, 1: 40, 2: 20, 3: 50, 4: 80, 5: 90, 6: 60}, "soft": 10},
        HT: {"c": {1: 90, 2: 30, 3: 20, 4: 5, 6: 80}, "soft": 8},
        MT: {"c": {1: 90, 2: 30, 3: 20, 4: 5, 6: 80}, "soft": 8},
        LT: {"c": {1: 90, 2: 30, 3: 20, 4: 5, 6: 80}, "soft": 8},
        CH: {"c": {0: 40, 1: 25, 2: 50, 3: 30, 4: 20, 6: 40}},
        OH: {"c": {0: 40, 1: 70, 2: 80, 3: 30, 4: 20, 6: 60}},
        CC: {"c": {0: 60, 1: 90, 2: 90, 3: 20, 4: 90, 5: 0, 6: 100}},
        BW: {"c": {0: 64, 1: 80, 2: 30, 3: 80, 6: 100}},
        BS: {"c": {1: 90, 2: 35, 4: 20, 6: 90}, "soft": 5},
        PL2: {"c": {1: 90, 2: 50, 6: 100}, "soft": 4},
        RC: {"c": {0: 70, 1: 110, 2: 50, 3: 30, 6: 90}},
    }),
    # a honky-tonk shuffle: tack hammers, loose screws, everything a little out
    "honkytonk": kit({
        BD: {"c": {0: 46, 1: 46, 2: 60, 3: 70, 4: 30, 5: 70, 7: 70}},
        SD: {"c": {0: 70, 1: 40, 2: 90, 3: 80, 5: 60, 7: 70}},
        CH: {"model": "TACK", "c": {0: 100, 1: 10, 2: 120, 3: 90, 4: 40, 5: 40, 7: 90}, "choke": 1},
        OH: {"c": {0: 70, 1: 60, 2: 40, 3: 80, 7: 80}},
        CB: {"c": {0: 70, 1: 50, 2: 110, 3: 60, 5: 90, 7: 90}, "pan": 30},
        RC: {"c": {0: 84, 1: 90, 2: 110, 3: 40, 5: 60, 7: 90}, "pan": -25},
        RS: {"c": {0: 90, 1: 30, 2: 120, 3: 60, 7: 100}},
        WB: {"c": {0: 110, 1: 20, 2: 127, 3: 100, 7: 100}},
        BS: {"c": {0: 40, 1: 50, 2: 90, 3: 30, 5: 40, 7: 90}},
        PL2: {"c": {0: 80, 1: 40, 2: 110, 5: 50, 7: 90}},
        CP: {"c": {0: 80, 2: 30, 3: 20, 4: 100, 7: 80}},
    }),
    # tuned thuds and gongs for melodic percussion, on the pentatonic set
    "sonatas": kit({
        **{k: at("THUD", n, {1: 70, 3: 50, 4: 20, 5: 30}, pan=p)
           for k, n, p in zip((BD, BD2), ("D1", "A1"), (0, 0))},
        **{k: at("TOM", n, {1: 70, 2: 50, 3: 40, 4: 10}, pan=p)
           for k, n, p in zip((HT, MT, LT, FT), ("C4", "A3", "G3", "D3"), (-30, -10, 10, 30))},
        **{k: at("BOLT", n, {1: 100, 2: 40, 3: 70, 4: 40}, pan=p)
           for k, n, p in zip((GL, GH, CB, RC), ("D3", "A3", "D4", "F4"), (-20, 20, -40, 40))},
        **{k: at("BELL", n, {1: 60, 2: 90, 3: 40, 4: 70}, pan=p)
           for k, n, p in zip((RS, WB, CP), ("A5", "C6", "G5"), (25, -25, 0))},
        BS: at("PLUCK", "D2", {1: 70, 2: 50, 4: 30}),
        PL2: at("PLUCK", "A2", {1: 70, 2: 60, 4: 30}),
    }),
    # cinematic: long washes, bowed risers, deep thuds
    "clusters": kit({
        BD: {"c": {0: 20, 1: 110, 2: 40, 3: 40, 4: 90, 5: 40, 6: 60}},
        BD2: {"c": {0: 10, 1: 127, 2: 30, 3: 30, 4: 110, 5: 30, 6: 90}},
        CC: {"c": {0: 30, 1: 110, 2: 110, 3: 40, 4: 110, 5: 60, 6: 110}, "fx2": 50},
        RC: {"model": "PLANK", "c": {0: 70, 1: 90, 2: 40, 3: 80, 4: 20, 5: 100, 6: 70}},
        BW: {"c": {0: 40, 1: 100, 2: 80, 3: 90, 4: 110, 5: 40, 6: 100}, "fx2": 50},
        GL: {"c": {0: 20, 1: 127, 2: 30, 3: 110, 4: 30, 6: 80}},
        LT: {"c": {0: 20, 1: 90, 2: 30, 3: 40, 4: 30, 6: 70}},
        FT: {"c": {0: 0, 1: 100, 2: 30, 3: 30, 4: 40, 6: 80}},
        SD: {"c": {0: 40, 1: 70, 3: 80, 5: 70, 6: 80}},
        BS: {"model": "BOW", "c": {0: 20, 1: 90, 2: 90, 3: 100, 4: 20, 5: 60, 6: 90}},
    }),
    # a music box: high, small, plucked and ticked
    "musicbox": kit({
        BD: {"c": {0: 70, 1: 30, 2: 60, 3: 90, 4: 30, 5: 30}},
        SD: {"model": "TACK", "c": {0: 100, 1: 30, 2: 110, 3: 80, 4: 30}},
        CH: {"c": {0: 120, 1: 6, 2: 0, 3: 90, 4: 0}},
        OH: {"c": {0: 120, 1: 40, 2: 20, 3: 100, 4: 0}},
        **{k: at("PLUCK", n, {1: 40, 2: 110, 3: 20, 4: 10}, pan=p)
           for k, n, p in zip((HT, MT, LT, FT), ("D4", "A3", "F3", "D3"), (30, 10, -10, -30))},
        **{k: at("BELL", n, {1: 60, 2: 110, 3: 30, 4: 30}, pan=p)
           for k, n, p in zip((RS, CB, RC, WB), ("D6", "A5", "F5", "C6"), (-40, 40, -20, 20))},
        SH: {"c": {0: 110, 1: 10, 2: 70, 3: 30, 4: 50}},
    }),
    # a jazz ballad: brushes, felt toms, a soft rattle, a bell ride
    "afterhours": kit({
        BD: {"c": {0: 40, 1: 40, 2: 10, 3: 70, 4: 20, 5: 30}, "soft": 8},
        SD: {"model": "BRUSH", "c": {0: 30, 1: 50, 2: 110, 3: 10, 4: 90, 5: 30}},
        SD2: {"c": {0: 60, 1: 40, 2: 20, 3: 60, 4: 30, 5: 60}, "soft": 10},
        CH: {"c": {0: 40, 1: 30, 2: 90, 3: 20, 4: 80}},
        OH: {"c": {0: 40, 1: 70, 2: 110, 3: 20, 4: 80}},
        PH: {"c": {0: 40, 1: 15, 2: 30, 3: 20, 4: 80}},
        HT: {"c": {1: 64, 2: 20, 3: 30}, "soft": 10},
        MT: {"c": {1: 64, 2: 20, 3: 30}, "soft": 10},
        LT: {"c": {1: 68, 2: 20, 3: 30}, "soft": 10},
        FT: {"c": {1: 72, 2: 20, 3: 30}, "soft": 10},
        RC: {"c": {0: 86, 1: 110, 2: 70, 3: 30, 4: 40, 6: 20}, "pan": 30},
        BS: {"c": {0: 34, 1: 70, 2: 20, 4: 30}, "soft": 6},
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
    assert -16 <= key["tune"] <= 15 and 0 <= key["soft"] <= 15 and send_ok(key["fx1"]) and send_ok(key["fx2"])
    return ((key["tune"] & 31) | key["soft"] << 5 | send_code(key["fx1"]) << 9 | send_code(key["fx2"]) << 12
            | send_index(key["fx1"]) << 16 | send_index(key["fx2"]) << 23)


def unpack_random(random):
    """What every firmware decodes from the word stock's reader restores, max(random << 16, 0)
    as a 32-bit int: the pad controls and the sends' codes (bits 16 and up of random drop out)."""
    word = (random << 16) & 0xffffffff
    word = 0 if word >> 31 else word
    field = lambda low, bits: (word >> low) & ((1 << bits) - 1)
    tune = field(16, 5)
    return {"tune": tune - 32 if tune & 16 else tune, "soft": field(21, 4), "fx1": field(25, 3), "fx2": field(28, 3)}


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
    p["type"] = "felt"
    p["regions"] = [region(i, k) for i, k in enumerate(keys)]
    return p


def check():
    """Each region decodes, as the SDK's drum_unpack does, to its key's model and controls."""
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        for i, k in enumerate(keys):
            r = region(i, k)
            value = sum(p << (15 * w) for w, p in enumerate(r["params"]))
            assert value >> 56 == k["model"] < len(MODELS), (name, i)
            assert [(value >> (7 * s)) & 127 for s in range(8)] == k["c"], (name, i)
            assert all(0 <= c <= 127 for c in k["c"]) and -30 <= r["gain"] <= 0 and -100 <= r.get("pan", 0) <= 100, (name, i)
            # the pad fields come back as they went in, and the word's bit 31 stays clear
            assert "random" in r, (name, i)
            random = r["random"]
            assert 0 <= random < 1 << 30 and not random & 0x8000, (name, i, random)
            back = unpack_random(random)
            assert back == {"tune": k["tune"], "soft": k["soft"], "fx1": send_code(k["fx1"]), "fx2": send_code(k["fx2"])}, (name, i, back)
            # SDK 1.15 reads each send back as the amount written. The kits were
            # voiced on the seven steps, so each amount is the percent SDK 1.15
            # shows for (and turns on from) the code these presets carried before
            # SDK 1.15 (drum_send_percent), and the code is unchanged.
            assert sends_read(random) == [k["fx1"], k["fx2"]], (name, i, random)
            assert all(a is TRK or a == STEPS[send_code(a) - 1] for a in (k["fx1"], k["fx2"])), (name, i)
    carried = sum(1 for keys in PRESETS.values() for k in keys if pad_random(k))
    print(f"{len(PRESETS) + 1} kits: every key decodes to its own sound, pan, level and pad fields ({carried} keys carry pad fields)")


def scenarios():
    """tools/ub.sh's sweep: every key of every kit, each model with each
    control at 0, 32, 64, 96 and 127 and SOFT at 4, 8, 11 and 15 (every
    setting tools/calibrate.py measures, so its levels come out the same
    under any compiler), every control at 0 and at 127, TUNE at its ends.
    Lines of: model c0..c7 tune soft."""
    rows = []
    for _name, keys in [("default", default_kit()), *PRESETS.items()]:
        rows += [(k["model"], k["c"], k["tune"], k["soft"]) for k in keys]
    for m, name in enumerate(MODELS):
        d = model_defaults(name)
        for s in range(8):
            for v in (0, 32, 64, 96, 127):   # every point tools/calibrate.py measures
                rows.append((m, d[:s] + [v] + d[s + 1:], 0, 0))
        rows += [(m, d, 0, so) for so in (4, 8, 11, 15)]
        rows += [(m, [0] * 8, 0, 0), (m, [127] * 8, 0, 0), (m, [127] * 8, 15, 15), (m, d, -16, 0), (m, d, 15, 15)]
    for m, c, t, so in rows:
        print(m, *c, t, so)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--scenarios", action="store_true", help="print tools/ub.sh's sweep")
    a = ap.parse_args()
    if a.scenarios:
        scenarios()
        return
    check()
    if not a.out:
        return
    definition = json.loads((ROOT / "definition.json").read_text())
    params = [round(p["default"] * 32767) for p in definition["parameters"]]
    folder = pathlib.Path(a.out) / "content" / "presets" / "felt"
    folder.mkdir(parents=True, exist_ok=True)
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        (folder / f"{name}.json").write_text(json.dumps(preset(keys, params), indent=2) + "\n")
        print("wrote", folder / f"{name}.json")


if __name__ == "__main__":
    main()
