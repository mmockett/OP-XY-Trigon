#!/usr/bin/env python3
"""Writes DUST's artwork, panel.svg.fragment, and art.h, the node numbers,
the projection and the geometry display.cpp draws with. Run it after
changing anything here and commit both.

    python3 dust/tools/make_panel.py

The screen is an isometric record deck in the stock look: a near-black
plinth with hairline seams, a dark platter, one record (black with a light
rim and edge) with the selected model's pictogram printed on its label, the
tonearm on its pillar with its counterweight, a dust brush on a post at the
left, a speed dial at the front left and a pitch fader at the front right.
One isometric projection for everything: 30 degree edges, no vanishing
point. World units are screen pixels: X runs down to the right, Y down to
the left, Z up; the plinth's back corner is the origin.

The grey is the encoder's: the record, its cue sticker, the speed dial and
the pitch fader are encoder 1 (hollow, a light outline on black), the
grooves and the picked bar encoder 2 (4B4B4B), the arm encoder 3 (969696),
the counterweight and the dust brush encoder 4 (CDCDCD). The plinth,
platter, record, pillar, spindle, dial body, fader slot and posts never
move, so they are plain artwork. Everything a control or a hit moves is a
bound node drawn around its own origin, or one of the four reshaping
outlines display.cpp computes in the projection every frame: the groove's
two halves, the picked bar and the arm. The dial's three pointers and the
fifteen pictograms are nodes shown one at a time. Lines are thin closed
ribbons; no path has more than one subpath or 64 points, and no outline
turns sharper than SHARPEST. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "dust"
PATHS, POINTS = 4, 64
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make

# ---------------------------------------------------------------- the view
C30, S30 = math.cos(math.radians(30)), 0.5
OX, OY = 228.0, 74.0      # the world's origin on screen: the plinth's top corner at y = 62, the deck centred


def P(x, y, z=0.0):
    """A world point on screen."""
    return (OX + (x - y) * C30, OY + (x + y) * S30 - z)


# ---------------------------------------------------------------- the deck
W, D, H = 140.0, 112.0, 12.0              # the plinth
RX, RY = 54.0, 52.0                       # the platter's and record's centre
R_PLATTER, PLATTER_H = 46.0, 3.0
Z_REC = H + PLATTER_H                     # the record's underside
REC_H = 3.0
Z_TOP = Z_REC + REC_H                     # the record's playing surface
R_REC = 40.0                              # one record, one size
R_LABEL = 15.0
G_IN, G_OUT = R_LABEL + 0.5, R_REC * 0.93 # the grooves' field
PITCH = 8.0                               # the groove's two walls advance this much a turn: a line every 4 px
PIVOT = (122.0, 20.0)                     # the tonearm's pillar
Z_PIVOT = H + 17.0                        # the arm's top at the pivot
ARM, TAIL = 64.0, 22.0                    # pivot to stylus; the counterweight's tail behind the pivot
ARM_W, ARM_T = 1.2, 0.8                   # the arm's width and depth: drawn as a thin outline stroked wide
STYLUS = 4.4                              # the needle under the headshell
HEAD_D = 1.6                              # the headshell's depth
BRUSH_T = 1.6                             # the brush arm's depth
POST = (6.0, 44.0)                        # the dust brush's post, at the left edge
BRUSH_ANGLE, BRUSH_LEN = -8.0, 30.0       # its arm's plan direction (degrees) and length to the head's centre
BRUSH_DROP = 7.0                          # how far the brush is parked above the record
HEAD_T = 2.5                              # the brush head's depth, bristles and all
Z_BRUSH = Z_TOP + BRUSH_DROP + HEAD_T     # the brush arm's top, parked
DIAL, DIAL_R, DIAL_H = (18.0, 98.0), 7.5, 2.5          # the speed dial, front left
DETENTS = ((33.3333, -175.0), (45.0, -115.0), (78.0, -55.0))   # rpm and the pointer's plan angle
FADER_X, FADER_Y0, FADER_Y1 = 124.0, 62.0, 104.0       # the pitch fader's slot, along the depth axis
CAP_TRAVEL = 15.0                         # the cap's travel either way of the slot's middle
CAP_MID = (FADER_Y0 + FADER_Y1) / 2
PRINT = (RX + 8.4 * math.cos(math.radians(125)), RY + 8.4 * math.sin(math.radians(125)))   # the pictogram, left of the spindle
MARK_SCALE = 0.9                          # the marks are drawn about 11 plan units across: as much as the label holds
PRINT_GREY, TICK_GREY, SCALE_GREY = "#7D7D7D", "#5A5A5A", "#4A4A4A"


def stylus(rho):
    """The stylus over radius rho of the record: the arm's circle about the pivot meets it, in front."""
    px, py = PIVOT
    dx, dy = RX - px, RY - py
    dd = math.hypot(dx, dy)
    aa = (ARM * ARM - rho * rho + dd * dd) / (2 * dd)
    hh = math.sqrt(max(0.0, ARM * ARM - aa * aa))
    mx, my = px + aa * dx / dd, py + aa * dy / dd
    cand = [(mx - hh * dy / dd, my + hh * dx / dd), (mx + hh * dy / dd, my - hh * dx / dd)]
    return max(cand, key=lambda q: q[0] + q[1])


STYLUS_LO, STYLUS_N = 14, 25   # the stylus table: radii 14, 15, .. 38


def screen_angle(x, y):
    """A plan direction's angle on screen (radians, clockwise from screen right)."""
    return math.atan2((x + y) * S30, (x - y) * C30)


def arm_direction(rho):
    sx, sy = stylus(rho)
    return (sx - PIVOT[0]) / ARM, (sy - PIVOT[1]) / ARM


U_REF = arm_direction(26.0)   # the headshell is drawn for this arm; display.cpp turns it to the arm's


def normal(u):
    """The arm's half-width direction toward the viewer."""
    nx, ny = -u[1], u[0]
    return (nx, ny) if nx + ny > 0 else (-nx, -ny)


# ---------------------------------------------------------------- greys
G1, G2, G3, G4 = "#191919", "#4B4B4B", "#969696", "#CDCDCD"   # encoders 1..4
SEAM = "#646464"
PALETTE = [("#000000", 1.0),      # 0
           (G2, 0.75),            # 1: the grooves
           (G2, 2.5),             # 2: the picked bar, a solid band
           (G3, 1.75)]            # 3: the arm, a solid bar
PALETTE += [(g, 1.0) for g in ["#191919", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696",
                                "#AFAFAF", "#CDCDCD", "#E6E6E6", "#FFFFFF", "#000000", "#000000"]]
PATH_SHADE = [1, 1, 2, 3]        # the palette entry each reshaping outline is stroked with


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(points):
    """The device's rules for an outline: distinct points 1/8 px apart, a closing edge of at
    least 1/4 px, area, and no turn sharper than SHARPEST degrees (the OP-XY strokes with mitred
    joins and no mitre limit: a turn near straight back draws a long spike)."""
    n = len(points)
    assert 3 <= n <= 64, n
    area = 0.0
    for i in range(n):
        (ax, ay), (bx, by), (cx, cy) = points[i - 1], points[i], points[(i + 1) % n]
        ux, uy, vx, vy = bx - ax, by - ay, cx - bx, cy - by
        assert abs(vx) >= 0.125 or abs(vy) >= 0.125, f"points {i} and {i + 1} merge"
        turn = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        assert turn <= SHARPEST, f"turn of {turn:.1f} degrees at point {i}"
        area += ax * by - bx * ay
    (ax, ay), (bx, by) = points[-1], points[0]
    assert math.hypot(bx - ax, by - ay) >= 0.25, "closing edge"
    assert abs(area / 2) >= 0.5, f"area {area / 2:.2f}"   # a ribbon a fraction of a pixel wide has area enough


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


def ellipse(cx, cy, z, r, n, a0=0.0, a1=2 * math.pi, closed=True):
    """An isometric circle about plan point (cx, cy) at height z, by its screen angle: 0 screen right."""
    m = n if closed else n - 1
    ox, oy = P(cx, cy, z)
    return [(ox + r * math.sqrt(2) * C30 * math.cos(a0 + (a1 - a0) * i / m),
             oy + r * math.sqrt(2) * S30 * math.sin(a0 + (a1 - a0) * i / m)) for i in range(n)]


def cylinder_hull(cx, cy, z0, h, r, n=32):
    """An upright cylinder's silhouette: the top's back half, the sides, the bottom's front half."""
    return ellipse(cx, cy, z0 + h, r, n, math.pi, 2 * math.pi, False) + ellipse(cx, cy, z0, r, n, 0.0, math.pi, False)


def hull(points):
    """The convex hull of screen points, clockwise on screen (Andrew's monotone chain)."""
    pts = sorted(set((round(x, 4), round(y, 4)) for x, y in points))
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 1e-9:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 1e-9:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def prism(corners, z_top, depth, origin):
    """The silhouette of a flat plate (plan corners) from z_top down by depth, about a screen origin."""
    pts = [P(x, y, z) for x, y in corners for z in (z_top, z_top - depth)]
    return [(x - origin[0], y - origin[1]) for x, y in hull(pts)]


def plate(corners, z, origin):
    return [(x - origin[0], y - origin[1]) for x, y in (P(cx, cy, z) for cx, cy in corners)]


def bar(a, b, w):
    """A plan rectangle from a to b, w wide."""
    (ax, ay), (bx, by) = a, b
    L = math.hypot(bx - ax, by - ay)
    nx, ny = -(by - ay) / L * w / 2, (bx - ax) / L * w / 2
    return [(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)]


def ribbon(points, w=0.6):
    """A drawn line as a thin closed ribbon (plan points), w wide: out along one edge, back along the other."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        left.append((points[i][0] - dy / length * w / 2, points[i][1] + dx / length * w / 2))
        right.append((points[i][0] + dy / length * w / 2, points[i][1] - dx / length * w / 2))
    loop = left + right[::-1]
    k = max(1, n // 2)
    return loop[k:] + loop[:k]   # start mid-edge, so the closing edge is a long one


def flat(points, z, origin=(0.0, 0.0)):
    """Plan points at height z on screen, about a screen origin."""
    return [(x - origin[0], y - origin[1]) for x, y in (P(px, py, z) for px, py in points)]


# ---------------------------------------------------------------- the static scene
LAYERS = {name: [] for name in ("back", "record", "label", "spindle", "middle", "pivot", "dial", "fader", "post")}


def static(layer, points, fill="none", stroke="none", w=0.5):
    LAYERS[layer].append((poly(points), fill, stroke, w))


def brush_frame():
    """The brush's post, its head's centre and the arm's plan direction."""
    bx, by = POST
    a = math.radians(BRUSH_ANGLE)
    ux, uy = math.cos(a), math.sin(a)
    return (bx, by), (bx + ux * BRUSH_LEN, by + uy * BRUSH_LEN), (ux, uy)


def scene():
    # the plinth: top and left faces 191919, the right face black, 0.5 px seams
    top = [P(0, 0, H), P(W, 0, H), P(W, D, H), P(0, D, H)]
    left = [P(0, D, H), P(W, D, H), P(W, D, 0), P(0, D, 0)]
    right = [P(W, 0, H), P(W, D, H), P(W, D, 0), P(W, 0, 0)]
    static("back", left, G1, SEAM)
    static("back", right, "#000000", SEAM)
    static("back", top, G1, SEAM)
    # the platter, one dark solid
    static("back", cylinder_hull(RX, RY, H, PLATTER_H, R_PLATTER), "#323232")
    # the record, encoder 1's hollow: black with a light rim and edge
    static("record", cylinder_hull(RX, RY, Z_REC, REC_H, R_REC), "#000000", G4, 1.0)
    static("record", ellipse(RX, RY, Z_TOP, R_REC, 64), "#000000", G4, 1.0)
    static("label", ellipse(RX, RY, Z_TOP, R_LABEL, 48), "#000000", G4, 1.0)
    # the spindle through the label
    static("spindle", cylinder_hull(RX, RY, Z_TOP, 3.0, 1.3, 12), G3)
    static("spindle", ellipse(RX, RY, Z_TOP + 3.0, 1.3, 16), G4)
    # the tonearm's pillar: a 4B4B4B base under a 7D7D7D column
    px, py = PIVOT
    static("middle", cylinder_hull(px, py, H, 3.0, 7.0), G2)
    static("middle", ellipse(px, py, H + 3.0, 7.0, 40), SEAM)
    static("middle", cylinder_hull(px, py, H + 3.0, Z_PIVOT - H - 3.0, 3.0, 24), "#7D7D7D", "#000000", 0.4)
    static("middle", ellipse(px, py, Z_PIVOT, 3.0, 24), G3, "#000000", 0.4)
    # the bearing's cap over the arm
    static("pivot", cylinder_hull(px, py, Z_PIVOT - 1.0, 2.4, 2.2, 16), G3, "#000000", 0.5)
    static("pivot", ellipse(px, py, Z_PIVOT + 1.4, 2.2, 20), G3, "#000000", 0.5)
    # the speed dial: a short hollow cylinder, three printed ticks for its detents
    dx, dy = DIAL
    for _rpm, a in DETENTS:
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        static("dial", flat(ribbon([(dx + 9.5 * c, dy + 9.5 * s), (dx + 13 * c, dy + 13 * s)], 0.7), H), "none", TICK_GREY, 0.8)
    static("dial", cylinder_hull(dx, dy, H, DIAL_H, DIAL_R, 30), "#000000", G4, 0.9)
    static("dial", ellipse(dx, dy, H + DIAL_H, DIAL_R, 40), "#000000", G4, 0.9)
    # the pitch fader: a sunk black slot along the depth axis, seven printed ticks beside it
    x = FADER_X
    static("fader", flat([(x - 1.6, FADER_Y0), (x + 1.6, FADER_Y0), (x + 1.6, FADER_Y1), (x - 1.6, FADER_Y1)], H), "#000000", SEAM, 0.5)
    for k in range(7):
        y = FADER_Y0 + 3 + (FADER_Y1 - FADER_Y0 - 6) * k / 6
        w = 3.5 if k == 3 else 2.0
        static("fader", flat(ribbon([(x + 4, y), (x + 4 + w, y)], 0.6), H), "none", SCALE_GREY, 0.8)
    # the dust brush's post: the brush slides down it
    bx, by = POST
    static("post", cylinder_hull(bx, by, H, Z_BRUSH - BRUSH_T - H, 2.4, 20), G2)
    static("post", ellipse(bx, by, Z_BRUSH - BRUSH_T, 2.4, 20), SEAM)


# ---------------------------------------------------------------- the pictograms
# Each model's mark, printed flat on the label in #7D7D7D: plan units about PRINT,
# u along X, v along Y (the print's up is -v). Each entry is one outline: a closed
# shape, or a drawn line as a thin ribbon.
def circle(cu, cv, r, n=28):
    return ("shape", [(cu + r * math.cos(2 * math.pi * i / n), cv + r * math.sin(2 * math.pi * i / n)) for i in range(n)])


def oval(cu, cv, ru, rv, n=28):
    return ("shape", [(cu + ru * math.cos(2 * math.pi * i / n), cv + rv * math.sin(2 * math.pi * i / n)) for i in range(n)])


def shape(*pts):
    return ("shape", list(pts))


def stroke(*pts):
    return ("line", list(pts))


def capsule(u0, u1, r, n=10):
    pts = [(u1 + r * math.cos(-math.pi / 2 + math.pi * i / n), r * math.sin(-math.pi / 2 + math.pi * i / n)) for i in range(n + 1)]
    pts += [(u0 + r * math.cos(math.pi / 2 + math.pi * i / n), r * math.sin(math.pi / 2 + math.pi * i / n)) for i in range(n + 1)]
    return ("shape", pts)


MODELS = ["kick", "snare", "rim", "hat", "open", "ride", "crash", "tom", "conga", "bongo", "tamb", "bell", "clap", "shake", "bass"]
MARKS = {
    "kick": [circle(0, 0, 5.5), circle(0, 0, 2.6)],                                   # a drum head
    "snare": [shape((-6, -3), (6, -3), (6, 3), (-6, 3)),                             # a drum side on, its wires crossed
              stroke((-5, -1.5), (5, 1.5)), stroke((-5, 1.5), (5, -1.5))],
    "rim": [circle(0, 0.5, 5.5), stroke((1.2, -6.2), (6.4, -1.8))],                   # a drum head, a stick across its edge
    "hat": [shape((-6, -2.2), (6, -2.2), (6, -1.0), (-6, -1.0)),                     # two cymbals closed, on the stand
            stroke((0, -1.0), (0, 5))],
    "open": [stroke((-6, -4.4), (6, -4.4)), stroke((-6, -1.0), (6, -1.0)),            # held apart, on the stand
             stroke((0, -4.4), (0, 5))],
    "ride": [stroke((-6.5, 0.5), (6.5, 0.5)), circle(0, -1.1, 1.1, 14)],              # one long cymbal, the bell above
    "crash": [stroke((5, 2.5), (-4, -1.5)),                                           # tilted, spray off its high end
              stroke((-4.7, -2.4), (-5.9, -4.6)), stroke((-5.2, -1.6), (-7.2, -2.6)), stroke((-5.0, -0.6), (-7.0, -0.2))],
    "tom": [shape((-5, -4), (5, -4), (5, 4), (-5, 4))],                               # a deeper drum side on
    "conga": [shape((-3.2, -6), (3.2, -6), (1.8, 6), (-1.8, 6))],                     # a tall barrel, narrow at the foot
    "bongo": [circle(-3, 0, 3, 22), circle(3, 0, 3, 22)],                             # two small heads touching
    "tamb": [circle(0, 0, 5)] + [circle(5 * math.cos(a), 5 * math.sin(a), 1.0, 12)   # a ring with four jingles
                                 for a in (math.pi / 4, 3 * math.pi / 4, 5 * math.pi / 4, 7 * math.pi / 4)],
    "bell": [shape((-2.5, -4), (2.5, -4), (5, 4), (-5, 4)), stroke((0, 1.5), (0, 6))],   # wide at the mouth, a clapper
    "clap": [oval(-1.8, 0, 3, 5), oval(1.8, 0, 3, 5)],                               # two palms overlapping
    "shake": [capsule(-3.5, 3.5, 2.6)] + [circle(u, 0, 0.8, 10) for u in (-3, 0, 3)],   # a capsule, three beads
    "bass": [stroke(*[(u, 1.7 * math.sin(u * 0.785)) for u in [-6 + 12 * i / 24 for i in range(25)]])],   # a string, a wave and a half
}


def mark_outline(kind, pts):
    """A mark's outline in plan, about PRINT."""
    pts = [(u * MARK_SCALE, v * MARK_SCALE) for u, v in pts]
    return pts if kind == "shape" else ribbon(pts, 0.6)


# ---------------------------------------------------------------- bound parts
NODES = []   # (name, points, fill, stroke, width)


def node(name, points, fill="none", stroke_=G4, w=1.0):
    NODES.append((name, points, fill, stroke_, w))


CAP = []     # the fader cap's nodes, drawn about its middle


def parts():
    o = P(RX, RY, Z_TOP)
    # the model's pictogram on the label: every model's, one shown at a time
    po = P(PRINT[0], PRINT[1], Z_TOP)
    for m in MODELS:
        for k, (kind, pts) in enumerate(MARKS[m]):
            plan = [(PRINT[0] + u, PRINT[1] + v) for u, v in mark_outline(kind, pts)]
            for x, y in plan:   # on the label, clear of the spindle
                d = math.hypot(x - RX, y - RY)
                assert 2.3 <= d <= R_LABEL - 0.9, (m, k, round(d, 2))
            node(f"mark_{m}_{k}", flat(plan, Z_TOP, po), "none", PRINT_GREY, 0.8)
    # CUE's sticker on the rim, hollow
    node("sticker", [(x - o[0], y - o[1]) for x, y in ellipse(RX, RY, Z_TOP, 3.2, 24)], "#000000", G4, 0.9)
    # WEIGHT's counterweight: a CDCDCD cylinder on the tail, its origin on the tail's top
    p = P(RX, RY, 0)
    node("weight_side", [(x - p[0], y - p[1]) for x, y in cylinder_hull(RX, RY, -3.5, 6.5, 3.6, 24)], "#AFAFAF", "#000000", 0.5)
    node("weight_top", [(x - p[0], y - p[1]) for x, y in ellipse(RX, RY, 3.0, 3.6, 24)], G4, "#000000", 0.5)
    # the headshell, drawn for U_REF about the needle's top; the needle under it
    u, n = U_REF, normal(U_REF)
    corners = [(a * u[0] + b * n[0], a * u[1] + b * n[1]) for a, b in ((-2, -2.5), (5, -2.5), (5, 2.5), (-2, 2.5))]
    node("head", prism(corners, HEAD_D, HEAD_D, P(0, 0, 0)), G3, "#000000", 0.5)
    node("needle", [(-0.5, 0.0), (0.5, 0.0), (0.5, STYLUS), (-0.5, STYLUS)], G3, G3, 0.3)
    # SPEED's dial pointer, one for each detent, about the dial's top centre
    dx, dy = DIAL
    do = P(dx, dy, H + DIAL_H)
    for rpm, a in DETENTS:
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        node(f"dial_{int(rpm)}", flat(ribbon([(dx, dy), (dx + DIAL_R * 0.85 * c, dy + DIAL_R * 0.85 * s)], 0.9), H + DIAL_H, do),
             "none", G4, 1.1)
    # SPEED's pitch-fader cap: a raised hollow box in the slot, a hairline across its top
    co = P(FADER_X, CAP_MID, H)
    x0, x1, y0, y1, zt = FADER_X - 4, FADER_X + 4, CAP_MID - 5, CAP_MID + 5, H + 3.5
    def face(*pts):
        return [(x - co[0], y - co[1]) for x, y in (P(*q) for q in pts)]
    node("cap_left", face((x0, y1, zt), (x1, y1, zt), (x1, y1, H), (x0, y1, H)), "#000000", G4, 0.8)
    node("cap_right", face((x1, y0, zt), (x1, y1, zt), (x1, y1, H), (x1, y0, H)), "#000000", G4, 0.8)
    node("cap_top", flat([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], zt, co), "#000000", G4, 0.8)
    node("cap_line", flat(ribbon([(x0 + 0.6, CAP_MID), (x1 - 0.6, CAP_MID)], 0.4), zt, co), "none", G4, 0.6)
    # DRAG's dust brush: its head (bristles and all) under the arm and the head's top, CDCDCD,
    # drawn about the arm's root at the post, parked
    (bx, by), (hx, hy), (ux, uy) = brush_frame()
    origin = P(bx, by, Z_BRUSH)
    head = bar((hx - ux * 1.75, hy - uy * 1.75), (hx + ux * 1.75, hy + uy * 1.75), 10.0)
    node("brush_side", prism(head, Z_BRUSH, HEAD_T, origin), "#AFAFAF", "#000000", 0.5)
    node("brush_arm", prism(bar((bx, by), (hx - ux * 1.75, hy - uy * 1.75), 2.0), Z_BRUSH, BRUSH_T, origin), G4, "#000000", 0.5)
    node("brush_top", plate(head, Z_BRUSH, origin), G4, "#000000", 0.5)


# the drawing order, back to front
ORDER = ["back", "record", "path:0", "path:1", "path:2", "label", "mark_*", "spindle", "sticker",
         "middle", "path:3", "weight_side", "weight_top", "pivot", "head", "needle",
         "dial", "dial_*", "fader", "cap_left", "cap_right", "cap_top", "cap_line",
         "post", "brush_side", "brush_arm", "brush_top"]


def noise():
    """WEAR's fixed per-point wander: [outline][wall][point], -127..127, the walls sharing most of it."""
    out, s = [], 7
    def rnd():
        nonlocal s
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        return (s >> 8) / (1 << 23)
    for k in range(2):
        a, b = rnd() * 2 * math.pi, rnd() * 2 * math.pi
        smooth = [0.55 * math.sin(2 * math.pi * 3 * i / 32 + a) + 0.3 * math.sin(2 * math.pi * 7 * i / 32 + b) for i in range(32)]
        walls = []
        for w in range(2):
            walls.append([max(-127, min(127, round(127 * (smooth[i] + 0.12 * (rnd() - 0.5) * 2) / 0.97))) for i in range(32)])
        out.append(walls)
    return out


def q4(v):
    return int(round(v * 16))


def q8(v):
    return int(round(v * 256))


def q10(v):
    return int(round(v * 1024))


def build():
    scene()
    parts()
    n_header = 8
    bound = len(NODES) + PATHS + n_header
    lines = [f'<g id="{ID}_panel">', "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    first_header = PATHS
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
    index = {name: PATHS + n_header + k for k, (name, *_rest) in enumerate(NODES)}
    by_name = {n[0]: n for n in NODES}
    emitted = set()

    def emit(name):
        _n, points, fill, stroke_, w = by_name[name]
        emitted.add(name)
        lines.append(f'<path id="{ID}_node_{index[name]}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                     f'fill="{fill}" stroke="{stroke_}" stroke-width="{fmt(w)}"/>')
    for item in ORDER:
        if item in LAYERS:
            for d, fill, stroke_, w in LAYERS[item]:
                lines.append(f'<path d="{d}" fill="{fill}" stroke="{stroke_}" stroke-width="{fmt(w)}"/>')
        elif item.startswith("path:"):
            p = int(item[5:])
            # the reshaping outlines, unfilled, authored as distinct points round an ellipse
            pts = [(30 * math.cos(2 * math.pi * i / POINTS), 20 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            col, w = PALETTE[PATH_SHADE[p]]
            lines.append(f'<path id="{ID}_node_{p}" d="{poly(pts)}" fill="none" stroke="{col}" stroke-width="{fmt(w)}"/>')
        elif item.endswith("*"):
            for name, *_r in NODES:
                if name.startswith(item[:-1]):
                    emit(name)
        else:
            emit(item)
    assert emitted == set(by_name), set(by_name) - emitted
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text) < 64 * 1024, len(text)
    (ROOT / "panel.svg.fragment").write_text(text)

    # art.h: the projection and the geometry; plan in Q4 (16 a pixel), radii in Q8, screen in Q10
    names = sorted(index.items(), key=lambda kv: kv[1])
    tab = [stylus(STYLUS_LO + i) for i in range(STYLUS_N)]
    phis = [math.atan2(sy - RY, sx - RX) + math.pi / 4 for sx, sy in tab]
    turns = [int(round(a / (2 * math.pi) * 65536)) for a in phis]
    assert all(abs(t - turns[0]) < 16384 for t in turns), turns
    (bx, by), _h, _u = brush_frame()
    ref = screen_angle(*U_REF)
    nz = noise()
    centre, mark, dial, cap, brush = P(RX, RY, Z_TOP), P(*PRINT, Z_TOP), P(*DIAL, H + DIAL_H), P(FADER_X, CAP_MID, H), P(bx, by, Z_BRUSH)
    first = [index[f"mark_{m}_0"] for m in MODELS]
    count = [len(MARKS[m]) for m in MODELS]
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers, the projection and the deck's geometry.",
            "#include <opxy/types.hpp>", "", "namespace dust {", "namespace art {",
            "using opxy::i32;",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            "// screen (Q10 px) = (ox + (x - y) cos 30, oy + (x + y) / 2 - z) for world x y z in Q4 (16 a px)",
            f"const i32 ox = {q10(OX)}, oy = {q10(OY)};",
            "// the parts' origins on screen, Q10: the record's centre on its surface, the label's print,",
            "// the dial's top, the fader cap at its middle, the brush arm's root parked",
            f"const i32 centre_x = {q10(centre[0])}, centre_y = {q10(centre[1])};",
            f"const i32 mark_x = {q10(mark[0])}, mark_y = {q10(mark[1])};",
            f"const i32 dial_x = {q10(dial[0])}, dial_y = {q10(dial[1])};",
            f"const i32 cap_x = {q10(cap[0])}, cap_y = {q10(cap[1])}, cap_travel = {q10(CAP_TRAVEL)};",
            f"const i32 brush_x = {q10(brush[0])}, brush_y = {q10(brush[1])}, brush_drop = {q10(BRUSH_DROP)};",
            "// the record and its grooves' field, Q8 radii",
            f"const i32 r_record = {q8(R_REC)}, g_in = {q8(G_IN)}, g_out = {q8(G_OUT)};",
            "// the arm, Q4",
            f"const i32 z_top = {q4(Z_TOP)};",
            f"const i32 pivot_x = {q4(PIVOT[0])}, pivot_y = {q4(PIVOT[1])}, pivot_z = {q4(Z_PIVOT)};",
            f"const i32 arm = {q4(ARM)}, tail = {q4(TAIL)}, arm_w = {q4(ARM_W)}, arm_t = {q4(ARM_T)}, head_d = {q4(HEAD_D)}, stylus = {q4(STYLUS)};",
            f"// the stylus over the record at radii {STYLUS_LO}, {STYLUS_LO + 1}, .. {STYLUS_LO + STYLUS_N - 1} px: its plan position (Q4) and its angle",
            "// about the spindle on screen (65536 a turn, 0 screen right, clockwise)",
            f"const i32 stylus_lo = {q8(STYLUS_LO)}, stylus_n = {STYLUS_N};",
            "const i32 stylus_x[" + str(STYLUS_N) + "] = {" + ", ".join(str(q4(x)) for x, _y in tab) + "};",
            "const i32 stylus_y[" + str(STYLUS_N) + "] = {" + ", ".join(str(q4(y)) for _x, y in tab) + "};",
            "const i32 stylus_phi[" + str(STYLUS_N) + "] = {" + ", ".join(str(t) for t in turns) + "};",
            "// the headshell's direction on screen as drawn, Q10",
            f"const i32 head_cos = {q10(math.cos(ref))}, head_sin = {q10(math.sin(ref))};",
            "// WEAR's fixed wander of each groove point: [half][wall][point], -127..127",
            "const signed char wander[2][2][32] = {" + ", ".join(
                "{" + ", ".join("{" + ", ".join(str(v) for v in wall) + "}" for wall in half) + "}" for half in nz) + "};",
            "// each model's pictogram: its first node and how many",
            f"const opxy::u32 marks_first = {first[0]}, marks_end = {first[-1] + count[-1]};",
            "const opxy::u8 mark_first[15] = {" + ", ".join(str(v) for v in first) + "};",
            "const opxy::u8 mark_count[15] = {" + ", ".join(str(v) for v in count) + "};",
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {idx}," for name, idx in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    n_static = sum(len(v) for v in LAYERS.values())
    print(f"wrote panel.svg.fragment ({len(text)} bytes) and art.h: {bound} nodes ({sum(count)} for the pictograms), {n_static} static paths")


if __name__ == "__main__":
    build()
