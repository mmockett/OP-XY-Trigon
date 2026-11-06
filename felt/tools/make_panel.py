#!/usr/bin/env python3
"""Writes FELT's artwork, panel.svg.fragment, and art.h, the node numbers and
anchors display.cpp places. Run it after changing anything here and commit
both.

    python3 -I felt/tools/make_panel.py

The screen is one note of an old upright piano's action, side on: the string
standing at the left (from the pressure bar to the bridge), the iron plate's
edge beside it, and to its right the hammer, the damper, the wippen and jack,
and the key along the bottom on its balance rail. Rails, flanges and the
keybed never move, so they are plain artwork, filled dark grey. Everything a
control or a hit moves is a bound node, drawn here around its own pivot
(0, 0), unfilled; display.cpp places it. Parts that move together share a
pivot and a transform. Lines are thin closed ribbons; no path has more than
one subpath or 64 points. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "felt"
PATHS, POINTS = 4, 64

# ---------------------------------------------------------------- anchors (screen px)
SX = 90.0            # the unison pair's centre line
STOP = 44.0          # the pressure bar's underside: the string's top
STRIKE = 84.0        # where the hammer's felt meets the string
DAMP = 58.0          # the middle of the damper's felt
BRIDGE_HI, BRIDGE_LO = 170.0, 206.0   # the bridge's top at TUNE's top and bottom
H = (124.0, 160.0)   # the hammer butt's centre pin
D = (154.0, 125.0)   # the damper lever's centre pin, on its flange over the main rail
W = (206.0, 189.0)   # the wippen's centre pin
B = (330.0, 210.0)   # the key's balance pin (the key's underside)
KEY_TOP, KEY_BOTTOM = 198.0, 210.0
KEY_BACK, KEY_FRONT = 130.0, 452.0
CAPSTAN_X = 180.0
JACK_X = 134.0
RAIL_Y = 124.0       # the hammer rail's centre
HINGE = (396.0, 190.0)   # the fallboard's hinge
TUNE_PIN = (SX + 1.0, 41.0)
DX = 26.0            # how far right of the first sketch the action stands
SPOOL_BOX = (316.0, 42.0, 376.0, 136.0)   # the player's spool box, its side frame
ROLL, TAKE_UP = (350.0, 66.0), (352.0, 114.0)
TRACKER = (334.0, 90.0)

# greys
INK, MID, DIM, FAINT = "#E6E6E6", "#ACACAC", "#767676", "#444444"
WOOD = "#181818"     # the static frame's dark body (GALILEO's, THUNDER's)
IRON = "#262626"     # iron fittings
EDGE = "#4A4A4A"     # their outline
# The palette: 0 black (parked); 1..8 a ramp at 1 px; 9..11 the reshaping
# outlines' greys at half weight (drawn at scale 2: 1 px as drawn); 12 a
# ramp step at 1 px for fittings; 13..15 spare.
RAMP = ["#2E2E2E", "#444444", "#5C5C5C", "#767676", "#909090", "#ACACAC", "#C8C8C8", "#E6E6E6"]
PALETTE = [("#000000", 1.0)] + [(g, 1.0) for g in RAMP] + [
    ("#E6E6E6", 0.5), ("#ACACAC", 0.5), ("#767676", 0.5), ("#5C5C5C", 0.5), ("#C8C8C8", 0.5), ("#909090", 0.5), ("#000000", 0.5)]


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(out):
    """The device's rules for an outline: points apart, no turn straight back."""
    n = len(out)
    assert 3 <= n <= 64, n
    for i in range(n):
        a, b, c = out[i], out[(i + 1) % n], out[(i + 2) % n]
        assert math.hypot(b[0] - a[0], b[1] - a[1]) >= 0.125, ("points merge", a, b)
        d1 = (b[0] - a[0], b[1] - a[1])
        d2 = (c[0] - b[0], c[1] - b[1])
        l1, l2 = math.hypot(*d1), math.hypot(*d2)
        cosang = (d1[0] * d2[0] + d1[1] * d2[1]) / (l1 * l2)
        assert cosang > -math.cos(math.radians(2.5)), ("turns straight back", b)


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


def line(x0, y0, x1, y1, w=0.5, steps=1):
    return ribbon([(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps) for i in range(steps + 1)], w)


def rect(x0, y0, x1, y1, r=0.0, steps=3):
    if r <= 0:
        return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def ellipse(cx, cy, rx, ry, n=20, rot=0.0):
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    out = []
    for i in range(n):
        a = 2 * math.pi * i / n
        x, y = rx * math.cos(a), ry * math.sin(a)
        out.append((cx + x * c - y * s, cy + x * s + y * c))
    return out


def arc(cx, cy, r, a0, a1, n=12, w=0.5):
    return ribbon([(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
                   for i in range(n + 1)], w)


def rotate(points, deg, ox=0.0, oy=0.0):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(ox + (x - ox) * c - (y - oy) * s, oy + (x - ox) * s + (y - oy) * c) for x, y in points]


def shift(points, dx, dy):
    return [(x + dx, y + dy) for x, y in points]


def rel(points, origin):
    return [(x - origin[0], y - origin[1]) for x, y in points]


# ---------------------------------------------------------------- static artwork
STATIC = []   # (d, stroke, width, fill)


def static(points, col=EDGE, w=1.0, fill="none"):
    STATIC.append((poly(points), col, w, fill))


def static_circle(cx, cy, r, col=EDGE, w=1.0, fill="none"):
    STATIC.append((("circle", cx, cy, r), col, w, fill))


def frame():
    # The iron plate's edge at the left, cast with round lightening holes, and
    # the pressure bar it carries over the strings, with its screw
    plate = [(0, 38), (48, 38), (55, 42), (59, 50), (60, 72), (58, 118), (59, 168), (62, 196), (65, 222), (0, 222)]
    static(plate, EDGE, 1.0, IRON)
    static_circle(28, 96, 14, EDGE, 1.0, "#000000")
    static_circle(31, 168, 10, EDGE, 1.0, "#000000")
    static(rect(16, 126, 44, 135, 2.5, 2), EDGE, 1.0, WOOD)      # a rib cast across it
    static([(56, 38), (SX + 10, 38), (SX + 10, STOP), (56, STOP)], EDGE, 1.0, IRON)
    static_circle(SX + 5, 41, 1.8, EDGE, 0.8, WOOD)
    # the main rail in section, and the flanges screwed to it; the wippen's
    # rail in section over its pin
    static(rect(112 + DX, 132, 132 + DX, 150, 1.5, 2), EDGE, 1.0, WOOD)
    static([(112 + DX, 141), (112 + DX, 149), (106 + DX, 156), (101 + DX, 156), (101 + DX, 151), (106 + DX, 145)], EDGE, 1.0, IRON)   # the butt's
    static([(D[0] - 4, 132), (D[0] - 4, D[1] - 2), (D[0] - 1, D[1] - 5), (D[0] + 4, D[1] - 5), (D[0] + 4, 132)], EDGE, 1.0, IRON)   # the damper's
    static(rect(W[0] - 9, W[1] - 22, W[0] + 9, W[1] - 10, 1.5, 2), EDGE, 1.0, WOOD)                       # the wippen's rail
    static([(W[0] - 4, W[1] - 10), (W[0] + 4, W[1] - 10), (W[0] + 4, W[1] + 3), (W[0] - 4, W[1] + 3)], EDGE, 1.0, IRON)
    # the let-off rail and its button over the jack's heel
    static(rect(120 + DX, 160, 134 + DX, 166, 1.2, 1), EDGE, 1.0, WOOD)
    static(rect(122 + DX, 166, 127 + DX, 170, 1, 1), EDGE, 1.0, IRON)
    # the keybed, the balance rail, the front rail and the key slip
    static([(110, 214), (480, 214), (480, 222), (110, 222)], EDGE, 1.0, WOOD)
    static(rect(B[0] - 9, KEY_BOTTOM + 0.5, B[0] + 9, 214, 1, 1), EDGE, 1.0, WOOD)
    static(rect(KEY_FRONT - 30, KEY_BOTTOM + 1, KEY_FRONT - 18, 214, 1, 1), EDGE, 1.0, WOOD)
    # the player's spool box in the upper case, seen from its side: the
    # music roll above, the take-up spool below, the paper running down
    # between them over the tracker bar, whose holes play the key; the
    # spools turn in the box's side frame
    static([(SPOOL_BOX[0], SPOOL_BOX[1]), (SPOOL_BOX[2], SPOOL_BOX[1]), (SPOOL_BOX[2], SPOOL_BOX[3]), (SPOOL_BOX[0] + 10, SPOOL_BOX[3]),
            (SPOOL_BOX[0], SPOOL_BOX[3] - 10)], EDGE, 1.0, WOOD)
    for (cx, cy), r in ((ROLL, 15.0), (TAKE_UP, 12.0)):
        static_circle(cx, cy, r, "#5C5C5C", 1.0, "#000000")
    static_circle(ROLL[0], ROLL[1], 10.5, "#3C3C3C", 1.0)   # the paper wound on the roll
    tb = TRACKER
    static(rect(tb[0] - 3, tb[1] - 10, tb[0] + 3, tb[1] + 10, 1.5, 2), EDGE, 1.0, IRON)
    paper = [(ROLL[0] - 10.4, ROLL[1] + 1.5), (tb[0] + 3.6, tb[1] - 9), (tb[0] + 3.6, tb[1] + 9), (TAKE_UP[0] - 11.8, TAKE_UP[1] - 2)]
    STATIC.append((poly(ribbon(paper, 0.5)), "#8C8C8C", 0.8, "none"))
    # the front panel above the keys and the fallboard's hinge
    static([(HINGE[0] - 14, 38), (HINGE[0] - 2, 38), (HINGE[0] - 2, 128), (HINGE[0] - 6, 132), (HINGE[0] - 14, 132)], EDGE, 1.0, WOOD)
    static_circle(HINGE[0], HINGE[1], 2.2, EDGE, 0.8, IRON)


# ---------------------------------------------------------------- bound parts
NODES = []   # (name, points, circle, fill)
ANCHORS = {}


def node(name, points=None, circle=None, fill="none"):
    NODES.append((name, points, circle, fill))


def mirror(points):
    """Left for right: an implement reaching in from the plate's side."""
    return [(-x, y) for x, y in points][::-1]


def scaled(points, k):
    return [(x * k, y * k) for x, y in points]


def parts():
    # ---------------- the neighbouring strings behind (the pedal's halo; a cluster's keys)
    for k in range(6):
        node(f"neighbour_{k}", line(0, -64, 0, 64, 0.5, 1))
    # ---------------- the bridge on the soundboard: a hardwood cap with its
    # pin; placed at its top, centred under the string
    node("bridge", [(-16, 1.5), (-13, -1.2), (1.5, -1.2), (3.5, 1), (3.5, 10), (-16, 10)])
    node("bridge_pin", line(-2, -5, -2, -0.5, 1.2, 1))
    # ---------------- the tuning hammer on the string's pin (BOW's BEND)
    node("tuning_lever", [(-2.5, -2.5), (2.5, -2.5), (4, -1), (34, -1.2), (36, 0), (34, 1.2), (4, 1), (2.5, 2.5), (-2.5, 2.5)])
    # ---------------- the spools' spokes, turning as the roll plays
    for name, r in (("roll_spokes", 13.5), ("take_up_spokes", 10.5)):
        node(name, [(-r, -0.6), (-2.8, -0.6), (-2.8, -2.8), (-0.6, -2.8), (-0.6, -r), (0.6, -r), (0.6, -2.8), (2.8, -2.8),
                    (2.8, -0.6), (r, -0.6), (r, 0.6), (2.8, 0.6), (2.8, 2.8), (0.6, 2.8), (0.6, r), (-0.6, r), (-0.6, 2.8),
                    (-2.8, 2.8), (-2.8, 0.6), (-r, 0.6)])
    # ---------------- the hammer: butt, shank and molding about its centre
    # pin H, authored at the pose where its felt meets the string, the
    # shank upright; the felt is a reshaping outline (display.cpp)
    tip = (SX + 2.0 - H[0], STRIKE - H[1])                   # (-32, -76)
    ANCHORS["hammer_tip_x"], ANCHORS["hammer_tip_y"] = tip
    face = tip[0] + 12.0                                     # the molding's front face
    ANCHORS["hammer_face_x"] = face
    hy = tip[1]
    node("hammer_butt", [(-8, -16), (7, -16), (11.5, -11), (12.5, -2), (12, 5), (8.5, 9), (-5.5, 9), (-9, 4.5), (-9.5, -10)])
    node("hammer_butt_felt", rect(5, 9, 13, 11.5, 0.8, 1))
    node("hammer_catcher", [(9, -9), (20, -19), (24, -19), (26, -16), (25, -12), (21, -11), (12, -5)])
    node("hammer_pin", circle=(0, 0, 2.2))
    node("hammer_shank", [(-12.8, hy + 10), (-10.2, hy + 10), (1.3, -15.8), (-1.3, -16)])
    node("hammer_molding", [(face, hy - 11), (face + 7, hy - 10), (face + 12, hy - 6.5), (face + 13.5, hy - 1.8),
                            (face + 13.5, hy + 1.8), (face + 12, hy + 6.5), (face + 7, hy + 10), (face, hy + 11)])
    node("hammer_tack", [(tip[0] - 1.0, hy - 2.0), (tip[0] + 5, hy - 1.2), (tip[0] + 5, hy + 1.2), (tip[0] - 1.0, hy + 2.0)])
    # ---------------- the hammer rail in section, felt-faced (SOFT moves it in)
    node("hammer_rail", rect(0, -6, 10, 6, 2.5, 2))
    node("hammer_rail_felt", line(-0.2, -4.5, -0.2, 4.5, 1.0, 1))
    # ---------------- the damper: lever about its pin D, its wire out over the
    # hammer to the head that holds the felt (a reshaping outline)
    wire = DAMP - D[1]                                       # the wire's height
    head_x = SX + 7.0 - D[0]                                 # the head's face, its felt 5 px
    ANCHORS["damper_face_x"] = head_x
    ANCHORS["damper_felt_y"] = wire
    node("damper_lever", [(-1.6, 41), (1.6, 41), (1.6, wire - 1.6), (-1.6, wire - 1.6)])
    node("damper_wire", line(1.0, wire, head_x + 7.5, wire, 1.1, 1))
    node("damper_head", rect(head_x, wire - 10, head_x + 7, wire + 10, 1.5, 2))
    node("damper_pin", circle=(0, 0, 2.0))
    # ---------------- the wippen about its pin W: its body, the jack standing
    # on it under the hammer butt, the damper's spoon
    jx, cx = JACK_X - W[0], CAPSTAN_X - W[0]
    node("wippen", [(jx - 5, -3), (jx + 10, -4.5), (cx - 8, -5.5), (-8, -4.5), (4, -3), (5.5, 0), (4, 3), (-10, 3.5),
                    (cx - 6, 4), (jx + 10, 3.5), (jx - 5, 3)])
    jt = H[1] + 11.5 - W[1]
    node("wippen_jack", [(jx - 2, -4.5), (jx - 2, jt + 0.5), (jx + 2.5, jt), (jx + 3, jt + 6), (jx + 12, jt + 4.5), (jx + 13, jt + 7),
                         (jx + 3, -4.5)])
    sp = D[0] - W[0]
    node("wippen_spoon", ribbon([(sp + 10, -4.5), (sp + 9, -10), (sp + 6, -16), (sp + 3, D[1] + 41 - W[1] + 1)], 1.6))
    node("wippen_pin", circle=(0, 0, 2.0))
    # ---------------- the key about its balance pin B: the key, its ivory at
    # the front, the capstan on its tail lifting the wippen
    kt, kb = KEY_TOP - B[1], KEY_BOTTOM - B[1]
    node("key", [(KEY_BACK - B[0], kt), (KEY_FRONT - 52 - B[0], kt), (KEY_FRONT - 52 - B[0], kt - 2.5), (KEY_FRONT - B[0], kt - 2.5),
                 (KEY_FRONT - B[0], kb), (KEY_BACK - B[0], kb)])
    node("key_front", line(KEY_FRONT - 50 - B[0], kt - 1.2, KEY_FRONT - 2 - B[0], kt - 1.2, 0.5, 1))
    node("key_capstan", rect(cx + W[0] - B[0] - 4, W[1] + 4.5 - B[1], cx + W[0] - B[0] + 4, kt, 1.2, 1))
    node("key_pin", circle=(0, -4, 1.6))
    # the lead weights let into the key's side, as an old key's are
    node("key_lead_0", circle=(KEY_FRONT - 80 - B[0], (kt + kb) / 2, 2.8))
    node("key_lead_1", circle=(KEY_FRONT - 104 - B[0], (kt + kb) / 2, 2.8))
    # ---------------- the preparations, centred on the string at their spot
    # RUBBER: a rubber wedge between the unisons
    node("rubber", [(-8, -9), (8, -9), (5.5, 9), (-5.5, 9)])
    node("rubber_lip", line(-9.5, -9.6, 9.5, -9.6, 1.4, 1))
    # PAPER: a paper strip woven through the strings (its width scales)
    node("paper", [(-18, -4.5), (-8, -3), (0, -5.5), (8, -3), (18, -4.5), (18, 4.5), (8, 6), (0, 3.5), (-8, 6), (-18, 4.5)])
    # a felt mute strip (TACK's MUTE, PLUCK's MUTE, RATTLE's RING)
    node("mute", rect(-9, -5, 9, 5, 2.5, 2))
    node("mute_line", line(-5.5, 0, 5.5, 0, 0.6, 1))
    # BELL: a wood screw through the strings, its head to the right
    node("screw_shank", [(-17, -2.2), (7, -3), (7, 3), (-17, 2.2), (-21, 0)])
    node("screw_head", [(7, -7), (12, -5.5), (13.5, 0), (12, 5.5), (7, 7)])
    node("screw_thread", ribbon([(-14, -3.6), (-10.5, 3.6), (-7, -3.6), (-3.5, 3.6), (0, -3.6), (3.5, 3.6)], 0.5))
    # BOLT: a bolt through the strings and its hex nut
    node("bolt_shank", rect(-20, -3.2, 12, 3.2, 0.8, 1))
    node("bolt_head", rect(-28, -8, -20, 8, 1.2, 1))
    node("bolt_nut", [(-4, -8.5), (4, -8.5), (6, -4.2), (6, 4.2), (4, 8.5), (-4, 8.5), (-6, 4.2), (-6, -4.2)])
    node("bolt_nut_face", line(0, -8.2, 0, 8.2, 0.6, 1))
    # CHAIN: a chain of glass beads draped over the strings, jingles among them
    for k in range(10):
        node(f"bead_{k}", circle=(0, 0, 3.0))
    for k in range(4):
        node(f"jingle_{k}", ellipse(0, 0, 4.2, 1.6, 14))
    # BRUSH: a stiff brush (handle, ferrule, its bristle block) and its
    # bristles, pivot at the bristles' tips on the string; it reaches in from
    # the plate's side, so the action stays clear
    node("brush_handle", mirror([(14, -3), (30, -4), (52, -3.4), (55, 0), (52, 3.4), (30, 4), (14, 3)]))
    node("brush_ferrule", mirror(rect(9, -5.5, 15, 5.5, 1, 1)))
    node("brush_block", mirror(rect(3, -7, 9, 7, 1, 1)))
    for k in range(4):
        node(f"brush_bristle_{k}", mirror(line(-0.5, -5.25 + 3.5 * k, 3.2, -5.25 + 3.5 * k, 0.7, 1)))
    for k in range(3):
        node(f"string_screw_{k}", [(-2.5, -2.5), (2.5, -2.5), (2.5, 2.5), (-2.5, 2.5)])
    # PLUCK: a quill plectrum from the upper left to its tip on the string
    node("quill", mirror([(4, -1.4), (20, -6.5), (40, -13), (56, -18.5), (57, -16), (40, -10.5), (20, -4), (4, 1.4)]))
    node("quill_vane", mirror([(30, -11), (38, -17), (48, -22), (56, -23), (58, -19.5), (48, -15.5), (40, -12)]))
    node("quill_tip", mirror([(0, -0.9), (4.5, -1.6), (4.5, 1.6), (0, 0.9)]))
    node("quill_nail", mirror([(-1, 0), (2, -2.4), (4.5, -2.4), (4.5, 2.4), (2, 2.4)]))
    # BOW: the thread's wooden toggle (the thread itself is a reshaping outline)
    node("toggle", rect(-2.5, -9, 2.5, 9, 2, 2))
    node("toggle_grain", line(0, -6, 0, 6, 0.6, 1))
    # LID: the fallboard about its hinge, authored closed: lying over the
    # keys toward the player, along +x
    node("fallboard", [(-2, -3), (2, -4.5), (52, -4.5), (55, -2), (55, 2.5), (52, 4.5), (2, 4.5), (-2, 3)])
    node("fallboard_lip", line(8, 0, 48, 0, 0.6, 1))
    node("fallboard_edge", rect(51, -5.5, 56, 5.5, 1.2, 1))
    node("landing_felt", rect(-4, -1.5, 4, 1.5, 0.6, 1))
    # the key slip before the keys' fronts (LID's HOLLOW draws it away)
    node("key_slip", [(1, -12), (7, -12), (7, 16), (1, 16)])
    # PLANK: Ives's plank laid across the keys' fronts
    node("plank", rect(-26, -9, 26, 0, 1.5, 2))
    node("plank_grain", ribbon([(-21, -4.5), (-8, -5.5), (6, -3.8), (20, -4.6)], 0.6))


def build():
    frame()
    parts()
    n_header = 8
    first_header = PATHS
    bound = PATHS + n_header + len(NODES)
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
    for d, col, w, fill in STATIC:
        if d[0] == "circle":
            _, cx, cy, r = d
            lines.append(f'<circle cx="{fmt(cx)}" cy="{fmt(cy)}" r="{fmt(r)}" fill="{fill}" stroke="{col}" stroke-width="{fmt(w)}"/>')
        else:
            lines.append(f'<path d="{d}" fill="{fill}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    # the reshaping outlines, unfilled, authored as distinct points round an ellipse
    for p in range(PATHS):
        pts = [(20 * math.cos(2 * math.pi * i / POINTS), 12 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
        lines.append(f'<path id="{ID}_node_{p}" transform="matrix(1 0 0 1 640 320)" d="{poly(pts)}" fill="none" stroke="{INK}" stroke-width="0.5"/>')
    names = []
    for k, (name, points, circle, fill) in enumerate(NODES):
        idx = PATHS + n_header + k
        names.append((name, idx))
        if circle:
            cx, cy, r = circle
            lines.append(f'<circle id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" cx="{fmt(cx)}" cy="{fmt(cy)}" '
                         f'r="{fmt(r)}" fill="{fill}" stroke="{MID}" stroke-width="1"/>')
        else:
            lines.append(f'<path id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                         f'fill="{fill}" stroke="{MID}" stroke-width="1"/>')
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    (ROOT / "panel.svg.fragment").write_text(text)
    assert len(text.encode()) <= 64 * 1024, len(text.encode())
    q = lambda v: str(round(v * 1024))
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers and anchors (Q10 px).",
            "#include <opxy/types.hpp>", "", "namespace felt {", "namespace art {",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            f"const opxy::i32 string_x = {q(SX)}, string_top = {q(STOP)}, strike_y = {q(STRIKE)}, damp_y = {q(DAMP)};",
            f"const opxy::i32 bridge_hi = {q(BRIDGE_HI)}, bridge_lo = {q(BRIDGE_LO)};",
            f"const opxy::i32 hammer_x = {q(H[0])}, hammer_y = {q(H[1])}, damper_x = {q(D[0])}, damper_y = {q(D[1])};",
            f"const opxy::i32 wippen_x = {q(W[0])}, wippen_y = {q(W[1])}, key_x = {q(B[0])}, key_y = {q(B[1])};",
            f"const opxy::i32 key_top = {q(KEY_TOP)}, key_front_x = {q(KEY_FRONT)}, capstan_x = {q(CAPSTAN_X)}, jack_x = {q(JACK_X)};",
            f"const opxy::i32 rail_y = {q(RAIL_Y)}, hinge_x = {q(HINGE[0])}, hinge_y = {q(HINGE[1])};",
            f"const opxy::i32 tune_pin_x = {q(TUNE_PIN[0])}, tune_pin_y = {q(TUNE_PIN[1])};",
            f"const opxy::i32 roll_x = {q(ROLL[0])}, roll_y = {q(ROLL[1])}, take_up_x = {q(TAKE_UP[0])}, take_up_y = {q(TAKE_UP[1])};",
            *[f"const opxy::i32 {k} = {q(v)};" for k, v in ANCHORS.items()],
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {idx}," for name, idx in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    # the definition declares the same frame
    import json
    dpath = ROOT / "definition.json"
    definition = json.loads(dpath.read_text())
    definition["resources"]["display"]["nodes"] = bound
    dpath.write_text(json.dumps(definition, indent=2) + "\n")
    print(f"wrote panel.svg.fragment ({len(text.encode())} bytes), art.h and definition.json's frame: {bound} nodes, {len(STATIC)} static paths")


if __name__ == "__main__":
    build()
