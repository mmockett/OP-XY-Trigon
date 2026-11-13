#!/usr/bin/env python3
"""FLICKER's kit presets, and the files a build installs for them.

    python3 tools/presets.py --out <build>/candidate     # content/presets/flicker/*.json
    python3 tools/presets.py --check                     # each region decodes to its key's sound

A kit preset holds the 24 keys (53..76) as the drum kind stores them: each key
a region whose four params carry its eight 7-bit controls and its model (the
SDK's drum_storage.hpp), marked "fm slap" (Drumline type 7, the SDK's mark),
its choke group as the play mode ("mute1" is group 1) and its own pan and
level (gain, in dB). The SDK's machine presets folder refuses regions, so
these are written into a build's content/presets/flicker/ after it is built.
default.json is the kit drum_defaults gives (kit.h).

The pad controls (TUNE, SLIT) and the per-key sends live in bits 16..30 of
stock's pad word +0x14 (the SDK's drum_storage.hpp): TUNE (5 bits, signed)
from bit 16, SLIT (4 bits, signed) from 21, FX I (3) from 25, FX II (3) from
28. Stock's preset writer exports that word's top half as the region's
"random" and its reader restores it shifted back, as max(random << 16, 0);
so a region carries them as random, with its top bit (the word's bit 31)
clear: TUNE in random's bits 0..4, SLIT 5..8, FX I 9..11, FX II 12..14.
Every region writes "random", 0 when every field is neutral.

Each send is TRK (the track's send) or the key's own 0..100 in 1% steps
(SDK 1.15): its code above is the nearest of seven steps, and random's bits
16..22 (FX I) and 23..29 (FX II) carry the amount itself (send_code, below).
"""
import argparse, json, math, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODELS = ["SWEEP", "SCRATCH", "RING", "STROKES", "DOT", "CARD", "HATCH", "ORNAMENT", "STIPPLE", "BOOP", "WAVE", "CLAW"]

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
    return [{"model": m, "c": list(v), "choke": g, "pan": 0, "gain": 0, "tune": 0, "slit": 0,
             "fx1": TRK, "fx2": TRK} for k, (m, v, g) in enumerate(zip(models, values, choke))]


def model_defaults(name):
    text = (ROOT / "kit.h").read_text()
    for n, d in re.findall(r'\{"(\w+)", \{[^}]*\}, \{([^}]*)\}\}', text):
        if n == name:
            return [int(x) for x in d.split(",")]
    raise KeyError(name)


# The keys by role (index from key 53), as stock's kits lay them out.
BD, SD, HT, MT, LT, CP, RS, CB, CH, OH, RC, CC, SH, BP, BS, CW, BD2, SD2, PH, CG, CL, BE, BH, SN = range(24)
# controls by name
PITCH, DECAY, C3, C4, C5, INK, FLUTTER, DUST = range(8)


def note_pitch(model, hz):
    """PITCH (0..127) that tunes a model to a frequency (its range is exponential, ranges.h)."""
    lo, hi = {"SWEEP": (30, 100), "RING": (55, 660), "CARD": (180, 2200), "BOOP": (65.40639, 2093.005),
              "WAVE": (27.5, 220.0), "DOT": (300, 4500)}[model]
    return max(0, min(127, round(127 * math.log(hz / lo) / math.log(hi / lo))))


def hz(note):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    n, octave = note[:-1], int(note[-1])
    return 440 * 2 ** ((names.index(n) + 12 * (octave + 1) - 69) / 12)


FIELDS = ("gain", "pan", "choke", "tune", "slit", "fx1", "fx2")


def kit(changes, every=None):
    """The default kit with changes: key -> {"model": name, "c": {slot: value}, "gain": dB, "pan": -100..100,
    "choke": 0/1, "tune": -16..15, "slit": -8..7, "fx1", "fx2": TRK or 0..100}; every: changes for every key first
    (its "c" sets those controls on every key, its other fields every key's fields)."""
    keys = default_kit()
    for k, key in enumerate(keys):
        for s, v in (every or {}).get("c", {}).items():
            key["c"][s] = v
        for f in FIELDS:
            if f in (every or {}):
                key[f] = every[f]
    for k, ch in changes.items():
        key = keys[k]
        if "model" in ch:
            key["model"] = MODELS.index(ch["model"])
            base = model_defaults(ch["model"])
            for s in (INK, FLUTTER, DUST):
                base[s] = key["c"][s]
            key["c"] = base
        for s, v in ch.get("c", {}).items():
            key["c"][s] = v
        for f in FIELDS:
            if f in ch:
                key[f] = ch[f]
    return keys


# Each key's LEVEL (dB, a cut), where a preset key sounds over its role's
# loudness (tools/calibrate.py's targets: kicks and toms -16.5 LUFS, the rest
# -19) by more than 0.75 LU at its settings, pad controls included
# (tools/demo.py --levels measures them); keys not named play at 0 dB.
LEVELS = {
    "talkie": {SH: -1, BS: -1},
    "synchromy": {RS: -2, CL: -1, SN: -2},
    "ornament": {SD: -1, RS: -1, OH: -1, CC: -2, CL: -2},
    "cartoon": {CW: -1, BH: -1},
    "nitrate": {SN: -2},
    "oramics": {CW: -2, BH: -2},
    "projector": {CW: -3},
}

# The pentatonic notes the melodic presets tune to: C D E G A.
PENTA = ["C", "D", "E", "G", "A"]

PRESETS = {
    # lo-fi house and chillhop: round, soft and clean; a sine kick with a
    # small tick, a soft snare, gentle hats, a shaker that swells, a ride's
    # bell, boops and a round drawn bass
    "talkie": kit({
        BD: {"c": {PITCH: 44, DECAY: 58, 2: 64, 3: 18, 4: 26, INK: 34}},
        SD: {"c": {PITCH: 54, DECAY: 50, 2: 56, 3: 34, 4: 28, INK: 30}, "fx2": 17},
        CP: {"c": {PITCH: 56, DECAY: 66, 2: 64, 3: 54, 4: 40}, "fx2": 33},
        CH: {"c": {DECAY: 24, 2: 46, 3: 44, INK: 34}, "pan": 18},
        OH: {"c": {DECAY: 80, 2: 46, 3: 44, INK: 34}, "pan": 18},
        PH: {"c": {DECAY: 10, 2: 46, 3: 50}, "pan": 18},
        RC: {"c": {PITCH: 74, DECAY: 66, 2: 30, 3: 70}, "pan": -24, "fx2": 17},
        SH: {"c": {PITCH: 58, DECAY: 46, 2: 88, 3: 66}, "pan": -30},
        RS: {"c": {PITCH: 86, DECAY: 46, 2: 36}, "pan": 12},
        BP: {"c": {PITCH: note_pitch("BOOP", hz("A4")), DECAY: 72, 2: 64, 3: 8, 4: 24}, "pan": -12, "fx1": 33},
        BH: {"c": {PITCH: note_pitch("BOOP", hz("E5")), DECAY: 64, 2: 64, 3: 8, 4: 24}, "pan": 16, "fx1": 33},
        BS: {"c": {PITCH: note_pitch("WAVE", hz("A1")), DECAY: 64, 2: 70, 3: 52, 4: 58}},
        CG: {"c": {PITCH: 66, DECAY: 44, 3: 44, 4: 60}, "pan": -36},
    }),
    # 1940s boom bap: every key inked heavier and run through a worn
    # projector (dust, flutter, a wider slit): a thick kick, a raspy snare,
    # dusty hats, the claw's rattle
    "newsreel": kit({
        BD: {"c": {PITCH: 40, DECAY: 60, 2: 74, 3: 40, 4: 54, INK: 70}},
        BD2: {"c": {PITCH: 34, DECAY: 70, 2: 80, 3: 30, 4: 40, INK: 70}},
        SD: {"c": {PITCH: 58, DECAY: 58, 2: 96, 3: 60, 4: 84, INK: 70}},
        SD2: {"c": {PITCH: 66, DECAY: 40, 2: 90, 3: 50, 4: 70, INK: 70}},
        CH: {"c": {DECAY: 30, 2: 40, 3: 48, INK: 60}},
        OH: {"c": {DECAY: 76, 2: 40, 3: 48, INK: 60}},
        RC: {"c": {DECAY: 62, 2: 30, 3: 50}},
        CW: {"c": {PITCH: 52, DECAY: 50, 2: 70, 3: 56, 4: 80}},
        RS: {"c": {PITCH: 80, DECAY: 44, 2: 50}},
        # the bright keys through a standard slit, a little dust: they keep their sizzle
        **{k: {"c": {DUST: 16}, "slit": 0} for k in (CH, OH, PH, RC, CC, CB, BE)},
    }, every={"c": {FLUTTER: 30, DUST: 48}, "slit": 3}),
    # tuned cards and boops: melodic percussion on a pentatonic scale, the
    # cards and dots tuned key by key with TUNE, the boops and rings as notes
    "synchromy": kit({
        **{k: {"model": "CARD", "c": {PITCH: note_pitch("CARD", hz("C5")), DECAY: 56, 2: 0, 3: 44, 4: 10}, "tune": st, "pan": pan}
           for k, st, pan in zip((CB, BE, CL, RS, SN), (0, 2, 4, 7, 9), (-40, -20, 0, 20, 40))},
        **{k: {"model": "BOOP", "c": {PITCH: note_pitch("BOOP", hz("C4")), DECAY: 66, 2: 70, 3: 20, 4: 20}, "tune": st, "pan": pan, "fx1": 33}
           for k, st, pan in zip((BP, BH, CP, SH), (0, 4, 7, 9), (-25, 25, -10, 10))},
        **{k: {"c": {PITCH: note_pitch("RING", hz(n)), DECAY: 62, 2: 20, 3: 40, 4: 30}, "pan": pan}
           for k, n, pan in zip((HT, MT, LT, CG), ("G3", "E3", "C3", "A3"), (30, 0, -30, 45))},
        BS: {"c": {PITCH: note_pitch("WAVE", hz("C2")), DECAY: 60, 2: 64, 3: 50, 4: 40}},
        BD: {"c": {PITCH: 48, DECAY: 50, 4: 30}},
        CH: {"c": {DECAY: 22, 2: 70, 3: 70}},
        OH: {"c": {DECAY: 70, 2: 70, 3: 70}},
    }, every={"slit": -2}),
    # metallic and dense: electro and IDM; hard drawn-square kicks, ringing
    # ruled hats, dense ornaments, echoing dots, odd cards, a narrow slit
    "ornament": kit({
        BD: {"c": {PITCH: 42, DECAY: 44, 2: 90, 3: 96, 4: 96, INK: 60}},
        SD: {"c": {PITCH: 70, DECAY: 40, 2: 100, 3: 90, 4: 20, INK: 56}},
        CH: {"c": {PITCH: 80, DECAY: 20, 2: 118, 3: 104, 4: 90}},
        OH: {"c": {PITCH: 80, DECAY: 70, 2: 118, 3: 104, 4: 90}},
        PH: {"c": {PITCH: 80, DECAY: 8, 2: 118, 3: 110, 4: 90}},
        RC: {"c": {PITCH: 90, DECAY: 56, 2: 110, 3: 90}},
        CC: {"c": {PITCH: 60, DECAY: 86, 2: 127, 3: 30, 4: 12}},
        CB: {"c": {PITCH: 70, DECAY: 46, 2: 92, 3: 30, 4: 30}},
        RS: {"c": {PITCH: 100, DECAY: 30, 2: 20, 3: 60, 4: 30}, "fx1": 17},
        CL: {"c": {PITCH: 110, DECAY: 30, 2: 10, 3: 90, 4: 16}},
        CP: {"c": {PITCH: 80, DECAY: 30, 2: 40, 3: 30, 4: 90}},
        SN: {"model": "HATCH", "c": {PITCH: 30, DECAY: 40, 2: 127, 3: 127, 4: 127}},
    }, every={"slit": -6}),
    # cartoons: boops that slide up and down, dots that bounce, a claw that
    # winds up, cards that drop
    "cartoon": kit({
        BP: {"c": {PITCH: 70, DECAY: 50, 2: 0, 3: 30, 4: 40}},
        BH: {"c": {PITCH: 96, DECAY: 40, 2: 127, 3: 30, 4: 0}},
        SN: {"model": "BOOP", "c": {PITCH: 50, DECAY: 34, 2: 127, 3: 70, 4: 0}},
        RS: {"c": {PITCH: 76, DECAY: 50, 2: 50, 3: 110, 4: 60}},
        CL: {"c": {PITCH: 96, DECAY: 40, 2: 30, 3: 127, 4: 40}},
        CW: {"c": {PITCH: 70, DECAY: 70, 2: 50, 3: 127, 4: 20}},
        BE: {"c": {PITCH: 80, DECAY: 46, 2: 70, 3: 50, 4: 127}},
        CB: {"c": {PITCH: 60, DECAY: 50, 2: 40, 3: 60, 4: 90}},
        HT: {"c": {PITCH: 90, DECAY: 40, 2: 127, 4: 20}},
        MT: {"c": {PITCH: 76, DECAY: 44, 2: 127, 4: 20}},
        LT: {"c": {PITCH: 62, DECAY: 48, 2: 127, 4: 20}},
        BD: {"c": {PITCH: 52, DECAY: 40, 2: 110, 4: 30}},
        CP: {"c": {DECAY: 40, 2: 30, 3: 90}},
    }),
    # lo-fi hip hop: inked hot, dusty and crunchy through a wide slit
    "nitrate": kit({
        BD: {"c": {PITCH: 42, DECAY: 56, 2: 70, 3: 34, 4: 46}},
        SD: {"c": {PITCH: 50, DECAY: 52, 2: 80, 3: 44, 4: 100}},
        SD2: {"c": {PITCH: 58, DECAY: 34, 2: 80, 3: 40, 4: 110}},
        CH: {"c": {DECAY: 28, 2: 30, 3: 40}},
        OH: {"c": {DECAY: 72, 2: 30, 3: 40}},
        SH: {"c": {PITCH: 50, DECAY: 50, 2: 76, 3: 60}},
        RS: {"c": {PITCH: 84, DECAY: 50, 2: 50}},
        BS: {"c": {PITCH: note_pitch("WAVE", hz("D2")), DECAY: 60, 2: 80, 3: 70, 4: 50}},
        BP: {"c": {PITCH: note_pitch("BOOP", hz("F4")), DECAY: 80, 2: 64, 3: 30, 4: 30}},
        # the bright keys through a standard slit, inked lighter, a little dust
        **{k: {"c": {INK: 60, DUST: 20}, "slit": 0} for k in (CH, OH, PH, RC, CC, RS)},
    }, every={"c": {INK: 100, DUST: 64, FLUTTER: 16}, "slit": 5}),
    # 808 style: long drawn sweeps for kicks, a drawn bass on the notes of a
    # line, tight hats, a clap with a long smear
    "oramics": kit({
        BD: {"c": {PITCH: 40, DECAY: 92, 2: 84, 3: 14, 4: 30}},
        BD2: {"c": {PITCH: 30, DECAY: 110, 2: 96, 3: 10, 4: 20}, "tune": -2},
        BS: {"c": {PITCH: note_pitch("WAVE", hz("F1")), DECAY: 92, 2: 30, 3: 30, 4: 80}},
        BP: {"model": "WAVE", "c": {PITCH: note_pitch("WAVE", hz("G#1")), DECAY: 92, 2: 30, 3: 30, 4: 80}},
        BH: {"model": "WAVE", "c": {PITCH: note_pitch("WAVE", hz("C2")), DECAY: 92, 2: 30, 3: 30, 4: 80}},
        CW: {"model": "WAVE", "c": {PITCH: note_pitch("WAVE", hz("D#2")), DECAY: 92, 2: 30, 3: 30, 4: 80}},
        CH: {"c": {DECAY: 18, 2: 70, 3: 80}},
        OH: {"c": {DECAY: 64, 2: 70, 3: 80}},
        PH: {"c": {DECAY: 8, 2: 70, 3: 80}},
        CP: {"c": {DECAY: 76, 2: 90, 3: 40, 4: 40}, "fx2": 33},
        SD: {"c": {PITCH: 64, DECAY: 50, 2: 80, 3: 70}},
        RS: {"c": {PITCH: 96, DECAY: 30, 2: 20}},
        CB: {"c": {PITCH: 72, DECAY: 50}},
    }),
    # techno: inked kicks with a hard tick, the claw rattling in sixteenths,
    # tight hats, a dry clap, the projector's flutter just showing
    "projector": kit({
        BD: {"c": {PITCH: 44, DECAY: 44, 2: 64, 3: 50, 4: 92, INK: 84}},
        BD2: {"c": {PITCH: 40, DECAY: 56, 2: 70, 3: 60, 4: 70, INK: 96}},
        CW: {"c": {PITCH: 76, DECAY: 30, 2: 100, 3: 64, 4: 60}},
        CH: {"c": {DECAY: 18, 2: 80, 3: 90}},
        OH: {"c": {DECAY: 60, 2: 80, 3: 90}},
        PH: {"c": {DECAY: 6, 2: 80, 3: 96}},
        CP: {"c": {PITCH: 70, DECAY: 40, 2: 70, 3: 30, 4: 20}},
        SD: {"model": "STROKES", "c": {PITCH: 64, DECAY: 50, 2: 100, 3: 40, 4: 30}},
        RC: {"c": {PITCH: 80, DECAY: 50, 2: 60, 3: 50}},
        RS: {"c": {PITCH: 90, DECAY: 30, 2: 30, 3: 30, 4: 30}},
        SH: {"c": {PITCH: 76, DECAY: 30, 2: 100, 3: 10}},
    }, every={"c": {FLUTTER: 10}}),
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
    assert -16 <= key["tune"] <= 15 and -8 <= key["slit"] <= 7 and send_ok(key["fx1"]) and send_ok(key["fx2"])
    return ((key["tune"] & 31) | (key["slit"] & 15) << 5 | send_code(key["fx1"]) << 9 | send_code(key["fx2"]) << 12
            | send_index(key["fx1"]) << 16 | send_index(key["fx2"]) << 23)


def unpack_random(random):
    """What every firmware decodes from the word stock's reader restores, max(random << 16, 0)
    as a 32-bit int: the pad controls and the sends' codes (bits 16 and up of random drop out)."""
    word = (random << 16) & 0xffffffff
    word = 0 if word >> 31 else word
    field = lambda low, bits: (word >> low) & ((1 << bits) - 1)
    tune, slit = field(16, 5), field(21, 4)
    return {"tune": tune - 32 if tune & 16 else tune, "slit": slit - 16 if slit & 8 else slit, "fx1": field(25, 3), "fx2": field(28, 3)}


def region(index, key):
    value = kit_value(key["model"], key["c"])
    r = {"hikey": 53 + index, "model": "fm slap", "params": [(value >> (15 * w)) & 0x7fff for w in range(4)]}
    if key["choke"]:
        r["playmode"] = "mute1"
    if key["pan"]:
        r["pan"] = key["pan"]
    r["gain"] = key["gain"]
    r["random"] = pad_random(key)   # written always: 0 when every pad field is neutral
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
    p["type"] = "flicker"
    p["regions"] = [region(i, k) for i, k in enumerate(keys)]
    return p


def check():
    """Each region decodes, as the SDK's drum_unpack does, to its key's model and controls."""
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        for i, k in enumerate(keys):
            r = region(i, k)
            value = sum(p << (15 * w) for w, p in enumerate(r["params"]))
            assert value >> 56 == k["model"] < 12, (name, i)
            assert [(value >> (7 * s)) & 127 for s in range(8)] == k["c"], (name, i)
            assert all(0 <= c <= 127 for c in k["c"]) and -30 <= r["gain"] <= 0 and -100 <= r.get("pan", 0) <= 100, (name, i)
            # the pad fields come back as they went in, and the word's bit 31 stays clear
            assert "random" in r, (name, i)
            random = r["random"]
            assert 0 <= random < 1 << 30 and not random & 0x8000, (name, i, random)
            back = unpack_random(random)
            assert back == {"tune": k["tune"], "slit": k["slit"], "fx1": send_code(k["fx1"]), "fx2": send_code(k["fx2"])}, (name, i, back)
            # SDK 1.15 reads each send back as the amount written. The kits were
            # voiced on the seven steps, so each amount is the percent SDK 1.15
            # shows for (and turns on from) the code these presets carried before
            # SDK 1.15 (drum_send_percent), and the code is unchanged.
            assert sends_read(random) == [k["fx1"], k["fx2"]], (name, i, random)
            assert all(a is TRK or a == STEPS[send_code(a) - 1] for a in (k["fx1"], k["fx2"])), (name, i)
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
    folder = pathlib.Path(a.out) / "content" / "presets" / "flicker"
    folder.mkdir(parents=True, exist_ok=True)
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        (folder / f"{name}.json").write_text(json.dumps(preset(keys, params), indent=2) + "\n")
        print("wrote", folder / f"{name}.json")


if __name__ == "__main__":
    main()
