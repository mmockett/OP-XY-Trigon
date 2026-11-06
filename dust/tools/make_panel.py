#!/usr/bin/env python3
"""Writes DUST's artwork, panel.svg.fragment, and art.h, the node numbers,
the projection and the geometry display.cpp draws with. Run it after
changing anything here and commit both.

    python3 dust/tools/make_panel.py

The screen is an isometric record deck in the stock look: a near-black
plinth with hairline seams, a dark platter, the record (black with a light
rim and edge), the tonearm on its pillar with its counterweight, and a dust
brush on a post at the front left. One isometric projection for everything:
30 degree edges, no vanishing point. World units are screen pixels: X runs
down to the right, Y down to the left, Z up; the plinth's back corner is the
origin.

The grey is the encoder's: the record and its cue sticker are encoder 1
(hollow, a light outline on black), the grooves and the picked bar encoder
2 (4B4B4B), the arm encoder 3 (969696), the counterweight and the dust brush
encoder 4 (CDCDCD). The plinth, platter, pillar, spindle and posts never
move, so they are plain artwork. Everything a control or a hit moves is a
bound node drawn around its own origin, or one of the four reshaping
outlines display.cpp computes in the projection every frame: the groove's
two halves, the picked bar and the arm. No path has more than one subpath
or 64 points, and no outline turns sharper than SHARPEST. Pure Python.
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
RX, RY = 52.0, 56.0                       # the platter's and record's centre
R_PLATTER, PLATTER_H = 46.0, 3.0
Z_REC = H + PLATTER_H                     # the record's underside
REC_H = 3.0
Z_TOP = Z_REC + REC_H                     # the record's playing surface
R_BIG = 44.0                              # a 12-inch at 33; a 7-inch at 45 is 0.6 of it, a 10-inch at 78 0.85
R_REF = 35.0                              # the record and its label are drawn at this size and scaled
LABEL = 0.36                              # the label's radius, of the record's
GROOVE_OUT = 0.93                         # the grooves' outer edge, of the record's
PITCH = 8.0                               # the groove's two walls advance this much a turn: a line every 4 px
PIVOT = (120.0, 22.0)                     # the tonearm's pillar
Z_PIVOT = H + 17.0                        # the arm's top at the pivot
ARM, TAIL = 70.0, 22.0                    # pivot to stylus; the counterweight's tail behind the pivot
ARM_W, ARM_T = 1.2, 0.8                   # the arm's width and depth: drawn as a thin outline stroked wide
STYLUS = 4.4                              # the needle under the headshell
HEAD_D = 1.6                              # the headshell's depth
BRUSH_T = 1.6                             # the brush arm's depth
POST = (12.0, 96.0)                       # the dust brush's post
BRUSH_R = 21.0                            # the brush's head over the record, from its centre
BRUSH_DROP = 7.0                          # how far the brush is parked above the record
HEAD_T = 2.5                              # the brush head's depth, bristles and all
Z_BRUSH = Z_TOP + BRUSH_DROP + HEAD_T     # the brush arm's top, parked


def record_radius(r):
    """SPEED (0..127) to the record's radius, as display.cpp computes it (Q4 integers)."""
    if r <= 64:
        return 704 - ((r * 4506) >> 10)
    return 422 + (((r - 64) * 2861) >> 10)


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


STYLUS_LO, STYLUS_N = 8, 37   # the stylus table: radii 8, 9, .. 44


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
    assert abs(area / 2) >= 1.0, f"area {area / 2:.2f}"


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


# ---------------------------------------------------------------- the static scene
LAYERS = {"back": [], "middle": [], "pivot": [], "spindle": [], "post": []}


def static(layer, points, fill="none", stroke="none", w=0.5):
    LAYERS[layer].append((poly(points), fill, stroke, w))


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
    # the tonearm's pillar: a 4B4B4B base under a 7D7D7D column
    px, py = PIVOT
    static("middle", cylinder_hull(px, py, H, 3.0, 7.0), G2)
    static("middle", ellipse(px, py, H + 3.0, 7.0, 40), SEAM)
    static("middle", cylinder_hull(px, py, H + 3.0, Z_PIVOT - H - 3.0, 3.0, 24), "#7D7D7D", "#000000", 0.4)
    static("middle", ellipse(px, py, Z_PIVOT, 3.0, 24), G3, "#000000", 0.4)
    # the bearing's cap over the arm
    static("pivot", cylinder_hull(px, py, Z_PIVOT - 1.0, 2.4, 2.2, 16), G3, "#000000", 0.5)
    static("pivot", ellipse(px, py, Z_PIVOT + 1.4, 2.2, 20), G3, "#000000", 0.5)
    # the spindle through the label
    static("spindle", cylinder_hull(RX, RY, Z_TOP, 3.0, 1.4, 12), G3)
    static("spindle", ellipse(RX, RY, Z_TOP + 3.0, 1.4, 16), G4)
    # the dust brush's post: the brush slides down it
    bx, by = POST
    static("post", cylinder_hull(bx, by, H, Z_BRUSH - BRUSH_T - H, 2.4, 20), G2)
    static("post", ellipse(bx, by, Z_BRUSH - BRUSH_T, 2.4, 20), SEAM)


# ---------------------------------------------------------------- bound parts
NODES = []   # (name, points, fill, stroke, width)


def node(name, points, fill="none", stroke=G4, w=1.0):
    NODES.append((name, points, fill, stroke, w))


def brush_frame():
    """The brush's arm and head in plan, and its origin on screen (the arm's root, parked)."""
    bx, by = POST
    ux, uy = RX - bx, RY - by
    L = math.hypot(ux, uy)
    ux, uy = ux / L, uy / L
    hx, hy = RX - ux * BRUSH_R, RY - uy * BRUSH_R          # the head's centre
    return (bx, by), (hx, hy), (ux, uy)


def parts():
    o = P(RX, RY, Z_TOP)
    # the record at R_REF: its edge (black, a light rim) and its top; its label (all scaled by SPEED)
    rec = [(x - o[0], y - o[1]) for x, y in cylinder_hull(RX, RY, Z_TOP - REC_H, REC_H, R_REF)]
    node("record_edge", rec, "#000000", G4, 1.0)
    node("record_top", [(x - o[0], y - o[1]) for x, y in ellipse(RX, RY, Z_TOP, R_REF, 64)], "#000000", G4, 1.0)
    node("label", [(x - o[0], y - o[1]) for x, y in ellipse(RX, RY, Z_TOP, R_REF * LABEL, 48)], "#000000", G4, 1.0)
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
    # DRAG's dust brush: its head (bristles and all) under the arm and the head's top, CDCDCD,
    # drawn about the arm's root at the post, parked
    (bx, by), (hx, hy), (ux, uy) = brush_frame()
    origin = P(bx, by, Z_BRUSH)
    head = bar((hx - ux * 1.75, hy - uy * 1.75), (hx + ux * 1.75, hy + uy * 1.75), 10.0)
    node("brush_side", prism(head, Z_BRUSH, HEAD_T, origin), "#AFAFAF", "#000000", 0.5)
    node("brush_arm", prism(bar((bx, by), (hx - ux * 1.75, hy - uy * 1.75), 2.0), Z_BRUSH, BRUSH_T, origin), G4, "#000000", 0.5)
    node("brush_top", plate(head, Z_BRUSH, origin), G4, "#000000", 0.5)


# the drawing order, back to front
ORDER = ["back", "record_edge", "record_top", "path:0", "path:1", "path:2", "label", "spindle", "sticker",
         "middle", "path:3", "weight_side", "weight_top", "pivot", "head", "needle",
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
    for item in ORDER:
        if item in LAYERS:
            for d, fill, stroke, w in LAYERS[item]:
                lines.append(f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
        elif item.startswith("path:"):
            p = int(item[5:])
            # the reshaping outlines, unfilled, authored as distinct points round an ellipse
            pts = [(30 * math.cos(2 * math.pi * i / POINTS), 20 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            col, w = PALETTE[PATH_SHADE[p]]
            lines.append(f'<path id="{ID}_node_{p}" d="{poly(pts)}" fill="none" stroke="{col}" stroke-width="{fmt(w)}"/>')
        else:
            _n, points, fill, stroke, w = by_name[item]
            emitted.add(item)
            lines.append(f'<path id="{ID}_node_{index[item]}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                         f'fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
    assert emitted == set(by_name), set(by_name) - emitted
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text) < 64 * 1024, len(text)
    (ROOT / "panel.svg.fragment").write_text(text)

    # art.h: the projection and the geometry in Q4 (16 a pixel), angles 65536 a turn
    names = sorted(index.items(), key=lambda kv: kv[1])
    tab = [stylus(STYLUS_LO + i) for i in range(STYLUS_N)]
    phis = [math.atan2(sy - RY, sx - RX) + math.pi / 4 for sx, sy in tab]
    turns = [int(round(a / (2 * math.pi) * 65536)) for a in phis]
    assert all(abs(t - turns[0]) < 16384 for t in turns), turns
    (bx, by), (hx, hy), _u = brush_frame()
    ref = screen_angle(*U_REF)
    nz = noise()
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers, the projection and the deck's geometry.",
            "#include <opxy/types.hpp>", "", "namespace dust {", "namespace art {",
            "using opxy::i32;",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            "// screen (Q10 px) = (ox + (x - y) cos 30, oy + (x + y) / 2 - z) for world x y z in Q4 (16 a px)",
            f"const i32 ox = {int(round(OX * 1024))}, oy = {int(round(OY * 1024))};",
            "// the deck, Q4",
            f"const i32 rx = {q4(RX)}, ry = {q4(RY)}, z_top = {q4(Z_TOP)};",
            f"const i32 r_ref = {q4(R_REF)};   // the record node's radius as drawn",
            f"const i32 pivot_x = {q4(PIVOT[0])}, pivot_y = {q4(PIVOT[1])}, pivot_z = {q4(Z_PIVOT)};",
            f"const i32 arm = {q4(ARM)}, tail = {q4(TAIL)}, arm_w = {q4(ARM_W)}, arm_t = {q4(ARM_T)}, head_d = {q4(HEAD_D)}, stylus = {q4(STYLUS)};",
            f"const i32 post_x = {q4(bx)}, post_y = {q4(by)}, brush_z = {q4(Z_BRUSH)}, brush_drop = {q4(BRUSH_DROP)};",
            "// the stylus over the record at radii 8, 9, .. 44 px: its plan position (Q4) and its angle",
            "// about the spindle on screen (65536 a turn, 0 screen right, clockwise)",
            f"const i32 stylus_lo = {q4(STYLUS_LO)}, stylus_n = {STYLUS_N};",
            "const i32 stylus_x[" + str(STYLUS_N) + "] = {" + ", ".join(str(q4(x)) for x, _y in tab) + "};",
            "const i32 stylus_y[" + str(STYLUS_N) + "] = {" + ", ".join(str(q4(y)) for _x, y in tab) + "};",
            "const i32 stylus_phi[" + str(STYLUS_N) + "] = {" + ", ".join(str(t) for t in turns) + "};",
            "// the headshell's direction on screen as drawn, Q10",
            f"const i32 head_cos = {int(round(math.cos(ref) * 1024))}, head_sin = {int(round(math.sin(ref) * 1024))};",
            "// WEAR's fixed wander of each groove point: [half][wall][point], -127..127",
            "const signed char wander[2][2][32] = {" + ", ".join(
                "{" + ", ".join("{" + ", ".join(str(v) for v in wall) + "}" for wall in half) + "}" for half in nz) + "};",
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {idx}," for name, idx in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    n_static = sum(len(v) for v in LAYERS.values())
    print(f"wrote panel.svg.fragment ({len(text)} bytes) and art.h: {bound} nodes, {n_static} static paths")


if __name__ == "__main__":
    build()
