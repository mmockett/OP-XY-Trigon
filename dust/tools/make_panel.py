#!/usr/bin/env python3
"""Writes DUST's artwork, panel.svg.fragment, and art.h, the node numbers
and the projection display.cpp draws with. Run it after changing anything
here and commit both.

    python3 dust/tools/make_panel.py

The screen is a portable record player seen from the front and a little to
the side, from above: one orthographic projection for everything, so every
circle in the platter's plane is an ellipse with level axes and the tonearm
is drawn in the same projection. World units are millimetres: X right, Y
back, Z up, the spindle at the origin, the record's surface at Z = 0.

The plinth, the platter, the record's black disc, its grooves and label and
the fittings never move, so they are plain artwork, the plinth and the
fittings filled dark grey. Everything a control or a hit moves is a bound
node drawn around its own pivot (0, 0) or a reshaping outline display.cpp
draws in the projection. Lines are 1 px; thin parts are closed ribbons; no
path has more than one subpath or 64 points. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "dust"
PATHS, POINTS = 4, 64

# ---------------------------------------------------------------- the view
K = 1.25                  # px a millimetre
PHI = math.radians(33)    # the eye's height: the platter's ellipse is sin(33) = 0.54 as tall as wide
PSI = math.radians(14)   # turned so the plinth's right side comes nearer
CX, CY = 186.0, 126.0     # the spindle on screen

AX, AY = K * math.cos(PSI), K * math.sin(PSI)
BX, BY, BZ = -K * math.sin(PSI) * math.sin(PHI), K * math.cos(PSI) * math.sin(PHI), K * math.cos(PHI)


def P(x, y, z=0.0):
    """A world point (mm) on screen (px)."""
    return (CX + AX * x + AY * y, CY - (BX * x + BY * y + BZ * z))


def circle_pts(r, z=0.0, n=64, a0=0.0, a1=2 * math.pi, cx=0.0, cy=0.0, closed=True):
    m = n if closed else n - 1
    return [P(cx + r * math.cos(a0 + (a1 - a0) * i / m), cy + r * math.sin(a0 + (a1 - a0) * i / m), z) for i in range(n)]


# the parts, in millimetres
R_PLATTER, R_RECORD, R_LABEL, R_HOLE = 100.0, 87.5, 45.0, 19.0
Z_PLATTER, Z_PLINTH = -1.5, -14.0
PLINTH = (-190.0, 200.0, -112.0, 175.0)     # x0, x1, y0, y1
PLINTH_H = 46.0
PIVOT = (150.0, 76.0, 9.0)                  # the tonearm's bearing
ARM = 160.0                                 # pivot to stylus
STUB = 58.0                                 # the counterweight's stub behind the pivot
R_OUTER, R_INNER = 83.0, 49.0               # the stylus's grooves: bar 1 and bar 8
BRUSH_PIVOT = (-126.0, -34.0, -6.0)
BRUSH_AT = (-72.0, -12.0)                   # where the brush meets the record
CUE = (180.0, 46.0, Z_PLINTH)
SPEED = (172.0, -48.0, Z_PLINTH)
REST = (176.0, 2.0, Z_PLINTH)               # the arm rest


def stylus(r):
    """Where the stylus is on a groove of radius r: the arm's circle about the pivot meets it."""
    px, py = PIVOT[0], PIVOT[1]
    d = math.hypot(px, py)
    # the angle at the spindle between the pivot and the stylus
    c = (r * r + d * d - ARM * ARM) / (2 * r * d)
    a = math.atan2(py, px) - math.acos(max(-1, min(1, c)))
    return r * math.cos(a), r * math.sin(a), a


# ---------------------------------------------------------------- greys
INK, MID, DIM, FAINT = "#E6E6E6", "#9C9C9C", "#5E5E5E", "#383838"
BODY = "#181818"    # the plinth's top
FIT = "#262626"     # fittings, the platter's edge
FRONT = "#101010"   # the plinth's front face, out of the light
MAT = "#0C0C0C"
LABEL = "#2E2E2E"
LINE = 1.0
RAMP = ["#2E2E2E", "#444444", "#5C5C5C", "#767676", "#909090", "#ACACAC", "#C8C8C8", "#E6E6E6"]
# palette: 0 black; 1..8 the ramp at 1 px; 9..15 a ramp at half width (for parts drawn at scale 2)
# 9..15: a ramp at half width, for the outlines drawn at twice size (1 px as drawn)
HALF = ["#3A3A3A", "#545454", "#6E6E6E", "#8A8A8A", "#A6A6A6", "#C4C4C4", "#E6E6E6"]
PALETTE = [("#000000", 1.0)] + [(g, LINE) for g in RAMP] + [(g, LINE / 2) for g in HALF]


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def poly(points, closed=True):
    out = []
    for p in points:
        q = (round(p[0], 2), round(p[1], 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert 3 <= len(out) <= 64, len(out)
    assert len(set(out)) == len(out), "repeated point"
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in out) + (" Z" if closed else "")


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


def clip(poly_pts, x0=0.0, y0=38.5, x1=480.0, y1=222.0):
    """Sutherland-Hodgman: a polygon clipped to the screen below the header."""
    def edge(pts, inside, cut):
        out = []
        for i in range(len(pts)):
            a, b = pts[i - 1], pts[i]
            if inside(b):
                if not inside(a):
                    out.append(cut(a, b))
                out.append(b)
            elif inside(a):
                out.append(cut(a, b))
        return out

    def at_x(x):
        return lambda a, b: (x, a[1] + (b[1] - a[1]) * (x - a[0]) / (b[0] - a[0]))

    def at_y(y):
        return lambda a, b: (a[0] + (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]), y)
    pts = poly_pts
    pts = edge(pts, lambda p: p[0] >= x0, at_x(x0))
    pts = edge(pts, lambda p: p[0] <= x1, at_x(x1))
    pts = edge(pts, lambda p: p[1] >= y0, at_y(y0))
    pts = edge(pts, lambda p: p[1] <= y1, at_y(y1))
    return pts


def split(points, n=60):
    """A long outline in pieces of at most n points (each its own path)."""
    out, i = [], 0
    while i < len(points) - 1:
        out.append(points[i:i + n])
        i += n - 1
    return out


# ---------------------------------------------------------------- the scene
STATIC = []   # (d, stroke, width, fill): the layer static() and line() add to
LAYERS = {"back": [], "mid": [], "front": []}
TEXTS = []    # (x, y, size, colour, text): static numerals


def layer(name):
    global STATIC
    STATIC = LAYERS[name]


def static(points, col=MID, w=LINE, fill="none"):
    STATIC.append((poly(points), col, w, fill))


def line(points, col=MID, w=0.5):
    """A drawn line: a thin ribbon, 1 px as drawn."""
    for piece in split(points, 30):
        STATIC.append((poly(ribbon(piece, w)), col, LINE * 0.5, "none"))


def ellipse_line(r, z, col, a0=0.0, a1=2 * math.pi, n=60, w=0.5):
    if a1 - a0 >= 2 * math.pi - 1e-9:
        pts = circle_pts(r, z, n)
        STATIC.append((poly(pts), col, LINE, "none"))
    else:
        line(circle_pts(r, z, n, a0, a1, closed=False), col, w)


def scene():
    layer("back")
    x0, x1, y0, y1 = PLINTH
    zt, zb = Z_PLINTH, Z_PLINTH - PLINTH_H
    top = [P(x0, y0, zt), P(x1, y0, zt), P(x1, y1, zt), P(x0, y1, zt)]
    front = [P(x0, y0, zt), P(x1, y0, zt), P(x1, y0, zb), P(x0, y0, zb)]
    left = [P(x0, y0, zt), P(x0, y1, zt), P(x0, y1, zb), P(x0, y0, zb)]
    for face, fill in ((top, BODY), (front, FRONT), (left, FRONT)):
        c = clip(face)
        if len(c) >= 3:
            static(c, fill, 0.5, fill)
    # the plinth's edges that are on screen: the chrome trim along the front edge, the near corner
    line([P(x0 + (x1 - x0) * i / 24, y0, zt) for i in range(25)], "#6A6A6A")
    line([P(x0 + (x1 - x0) * i / 24, y0, zt - 2.5) for i in range(25)], "#2A2A2A")
    line([P(x1, y0 + (y1 - y0) * i / 12, zt) for i in range(13)], "#4A4A4A")
    line([P(x1, y0, zt - 3 * i) for i in range(int(PLINTH_H / 3) + 1)], "#3A3A3A")

    # the platter: its edge (a band with the stroboscope's dots), its rubber mat
    near = [a for a in (math.radians(d) for d in range(0, 361, 6))]
    edge_top = circle_pts(R_PLATTER, Z_PLATTER, 61, math.pi + 0.0, 2 * math.pi, closed=False)
    a0, a1 = math.pi, 2 * math.pi
    # the visible half of the side: angles where the circle faces the eye
    def facing(a):
        return P(math.cos(a) * R_PLATTER, math.sin(a) * R_PLATTER)[1]
    band = circle_pts(R_PLATTER, Z_PLATTER, 31, PSI + math.pi, PSI + 2 * math.pi, closed=False)
    band += circle_pts(R_PLATTER, Z_PLINTH + 1, 31, PSI + 2 * math.pi, PSI + math.pi, closed=False)
    static(band, "#4A4A4A", 0.5, FIT)
    for i in range(28):
        a = PSI + math.pi + math.pi * (i + 0.5) / 28
        p, q = P(math.cos(a) * R_PLATTER, math.sin(a) * R_PLATTER, Z_PLATTER - 4.0), P(math.cos(a) * R_PLATTER, math.sin(a) * R_PLATTER, Z_PLATTER - 7.5)
        if abs(p[0] - q[0]) < 50:
            STATIC.append((poly(ribbon([p, q], 0.9)), "#5A5A5A", 0.5, "none"))
    static(circle_pts(R_PLATTER, Z_PLATTER, 64), "#4A4A4A", LINE, MAT)
    for r in (95.0, 91.0):
        ellipse_line(r, Z_PLATTER, "#1E1E1E")
    # the record: black, its grooves, its sheen, the label and the big hole
    static(circle_pts(R_RECORD, 0, 64), "#000000", 0.5, "#000000")
    for r in (82.0, 75.5, 69.0, 62.5, 56.0, 50.5):
        ellipse_line(r, 0, "#232323")
    # two sheens where the light catches the grooves
    for (a, b) in ((math.radians(118), math.radians(146)), (math.radians(298), math.radians(326))):
        for r in (79.0, 72.5, 66.0, 59.5):
            line(circle_pts(r, 0, 10, a, b, closed=False), "#4A4A4A")
    static(circle_pts(R_LABEL, 0, 64), "#6A6A6A", LINE, LABEL)
    ellipse_line(41.5, 0, "#4E4E4E")
    static(circle_pts(R_HOLE, 0, 48), "#3A3A3A", LINE, MAT)

    # the tonearm's base: a turned pillar on a round plinth plate, the arm rest
    layer("mid")
    px, py, pz = PIVOT
    static(circle_pts(17, Z_PLINTH + 0.5, 40, cx=px, cy=py), "#4A4A4A", LINE, FIT)
    pillar = circle_pts(8, Z_PLINTH, 20, PSI + math.pi, PSI + 2 * math.pi, cx=px, cy=py, closed=False)
    pillar += circle_pts(8, pz - 3, 20, PSI + 2 * math.pi, PSI + math.pi, cx=px, cy=py, closed=False)
    static(pillar, "#5A5A5A", LINE, FIT)
    static(circle_pts(8, pz - 3, 30, cx=px, cy=py), "#6A6A6A", LINE, "#202020")
    rx, ry, rz = REST
    post = [P(rx - 2.5, ry, rz), P(rx + 2.5, ry, rz), P(rx + 2.5, ry, 4), P(rx - 2.5, ry, 4)]
    static(post, "#5A5A5A", LINE, FIT)
    static([P(rx - 6, ry, 4), P(rx + 6, ry, 4), P(rx + 6, ry, 7), P(rx - 6, ry, 7)], "#6A6A6A", LINE, FIT)
    # the cue lever's housing
    cx, cy, cz = CUE
    static(circle_pts(7, cz + 0.5, 24, cx=cx, cy=cy), "#4A4A4A", LINE, FIT)
    # the speed selector's plate, with its three stops marked and numbered
    sx, sy, sz = SPEED
    static(circle_pts(17, sz + 0.5, 40, cx=sx, cy=sy), "#4A4A4A", LINE, FIT)
    hx, hy = P(sx, sy, sz)
    for deg, label, dx, dy in ((-134.8, "33", -16, -1), (-90.0, "45", -5, -8), (-45.9, "78", 6, -1)):
        a = math.radians(deg)
        line([(hx + 20.5 * math.cos(a), hy + 20.5 * math.sin(a)), (hx + 24 * math.cos(a), hy + 24 * math.sin(a))], "#8A8A8A", 0.9)
        TEXTS.append((hx + 24 * math.cos(a) + dx, hy + 24 * math.sin(a) + dy, 9, "#7A7A7A", label))
    # the dust brush's pivot block
    bx, by, bz = BRUSH_PIVOT
    static(circle_pts(8, Z_PLINTH + 0.5, 24, cx=bx, cy=by), "#4A4A4A", LINE, FIT)
    static(circle_pts(4.5, bz, 20, cx=bx, cy=by), "#6A6A6A", LINE, "#202020")
    # the arm's bearing: a yoke over the arm at the pivot, in front of it
    layer("front")
    gx, gy = P(px, py, pz)
    static([(gx - 6, gy - 4.5), (gx + 6, gy - 4.5), (gx + 6.8, gy - 2), (gx + 6.8, gy + 3), (gx - 6.8, gy + 3), (gx - 6.8, gy - 2)], "#7A7A7A", LINE, "#202020")
    static(circle_pts(2.2, pz + 3.5, 16, cx=px, cy=py), "#9A9A9A", LINE, "#2A2A2A")


# ---------------------------------------------------------------- bound parts
NODES = []   # (name, points, circle, fill, colour)


def node(name, points=None, circle=None, fill="none", col=MID):
    NODES.append((name, points, circle, fill, col))


def rect(x0, y0, x1, y1, r=0.0, steps=3):
    if r <= 0:
        return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def parts():
    # the label's printed mark: a short bar, placed on the label as it turns
    node("label_mark", rect(-6, -1.6, 6, 1.6, 1.2, 2), col=INK)
    # WEAR: scratches (short arcs) and dust specks
    for k in range(6):
        L = 7 + (k % 3) * 3
        pts = [(L * (i / 6 - 0.5), 0.6 * math.sin(math.pi * i / 6) * (1 if k % 2 else -1)) for i in range(7)]
        node(f"scratch_{k}", ribbon(pts, 0.7), col=MID)
    for k in range(8):
        node(f"speck_{k}", circle=(0, 0, 1.6 if k % 3 else 2.1), col=INK)
    # the counterweight: a cylinder on its stub, seen from the side; pivot at its centre
    node("weight", rect(-8.5, -6.5, 8.5, 6.5, 2.0, 2), fill="#202020", col=MID)
    node("weight_ring", rect(-3.2, -6.4, 3.2, 6.4, 0, 0), col=DIM)
    # the cue lever: a handle on its pivot (pointing right at rest)
    node("cue_lever", rect(-2, -1.8, 19, 1.8, 1.6, 2), col=MID)
    node("cue_knob", circle=(19, 0, 3.2), fill="#202020", col=MID)
    # the speed selector: a pointer lever on its hub
    node("speed_lever", [(-4, -3.2), (3, -3.6), (17, -1.6), (19, 0), (17, 1.6), (3, 3.6), (-4, 3.2), (-6, 0)], fill="#202020", col=INK)
    node("speed_hub", circle=(0, 0, 3.6), fill="#2A2A2A", col=MID)
    # the dust brush: its arm from the pivot (pointing right), the head at the arm's end
    node("brush_arm", ribbon([(0, 0), (30, -1.5), (60, -2.2)], 2.2), col=MID)
    node("brush_head", rect(-9, -4, 9, 3, 1.5, 2), fill="#202020", col=MID)
    # its bristles: a tuft hanging under the head, bent by the turn the display gives it
    node("brush_tuft", [(-7.5, 0), (7.5, 0), (7.0, 7.0), (4.6, 7.4), (3.2, 6.0), (1.6, 7.6), (0, 6.0), (-1.6, 7.6), (-3.2, 6.0),
                        (-4.6, 7.4), (-7.0, 7.0)], col=DIM)
    # the needle's stylus tip glint, at the headshell's tip
    node("tip", circle=(0, 0, 1.6), fill="#E6E6E6", col=INK)


ORDER = ["back", "path:1", "label_mark", "scratch_*", "speck_*", "path:2", "path:0", "mid",
         "cue_lever", "cue_knob", "speed_lever", "speed_hub", "brush_arm", "brush_head", "brush_tuft",
         "path:3", "weight", "weight_ring", "front", "tip"]


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
    path_cols = [MID, INK, MID, INK]
    emitted = set()

    def emit_node(name):
        _n, points, circle, fill, col = by_name[name]
        idx = index[name]
        emitted.add(name)
        if circle:
            cx, cy, r = circle
            lines.append(f'<circle id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" cx="{fmt(cx)}" cy="{fmt(cy)}" '
                         f'r="{fmt(r)}" fill="{fill}" stroke="{col}" stroke-width="{fmt(LINE)}"/>')
        else:
            lines.append(f'<path id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                         f'fill="{fill}" stroke="{col}" stroke-width="{fmt(LINE)}"/>')
    for item in ORDER:
        if item in LAYERS:
            for d, col, w, fill in LAYERS[item]:
                lines.append(f'<path d="{d}" fill="{fill}" stroke="{col}" stroke-width="{fmt(w)}"/>')
            if item == "mid":
                for x, y, size, col, t in TEXTS:
                    lines.append(f'<text transform="matrix(1 0 0 1 {fmt(x)} {fmt(y)})" fill="{col}" '
                                 f'font-family="UniversNextPro-Light" font-size="{size}px">{t}</text>')
        elif item.startswith("path:"):
            p = int(item[5:])
            # the reshaping outlines, unfilled, authored as distinct points round a circle
            pts = [(30 * math.cos(2 * math.pi * i / POINTS), 20 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            lines.append(f'<path id="{ID}_node_{p}" d="{poly(pts)}" fill="none" stroke="{path_cols[p]}" stroke-width="{fmt(LINE)}"/>')
        elif item.endswith("*"):
            for name, *_r in NODES:
                if name.startswith(item[:-1]):
                    emit_node(name)
        else:
            emit_node(item)
    assert emitted == set(by_name), set(by_name) - emitted
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{w}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text) < 64 * 1024, len(text)
    (ROOT / "panel.svg.fragment").write_text(text)
    names = sorted(index.items(), key=lambda kv: kv[1])
    # the projection and the geometry, for display.cpp (Q16 px a mm; mm)
    q16 = lambda v: int(round(v * 65536))
    sty = [stylus(R_OUTER + (R_INNER - R_OUTER) * i / 16) for i in range(17)]
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers and the projection.",
            "#include <opxy/types.hpp>", "", "namespace dust {", "namespace art {",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            "// screen = (cx + ax x + ay y, cy - (bx x + by y + bz z)), x y z in mm; Q16 px a mm",
            f"const opxy::i32 cx = {q16(CX)}, cy = {q16(CY)};",
            f"const opxy::i32 ax = {q16(AX)}, ay = {q16(AY)}, bx = {q16(BX)}, by = {q16(BY)}, bz = {q16(BZ)};",
            "// the parts, in mm (Q4: 16 a mm)",
            f"const opxy::i32 r_record = {int(R_RECORD * 16)}, r_label = {int(R_LABEL * 16)}, r_hole = {int(R_HOLE * 16)};",
            f"const opxy::i32 pivot_x = {int(PIVOT[0] * 16)}, pivot_y = {int(PIVOT[1] * 16)}, pivot_z = {int(PIVOT[2] * 16)};",
            f"const opxy::i32 arm = {int(ARM * 16)}, stub = {int(STUB * 16)};",
            f"const opxy::i32 r_outer = {int(R_OUTER * 16)}, r_inner = {int(R_INNER * 16)};",
            f"const opxy::i32 brush_x = {int(BRUSH_PIVOT[0] * 16)}, brush_y = {int(BRUSH_PIVOT[1] * 16)}, brush_z = {int(BRUSH_PIVOT[2] * 16)};",
            f"const opxy::i32 brush_at_x = {int(BRUSH_AT[0] * 16)}, brush_at_y = {int(BRUSH_AT[1] * 16)};",
            f"const opxy::i32 cue_x = {int(CUE[0] * 16)}, cue_y = {int(CUE[1] * 16)}, cue_z = {int(CUE[2] * 16)};",
            f"const opxy::i32 speed_x = {int(SPEED[0] * 16)}, speed_y = {int(SPEED[1] * 16)}, speed_z = {int(SPEED[2] * 16)};",
            "// the stylus's angle about the spindle (65536 a turn) on 17 grooves from bar 1's to bar 8's",
            "const opxy::i32 stylus_angle[17] = {" + ", ".join(str(int(round(a / (2 * math.pi) * 65536))) for _x, _y, a in sty) + "};",
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {idx}," for name, idx in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    n_static = sum(len(v) for v in LAYERS.values())
    print(f"wrote panel.svg.fragment ({len(text)} bytes) and art.h: {bound} nodes, {n_static} static paths, {len(TEXTS)} numerals")


if __name__ == "__main__":
    build()
