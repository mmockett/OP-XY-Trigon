#!/usr/bin/env python3
"""Writes FELT's artwork, panel.svg.fragment, and art.h, the node numbers and
the plate's geometry display.cpp places by. Run it after changing anything
here and commit both.

    python3 -I felt/tools/make_panel.py

The screen is a textbook plate of Cage's prepared piano, isometric: three
unison strings over a near-black iron plate, from a fixed far bridge to the
hitch rail, with the near bridge sliding under them. Each encoder owns a part
and its grey: the near bridge is encoder 1 (hollow, 191919 with a CDCDCD
edge), the damper encoder 2 (4B4B4B), the striker encoder 3 (969696) and the
preparation wedged between the strings encoder 4 (CDCDCD). Every model draws
its own striker and preparation in those places and greys.

Isometric rules: edges at 30 degrees; top and left faces share a part's grey,
its right face one ramp step darker; a 0.5 px black seam on every edge (light
edges on a black solid); hairlines over solids. A box is three faces, bound
as three nodes about one pivot, so they move together. The strings and the
bead chain or the bow's thread are the four reshaping outlines, unfilled.
The plate, the far bridge, the hitch rail and the hammer's slot never move:
plain artwork. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "felt"
PATHS, POINTS = 4, 64
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make

C30, S30 = math.cos(math.radians(30)), 0.5

# ---------------------------------------------------------------- the plate (iso units: a pixel along an axis)
OX, OY = 190.0, 79.0     # where the plate's back corner (0, 0, 0) lands on screen
PL, PD, PT = 172.0, 56.0, 6.0   # the plate's length (x, along the strings), depth (y, across) and thickness
ZS = 14.0                # the strings' height over the plate
STRINGS = (20.0, 30.0, 40.0)    # the three unisons (y)
SYMP = (5.0, 10.0)       # the sympathetic strings behind them (y)
FAR = (5.0, 9.0)        # the far bridge (x)
HITCH = (164.0, 168.0)   # the hitch rail (x)
STRING_X0, STRING_X1 = 5.0, 167.0   # the strings run from the far bridge to the hitch rail
NODE_X = 7.0             # the far end of the speaking length
BRIDGE_LO, BRIDGE_HI = 150.0, 84.0  # the near bridge's centre at TUNE 0 and 127
HAMMER_X = 36.0          # the striker's line across the strings
DAMPER_X = 20.0          # the damper's
SLOT = (31.0, 41.0, 22.0, 36.0)     # the hammer's slot in the plate (x0, x1, y0, y1)

# the parts' sizes (iso units: along, across, tall), shared with display.cpp
FELT = (18.0, 34.0, 5.0)        # the hammer's felt at scale 1, the softest
MOLDING = (10.0, 30.0, 3.0)
SHANK = 6.0
PLANK = (6.0, 24.0, 3.0)
PAD = (4.0, 22.0, 2.0)
DAMPER = (14.0, 30.0, 7.0)
PEDAL = (12.0, 11.0, 4.0)
RUBBER = (14.0, 6.0, 12.0)      # its foot 5 under the strings
PAPER = (12.0, 34.0, 0.8)
MUTE = (6.0, 34.0, 3.0)
BFELT = (12.0, 28.0, 4.0)       # the felt laid against the near bridge
LIDP = (6.0, 5.0, 4.0)          # LID's rubber pads
LID = (28.0, 34.0, 3.0)
LIP = (28.0, 2.5, 3.0)
TUFT = (6.0, 24.0, 3.0)
BRUSH = (8.0, 26.0, 4.0)
TOGGLE = (14.0, 3.0, 3.0)
PEG = (3.5, 3.5, 14.0)
SCREW_TOP, SCREW_FOOT = 7.0, 8.0    # the screw over and under the strings
NUT = 1.5                        # a nut's thickness

# greys
G1, G2, G3, G4 = "#191919", "#4B4B4B", "#969696", "#CDCDCD"   # the grey code, encoders 1..4
G1R, G2R, G3R, G4R = "#000000", "#323232", "#7D7D7D", "#AFAFAF"   # their right faces, a ramp step darker
IRON, IRON_R, SEAM = "#191919", "#000000", "#646464"
WOOD, WOOD_R = "#232323", "#101010"   # the far bridge and hitch rail
NEUT, NEUT_R = "#323232", "#191919"   # the hammer's molding and shank when no encoder owns them
# The palette: each entry lends a fill, a stroke and its width (which scales
# with the node). 1..8 the grey code, base and right face; 9..10 the
# strings (outlines drawn at scale 2: 1 px); 11..12 the neutral wood; 13 a
# CDCDCD hairline (encoder 1's mark, the bead chain); 14 encoder 2's mark;
# 15 the bow's thread; 0 black (parked, and AGE's seam).
PALETTE = [("#000000", "#000000", 0.5),
           (G1, G4, 0.5), (G1R, G4, 0.5), (G2, "#000000", 0.5), (G2R, "#000000", 0.5),
           (G3, "#000000", 0.5), (G3R, "#000000", 0.5), (G4, "#000000", 0.5), (G4R, "#000000", 0.5),
           ("#9A9A9A", "#9A9A9A", 0.5), ("#7A7A7A", "#7A7A7A", 0.5),
           (NEUT, "#000000", 0.5), (NEUT_R, "#000000", 0.5),
           (G4, G4, 0.5), (G2, G2, 0.75), (G3, G3, 0.5)]


def P(x, y, z=0.0):
    """An iso point, relative to the plate's back corner on screen."""
    return ((x - y) * C30, (x + y) * S30 - z)


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(out):
    """The device's rules for an outline: at most 64 points, apart, some area,
    and no turn sharper than SHARPEST (the OP-XY strokes with mitred joins
    and no limit, so a turn near straight back draws a long spike)."""
    n = len(out)
    assert 3 <= n <= 64, n
    for i in range(n):
        (ax, ay), (bx, by), (cx, cy) = out[i - 1], out[i], out[(i + 1) % n]
        assert math.hypot(bx - ax, by - ay) >= 0.125, ("points merge", out[i - 1], out[i])
        ux, uy, vx, vy = bx - ax, by - ay, cx - bx, cy - by
        turn = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        assert turn <= SHARPEST, f"turn of {turn:.1f} degrees at point {i}: {out[i]}"
    area = sum(out[i][0] * out[(i + 1) % n][1] - out[(i + 1) % n][0] * out[i][1] for i in range(n)) / 2
    assert abs(area) > 0.05, ("no area", out)


def poly(points):
    out = []
    for p in points:
        q = (round(p[0], 2), round(p[1], 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert len(set(out)) == len(out), "repeated point"
    check(out)
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in out) + " Z"


def ribbon(points, w=0.5):
    """A closed ribbon along a polyline: how a line is drawn."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        left.append((points[i][0] - dy / length * w / 2, points[i][1] + dx / length * w / 2))
        right.append((points[i][0] + dy / length * w / 2, points[i][1] - dx / length * w / 2))
    loop = left + right[::-1]
    k = max(1, n // 2)
    return loop[k:] + loop[:k]


def faces(x0, y0, z0, w, d, h):
    """A box's three visible faces (left, right, top), relative to the iso origin."""
    top = [P(x0, y0, z0 + h), P(x0 + w, y0, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0, y0 + d, z0 + h)]
    left = [P(x0, y0 + d, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0 + w, y0 + d, z0), P(x0, y0 + d, z0)]
    right = [P(x0 + w, y0, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0 + w, y0 + d, z0), P(x0 + w, y0, z0)]
    return left, right, top


def disc(cx, cy, cz, r, n=16):
    """A flat iso disc (a circle lying in the plane z = cz) as an outline."""
    return [P(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n), cz) for i in range(n)]


def hull(points):
    pts = sorted(set((round(x, 3), round(y, 3)) for x, y in points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def hexagon(r, z):
    return [P(r * math.cos(math.radians(60 * i + 15)), r * math.sin(math.radians(60 * i + 15)), z) for i in range(6)]


# ---------------------------------------------------------------- the artwork
DOC = []      # in drawing order: ("static", d, fill, stroke, width), ("node", name) or ("path", index)
NODES = {}    # name -> (points or None, circle or None, fill, stroke)
ORDER = []    # bound node names, in node-number order


def static(points, fill, stroke=SEAM, w=0.5):
    DOC.append(("static", poly([(OX + x, OY + y) for x, y in points]), fill, stroke, w))


def static_box(x0, y0, z0, w, d, h, base, right):
    for pts, fill in zip(faces(x0, y0, z0, w, d, h), (base, right, base)):
        static(pts, fill)


def node(name, points=None, circle=None, fill=G3, stroke="#000000"):
    assert name not in NODES
    NODES[name] = (points, circle, fill, stroke)
    ORDER.append(name)
    DOC.append(("node", name))


def box(name, x0, y0, z0, w, d, h, base, right):
    """A box bound as three nodes, name_l, name_r and name_t, about one pivot (the iso origin)."""
    for suffix, pts, fill in zip("lrt", faces(x0, y0, z0, w, d, h), (base, right, base)):
        node(f"{name}_{suffix}", pts, fill=fill)


def path(index):
    DOC.append(("path", index))


SCREW = [(-0.7, 6), (-0.7, -4.5), (-2.6, -4.5), (-2.6, -6.5), (2.6, -6.5), (2.6, -4.5), (0.7, -4.5), (0.7, 6)]


def build_scene():
    # ---- the plate: near-black iron, black right face, 646464 seams
    static_box(0, 0, -PT, PL, PD, PT, IRON, IRON_R)
    # the hammer's slot, where the striker rises through the plate
    x0, x1, y0, y1 = SLOT
    static([P(x0, y0), P(x1, y0), P(x1, y1), P(x0, y1)], "#000000")
    # the far bridge and the hitch rail, which never move
    static_box(FAR[0], 1, 0, FAR[1] - FAR[0], PD - 2, ZS, WOOD, WOOD_R)
    static_box(HITCH[0], 1, 0, HITCH[1] - HITCH[0], PD - 2, ZS, WOOD, WOOD_R)

    # ---- encoder 1: the near bridge, hollow, pivot at its top centre
    box("bridge", -3, -(PD - 2) / 2, -ZS, 6, PD - 2, ZS, G1, G1R)

    # ---- encoder 3: the striker from beneath, each part about its top
    # centre. The hammer: shank, molding (neutral wood, or the grey of the
    # encoder that owns KNOCK), felt; TACK's drawing pin on the felt; PLANK's
    # plank and its felt pad (encoder 4)
    node("shank", [(-1, 0), (1, 0), (1, SHANK), (-1, SHANK)], fill=NEUT)
    w, d, h = MOLDING
    box("molding", -w / 2, -d / 2, -h, w, d, h, NEUT, NEUT_R)
    w, d, h = FELT
    box("felt", -w / 2, -d / 2, -h, w, d, h, G3, G3R)
    node("tack", disc(0, 0, 0, 3.0), fill=G3)
    w, d, h = PLANK
    box("plank", -w / 2, -d / 2, -h, w, d, h, G3, G3R)
    w, d, h = PAD
    box("pad", -w / 2, -d / 2, 0, w, d, h, G4, G4R)

    # ---- the sympathetic strings behind the unisons, and the felt PEDAL lifts off them
    a, b = P(STRING_X0, 0, 0), P(STRING_X1, 0, 0)
    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    for k in range(2):
        node(f"symp_{k}", ribbon([(a[0] - mid[0], a[1] - mid[1]), (b[0] - mid[0], b[1] - mid[1])], 0.4),
             fill="#7A7A7A", stroke="#7A7A7A")
    w, d, h = PEDAL
    box("pedal", -w / 2, -d / 2, 0, w, d, h, G3, G3R)
    # CHAIN's peg behind the strings, which holds the chain's end (encoder 2)
    w, d, h = PEG
    box("peg", -w / 2, -d / 2, 0, w, d, h, G2, G2R)

    # ---- the strings and what is wedged between them, back to front
    path(0)
    # between the first and second strings (y = 25)
    w, d, h = RUBBER
    box("rubber_a", -w / 2, -d / 2, -5, w, d, h, G4, G4R)
    node("screw_shank", [(-1.1, -SCREW_TOP), (1.1, -SCREW_TOP), (1.1, SCREW_FOOT), (-1.1, SCREW_FOOT)], fill=G4)
    node("bolt_head_side", hull(hexagon(5.0, -SCREW_FOOT) + hexagon(5.0, -SCREW_FOOT + 2.5)), fill=G4R)
    node("bolt_head", hexagon(5.0, -SCREW_FOOT + 2.5), fill=G4)
    node("bolt_shank", [(-1.8, -SCREW_TOP + 1), (1.8, -SCREW_TOP + 1), (1.8, SCREW_FOOT - 2.5), (-1.8, SCREW_FOOT - 2.5)], fill=G4)
    node("screw_head", disc(0, 0, SCREW_TOP, 4.2), fill=G4)
    node("nut_side", hull(hexagon(3.6, 0) + hexagon(3.6, NUT)), fill=G2R)
    node("nut", hexagon(3.6, NUT), fill=G2)
    node("plank_bolt_0", hull(hexagon(2.6, -4) + hexagon(2.6, 4)), fill=G2)
    for k in range(2):
        node(f"brush_screw_{k}", SCREW, fill=G4)
    path(1)
    # between the second and third strings (y = 35); the paper woven over
    # the first two and under the third
    w, d, h = RUBBER
    box("rubber_b", -w / 2, -d / 2, -5, w, d, h, G4, G4R)
    w, d, h = PAPER
    box("paper", -w / 2, -d / 2, 0, w, d, h, G4, G4R)
    node("plank_bolt_1", hull(hexagon(2.6, -4) + hexagon(2.6, 4)), fill=G2)
    node("brush_screw_2", SCREW, fill=G4)
    path(2)

    # ---- on the strings
    # encoder 1's mark and encoder 2's: hairlines lying across the strings
    across = [P(0, -16, 0), P(0, 16, 0)]
    node("mark_1", ribbon(across, 0.5), fill=G4, stroke=G4)
    node("mark_2", ribbon(across, 0.8), fill=G2, stroke=G2)
    # encoder 2: the damper, pivot at its foot's centre
    w, d, h = DAMPER
    box("damper", -w / 2, -d / 2, 0, w, d, h, G2, G2R)
    # TACK's felt mute across the strings (encoder 4)
    w, d, h = MUTE
    box("mute", -w / 2, -d / 2, 0, w, d, h, G4, G4R)
    # encoder 1: a felt laid against the near bridge (PLUCK's MUTE, PLANK's
    # BLOOM), pivot at its foot by the bridge
    w, d, h = BFELT
    box("bfelt", -w, -d / 2, 0, w, d, h, G1, G1R)
    # the bead chain (path 3), its jingles (encoder 1) and beads (encoder 3)
    path(3)
    for k in range(4):
        node(f"jingle_{k}", disc(0, 0, 0, 2.6), fill=G1, stroke=G4)
    for k in range(8):
        node(f"bead_{k}", circle=(0, 0, 2.8), fill=G3)
    # LID: its rubber pads on the strings (encoder 4) and the lid (encoder 3)
    # with its lip (encoder 1), pivots at their feet
    w, d, h = LIDP
    for k in range(2):
        box(f"lidpad_{k}", -w / 2, -d / 2, 0, w, d, h, G4, G4R)
    w, d, h = LID
    box("lid", -w / 2, -d / 2, 0, w, d, h, G3, G3R)
    w, d, h = LIP
    box("lip", -w / 2, 0, 0, w, d, h, G1, G1R)
    # BRUSH: its bristles (encoder 1), its handle and head (encoder 3)
    w, d, h = TUFT
    box("tuft", -w / 2, -d / 2, 0, w, d, h, G1, G1R)
    w, d, h = BRUSH
    left, _, top = faces(-20, -2, h, 16, 4, 3)
    node("handle_l", left, fill=G3)
    node("handle_t", top, fill=G3)
    box("brush", -w / 2, -d / 2, 0, w, d, h, G3, G3R)
    # PLUCK: the fingertip and the quill (encoder 3), pivot at the tip on the string
    node("fingertip", disc(0, 3.2, 0, 3.4), fill=G3)
    plan = [(-0.45, 0), (0.45, 0), (1.0, 8), (2.4, 18), (3.0, 28), (2.4, 36), (0.8, 42), (-0.8, 42), (-1.5, 34),
            (-1.4, 22), (-0.9, 10)]
    node("quill", [P(x, y, 0) for x, y in plan], fill=G3)
    # BOW: the thread's toggle (encoder 4), pivot at its centre
    w, d, h = TOGGLE
    box("toggle", -w / 2, -d / 2, -h / 2, w, d, h, G4, G4R)
    # AGE: a dark seam across the preparation
    node("seam", ribbon([P(-5, 0, 0), P(5, 0, 0)], 0.6), fill="#000000", stroke="#000000")


def build():
    build_scene()
    n_header = 8
    first_header = PATHS
    index = {name: PATHS + n_header + k for k, name in enumerate(ORDER)}
    bound = PATHS + n_header + len(ORDER)
    lines = [f'<g id="{ID}_panel">', "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i in range(8):
        lines.append(f'<path id="{ID}_node_{first_header + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                     f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{greys[i]}" stroke="{greys[i]}" stroke-width="0.5"/>')
    for i in range(16):
        x = [5, 125, 245, 365, 65, 185, 305, 425, 5, 125, 245, 365, 65, 185, 305, 425][i]
        y = [10, 10, 10, 10, 17.4, 17.4, 17.4, 17.4, 33, 33, 33, 33, 33, 33, 33, 33][i]
        col = (["#CBCBCB"] * 2 + ["#000000"] * 2) * 2 + ["#5A5A5A"] * 4 + ["#707070"] * 4
        size = [10] * 4 + [20] * 4 + [10] * 4 + [13] * 4
        lines.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{col[i]}" '
                     f'font-family="UniversNextPro-Light" font-size="{size[i]}px">-</text>')
    statics = 0
    for item in DOC:
        if item[0] == "static":
            _, d, fill, stroke, w = item
            lines.append(f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
            statics += 1
        elif item[0] == "path":
            # a reshaping outline, unfilled, authored as distinct points round an ellipse
            p = item[1]
            pts = [(20 * math.cos(2 * math.pi * i / POINTS), 12 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            lines.append(f'<path id="{ID}_node_{p}" transform="matrix(1 0 0 1 640 320)" d="{poly(pts)}" fill="none" '
                         f'stroke="#9A9A9A" stroke-width="0.5"/>')
        else:
            name = item[1]
            points, circle, fill, stroke = NODES[name]
            idx = index[name]
            if circle:
                cx, cy, r = circle
                lines.append(f'<circle id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" cx="{fmt(cx)}" cy="{fmt(cy)}" '
                             f'r="{fmt(r)}" fill="{fill}" stroke="{stroke}" stroke-width="0.5"/>')
            else:
                lines.append(f'<path id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                             f'fill="{fill}" stroke="{stroke}" stroke-width="0.5"/>')
    for i, (fill, stroke, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    (ROOT / "panel.svg.fragment").write_text(text)
    assert len(text.encode()) <= 64 * 1024, len(text.encode())
    assert bound <= 256, bound

    q = lambda v: str(round(v * 1024))
    consts = {
        "origin_x": OX, "origin_y": OY, "zs": ZS,
        "string_0": STRINGS[0], "string_1": STRINGS[1], "string_2": STRINGS[2], "symp_y0": SYMP[0], "symp_y1": SYMP[1],
        "string_x0": STRING_X0, "string_x1": STRING_X1, "node_x": NODE_X,
        "bridge_lo": BRIDGE_LO, "bridge_hi": BRIDGE_HI, "hammer_x": HAMMER_X, "damper_x": DAMPER_X,
        "felt_h": FELT[2], "molding_h": MOLDING[2], "plank_h": PLANK[2], "pad_h": PAD[2],
        "pedal_y": PEDAL[1] / 2 + 2, "paper_h": PAPER[2], "lidpad_h": LIDP[2], "lid_d": LID[1], "tuft_h": TUFT[2],
        "screw_top": SCREW_TOP, "nut_h": NUT, "peg_h": PEG[2], "symp_mid_x": (STRING_X0 + STRING_X1) / 2,
    }
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers, and the plate's",
            "// iso geometry in Q10 (1024 a unit; an iso unit is a pixel along an axis).",
            "#include <opxy/types.hpp>", "", "namespace felt {", "namespace art {",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};"]
    head += [f"const opxy::i32 {k} = {q(v)};" for k, v in consts.items()]
    head += ["enum Node : opxy::u32 {"]
    head += [f"    {name} = {index[name]}," for name in ORDER]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    # the definition declares the same frame
    import json
    dpath = ROOT / "definition.json"
    definition = json.loads(dpath.read_text())
    definition["resources"]["display"]["nodes"] = bound
    dpath.write_text(json.dumps(definition, indent=2) + "\n")
    print(f"wrote panel.svg.fragment ({len(text.encode())} bytes), art.h and definition.json's frame: "
          f"{bound} nodes, {statics} static paths")


if __name__ == "__main__":
    build()
