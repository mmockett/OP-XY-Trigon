#!/usr/bin/env python3
"""Writes FLICKER's artwork, panel.svg.fragment, and art.h (the node numbers
and the geometry display.cpp places them by). Run it after changing anything
here and commit both.

    python3 flicker/tools/make_panel.py

The screen is a strip of 16 mm sound film running right to left through a
projector's sound head, seen face on. The film is drawn to its own
proportions at 9.45 px a millimetre (a frame pitch of 7.62 mm is 72 px; a
perforation 1.27 x 1.83 mm is 12 x 17 px; a picture 7.49 x 10.26 mm is
drawn 64 x 80 px, a little shortened), its soundtrack widened from 2.5 mm
to 48 px so the drawing reads. The sound head's castings cover the film either side of
its gate, so three frame pitches show: the exciter lamp's house and lens
barrel stand over the track at the gate, the photocell under it; a sprocket
roller at each side of the gate; the film comes in over a guide roller in a
slack loop.

Everything a control or a note moves is a bound node, drawn around its own
pivot (0, 0); display.cpp places it. Parts that never move are plain
artwork, drawn back to front (artwork order is stacking order): the film,
then what moves on it, then the castings and the lamp in front. Lines are
thin closed ribbons; no path has more than one subpath or 64 points. Pure
Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "flicker"
PATHS, POINTS = 4, 64

# ---------------------------------------------------------------- geometry
PITCH = 72            # px a frame
GATE = 240            # the slit's x
WIN_L, WIN_R = GATE - 108, GATE + 108   # the gate's window: three pitches
FILM_TOP, FILM_BOT = 41, 205
PERF_Y0, PERF_Y1, PERF_W, PERF_R = 48, 65, 12, 2.2
PIC_Y0, PIC_Y1, PIC_W, PIC_R = 70, 150, 64, 4.0
TRACK_Y0, TRACK_Y1 = 154, 202
TRACK_C = (TRACK_Y0 + TRACK_Y1) / 2
ROLL_Y = (PERF_Y0 + PERF_Y1) / 2      # the sprocket rollers' centres, on the perforation row
ROLL_R = 18.335                        # their pitch circle: five teeth (225 degrees) a frame
ROLL_TEETH = 8
LOOP_L, LOOP_R, LOOP_TOP = 404, 444, 60   # the slack loop's sides, hung from two rollers' centres
FLY_X, FLY_Y, FLY_R = 60, 124, 40         # the flywheel that steadies the film
LOOP_ARC = (LOOP_R - LOOP_L) / 2
SLIT_TOP, SLIT_BOT = TRACK_Y0 - 1, TRACK_Y1 + 1

# greys
INK = "#F2F2F2"       # the drawing: the brightest thing on screen
LIGHT = "#FFFFFF"     # the slit's light
EDGE = "#8C8C8C"      # the film's edges
DIM = "#5A5A5A"       # the picture frames, the track's edges
PERF = "#A8A8A8"
BASE = "#0A0A0A"      # the film base, dark
BODY = "#181818"      # the castings, the lamp house
FIT = "#262626"       # fittings
CAST = "#6A6A6A"      # their outline
METAL = "#9A9A9A"

# The palette: 0 black (parked); 1..8 a ramp of greys at 1 px; 9 the
# drawing's white; 10 the slit's light; 11 the hair's grey; the rest spare.
RAMP = ["#2E2E2E", "#444444", "#5C5C5C", "#767676", "#909090", "#ACACAC", "#C8C8C8", "#E6E6E6"]
PALETTE = [("#000000", 1.0)] + [(g, 1.0) for g in RAMP] + [
    (INK, 1.0), (LIGHT, 1.0), ("#7A7A7A", 1.0), ("#000000", 1.0), ("#000000", 1.0), ("#000000", 1.0), ("#000000", 1.0)]


SHARPEST = 150.0   # degrees: the sharpest turn an outline may make


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def poly(points):
    out = []
    for p in points:
        q = (round(p[0], 2), round(p[1], 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert 3 <= len(out) <= 64, len(out)
    assert len(set(out)) == len(out), "repeated point"
    # The OP-XY strokes an outline with mitred joins: an outline that turns
    # (nearly) straight back on itself draws a mitre spike tens of pixels
    # long (the sprocket rollers' streaks on an early build). Refuse any turn
    # sharper than SHARPEST degrees.
    n = len(out)
    for i in range(n):
        (ax, ay), (bx, by), (cx, cy) = out[i - 1], out[i], out[(i + 1) % n]
        ux, uy, vx, vy = bx - ax, by - ay, cx - bx, cy - by
        turn = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        assert turn <= SHARPEST, f"turn of {turn:.1f} degrees at point {i}"
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
    return [(cx + rx * math.cos(2 * math.pi * i / n) * c - ry * math.sin(2 * math.pi * i / n) * s,
             cy + rx * math.cos(2 * math.pi * i / n) * s + ry * math.sin(2 * math.pi * i / n) * c) for i in range(n)]


def arc(cx, cy, r, a0, a1, n=12, w=0.5):
    return ribbon([(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
                   for i in range(n + 1)], w)


# ---------------------------------------------------------------- the scene, back to front
LAYERS = []   # ("static", d, stroke, width, fill) or ("node", name)
NODES = {}    # name -> (points or None, circle or None, fill, stroke)


def static(points, col, w=1.0, fill="none"):
    LAYERS.append(("static", poly(points), col, w, fill))


def static_circle(cx, cy, r, col, w=1.0, fill="none"):
    LAYERS.append(("circle", (cx, cy, r), col, w, fill))


def node(name, points=None, circle=None, fill="none", stroke=METAL):
    NODES[name] = (points, circle, fill, stroke)
    LAYERS.append(("node", name))


def film():
    # the film base in the gate's window, dark, and its two edges
    static([(WIN_L - 20, FILM_TOP), (WIN_R + 20, FILM_TOP), (WIN_R + 20, FILM_BOT), (WIN_L - 20, FILM_BOT)], BASE, 1.0, BASE)
    static(line(WIN_L - 20, FILM_TOP, WIN_R + 20, FILM_TOP, 0.6), EDGE, 0.8)
    static(line(WIN_L - 20, FILM_BOT, WIN_R + 20, FILM_BOT, 0.6), EDGE, 0.8)
    # the soundtrack's two edges: the drawing lives between them
    static(line(WIN_L - 20, TRACK_Y0, WIN_R + 20, TRACK_Y0, 0.4), DIM, 0.8)
    static(line(WIN_L - 20, TRACK_Y1, WIN_R + 20, TRACK_Y1, 0.4), DIM, 0.8)


def moving():
    # the drawings: the reshaping paths 0..3 (placed in build)
    for k in range(PATHS):
        LAYERS.append(("path", k))
    # each frame's picture: an empty frame, its corners rounded as a camera
    # gate's are; centred on its node
    for k in range(4):
        node(f"frame_{k}", rect(-PIC_W / 2, -(PIC_Y1 - PIC_Y0) / 2, PIC_W / 2, (PIC_Y1 - PIC_Y0) / 2, PIC_R, 2), stroke=DIM)
    # one perforation a frame, on the frame line (single-perforation sound film)
    for k in range(3):
        node(f"perf_{k}", rect(-PERF_W / 2, -(PERF_Y1 - PERF_Y0) / 2, PERF_W / 2, (PERF_Y1 - PERF_Y0) / 2, PERF_R, 2), stroke=PERF)
    # DUST: a hair caught on each frame, curling over the picture's foot
    for k in range(4):
        node(f"hair_{k}", ribbon([(-17, 3), (-12, 0.5), (-6, -0.6), (0, 0.6), (5, 2.6), (10, 3.4), (14, 2.2), (17, -0.6), (19, -3.6)], 0.6),
             stroke=EDGE)


def castings():
    # the sound head's castings either side of the gate, over the film:
    # dark iron bodies with a chamfered inner edge, a channel lip, screws
    for side in (-1, 1):
        x_in = WIN_L if side < 0 else WIN_R
        x_out = -2 if side < 0 else 482
        y0, y1 = 38.6, 212
        pts = [(x_out, y0), (x_in + side * 6, y0), (x_in, y0 + 6), (x_in, y1 - 6), (x_in + side * 6, y1), (x_out, y1)]
        static(pts, CAST, 1.0, BODY)
        # the film channel's lip: where the film runs in under the casting
        static(line(x_in + side * 4, FILM_TOP + 30, x_in + side * 4, FILM_BOT - 4, 0.5), "#3A3A3A", 0.8)
        # screws by the gate
        for y in (112, 186):
            cx = x_in + side * 16
            static_circle(cx, y, 3.2, "#5E5E5E", 0.8, FIT)
            static(line(cx - 2, y + 0.6, cx + 2, y - 0.6, 0.5), "#5E5E5E", 0.6)


def flywheel():
    # the sound head's flywheel on the left casting, which steadies the
    # film's speed past the slit: a rim, its spokes (they turn with the
    # film, one spoke a frame) and the hub
    static_circle(FLY_X, FLY_Y, FLY_R, "#7A7A7A", 1.0, FIT)
    static_circle(FLY_X, FLY_Y, FLY_R - 6, "#5A5A5A", 0.8, "#101010")
    spokes = []
    for k in range(5):
        a = 2 * math.pi * k / 5
        for da, r in ((-0.34, 9.0), (-0.09, 13.0), (-0.055, FLY_R - 7.5), (0.055, FLY_R - 7.5), (0.09, 13.0)):
            spokes.append((r * math.cos(a + da), r * math.sin(a + da)))
    node("spokes", spokes, stroke="#9A9A9A")
    static_circle(FLY_X, FLY_Y, 7.5, "#8A8A8A", 0.9, BODY)
    static_circle(FLY_X, FLY_Y, 2.4, "#8A8A8A", 0.8, FIT)


def lamp():
    # the exciter lamp's house over the gate: a domed can with its vents and
    # a clamp band, the optical tube stepping down to the slit plate, a
    # focusing collar; the photocell's cup under the film
    cx = GATE
    static(rect(cx - 22, 40.5, cx + 22, 46, 1.5, 1), "#7E7E7E", 0.9, FIT)    # the lid
    static(rect(cx - 19, 46, cx + 19, 90, 2.5, 2), "#7E7E7E", 1.0, BODY)     # the house
    for x in (-9, -3, 3, 9):                                                # its louvres
        static(rect(cx + x - 1, 52, cx + x + 1, 76, 1, 1), "#4A4A4A", 0.6, "#050505")
    static_circle(cx + 22.5, 66, 3.2, "#7E7E7E", 0.9, FIT)                 # the lamp's thumbscrew
    static(rect(cx - 22, 90, cx + 22, 95, 1.5, 1), "#7E7E7E", 0.9, FIT)    # the flange
    static_circle(cx - 17, 92.5, 1.3, "#9A9A9A", 0.6, BODY)
    static_circle(cx + 17, 92.5, 1.3, "#9A9A9A", 0.6, BODY)
    static(rect(cx - 10, 95, cx + 10, 134, 2, 2), "#7E7E7E", 1.0, BODY)
    static(rect(cx - 12.5, 112, cx + 12.5, 118, 1, 1), "#6A6A6A", 0.8, FIT)   # the focusing collar
    for x in (-8, -4, 0, 4, 8):
        static(line(cx + x, 112.8, cx + x, 117.2, 0.4), "#4A4A4A", 0.5)
    static(rect(cx - 6, 134, cx + 6, 149, 1.5, 1), "#7E7E7E", 1.0, BODY)
    static(rect(cx - 13, 149, cx + 13, 152.5, 1, 1), "#9A9A9A", 0.8, FIT)   # the slit plate
    # the photocell under the film: its cup and window
    static(rect(cx - 15, 208, cx + 15, 230, 4, 2), "#7E7E7E", 1.0, BODY)
    static(rect(cx - 8, 211, cx + 8, 216, 1.5, 1), "#9A9A9A", 0.8, "#050505")


def rollers():
    # the sprocket rollers at the gate's edges: a dark drum (still), its
    # toothed flange (it turns with the film), the hub
    for side, x in (("l", WIN_L), ("r", WIN_R)):
        static_circle(x, ROLL_Y, ROLL_R - 2.6, "#7A7A7A", 0.9, FIT)
        # one outline round all eight teeth, always turning forward: each
        # tooth's flanks and tip, then the root between it and the next,
        # strictly inside the gap (a root that ran past the next tooth's
        # flank doubled back on itself, which the OP-XY draws as streaks).
        # It starts on a tip, where both edges are longest.
        teeth = []
        n = ROLL_TEETH
        step = 2 * math.pi / n
        r0, r1 = ROLL_R - 2.6, ROLL_R + 0.2
        base, tip = 0.21, 0.12
        for t in range(n):
            a = step * t
            for da, r in ((-base, r0), (-tip, r1), (tip, r1), (base, r0)):
                teeth.append((r * math.cos(a + da), r * math.sin(a + da)))
            gap = step - 2 * base
            for k in range(1, 4):
                da = base + gap * k / 4
                teeth.append((r0 * math.cos(a + da), r0 * math.sin(a + da)))
        teeth = teeth[2:] + teeth[:2]
        node(f"roller_{side}", teeth, stroke="#C8C8C8")
        static_circle(x, ROLL_Y, 5.5, "#8A8A8A", 0.9, BODY)
        static_circle(x, ROLL_Y, 1.8, "#8A8A8A", 0.8, FIT)


def toothed(cx, cy, r, n, col, fill=FIT):
    """A small sprocket, still: a toothed rim round a dark disc."""
    pts = []
    for t in range(n):
        a = 2 * math.pi * t / n
        for da, rr in ((-0.5, r - 2), (-0.26, r), (0.26, r), (0.5, r - 2)):
            pts.append((cx + rr * math.cos(a + da * 2 * math.pi / n), cy + rr * math.sin(a + da * 2 * math.pi / n)))
    static(pts, col, 0.9, fill)


def loop():
    # the incoming film, edge on: down the screen's right edge, under the
    # feed sprocket, hanging in a slack loop, up over a guide roller and into
    # the sound head's channel. The loop's sides and foot are nodes (FLUTTER
    # lowers it).
    FILM = "#B4B4B4"
    fx, gx = LOOP_R + 12, LOOP_L - 8
    toothed(fx, LOOP_TOP, 9.5, 8, "#7A7A7A")
    static_circle(fx, LOOP_TOP, 3, "#7A7A7A", 0.8, BODY)
    static_circle(gx, LOOP_TOP, 6, "#7A7A7A", 0.9, FIT)
    static_circle(gx, LOOP_TOP, 2.2, "#7A7A7A", 0.8, BODY)
    static(line(fx + 12, 38.4, fx + 12, LOOP_TOP, 0.7), FILM, 0.8)
    static(arc(fx, LOOP_TOP, 12, 0, 180, 12, 0.7), FILM, 0.8)
    for side in ("l", "r"):
        for k in range(6):
            node(f"loop_{side}{k}", line(0, 0, 0, 26, 0.7), stroke=FILM)
    node("loop_foot", arc(0, 0, LOOP_ARC, 0, 180, 18, 0.7), stroke=FILM)
    static(arc(gx, LOOP_TOP, 8.6, 180, 360, 10, 0.7), FILM, 0.8)
    static(line(gx - 8.6, LOOP_TOP, gx - 8.6, LOOP_TOP + 16, 0.7), FILM, 0.8)
    # the channel's mouth the film runs into
    static(rect(gx - 13, LOOP_TOP + 16, gx - 4, LOOP_TOP + 20, 1, 1), "#5A5A5A", 0.8, "#050505")


def slits():
    # SLIT: the light's line across the track, 1.2 to 7 px wide (16 widths)
    for k in range(16):
        w = 0.8 + (6.0 - 0.8) * (k / 15) ** 2.4
        node(f"slit_{k}", [(-w / 2, SLIT_TOP - TRACK_C), (w / 2, SLIT_TOP - TRACK_C), (w / 2, SLIT_BOT - TRACK_C), (-w / 2, SLIT_BOT - TRACK_C)],
             fill=LIGHT, stroke=LIGHT)


def build():
    film()
    moving()
    castings()
    flywheel()
    loop()
    rollers()
    lamp()
    slits()
    n_header = 8
    names = list(NODES)
    first_header = PATHS
    index = {name: PATHS + n_header + k for k, name in enumerate(names)}
    bound = PATHS + n_header + len(names)
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
    for layer in LAYERS:
        kind = layer[0]
        if kind == "static":
            _, d, col, w, fill = layer
            lines.append(f'<path d="{d}" fill="{fill}" stroke="{col}" stroke-width="{fmt(w)}"/>')
            statics += 1
        elif kind == "circle":
            _, (cx, cy, r), col, w, fill = layer
            lines.append(f'<circle cx="{fmt(cx)}" cy="{fmt(cy)}" r="{fmt(r)}" fill="{fill}" stroke="{col}" stroke-width="{fmt(w)}"/>')
            statics += 1
        elif kind == "path":
            p = layer[1]
            pts = [(30 * math.cos(2 * math.pi * i / POINTS), 15 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            lines.append(f'<path id="{ID}_node_{p}" d="{poly(pts)}" fill="none" stroke="{INK}" stroke-width="1"/>')
        else:
            name = layer[1]
            points, circle, fill, stroke = NODES[name]
            idx = index[name]
            if circle:
                cx, cy, r = circle
                lines.append(f'<circle id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" cx="{fmt(cx)}" cy="{fmt(cy)}" '
                             f'r="{fmt(r)}" fill="{fill}" stroke="{stroke}" stroke-width="1"/>')
            else:
                lines.append(f'<path id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                             f'fill="{fill}" stroke="{stroke}" stroke-width="1"/>')
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    (ROOT / "panel.svg.fragment").write_text(text)
    q10 = lambda v: int(round(v * 1024))
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers and the",
            "// geometry display.cpp places them by (Q10 px).", "#include <opxy/types.hpp>", "",
            "namespace flicker {", "namespace art {",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            f"const opxy::i32 pitch = {q10(PITCH)}, gate = {q10(GATE)}, window_left = {q10(WIN_L)}, window_right = {q10(WIN_R)};",
            f"const opxy::i32 picture_y = {q10((PIC_Y0 + PIC_Y1) / 2)}, perf_y = {q10(ROLL_Y)}, track_y = {q10(TRACK_C)};",
            f"const opxy::i32 track_half = {q10((TRACK_Y1 - TRACK_Y0) / 2)}, roller_y = {q10(ROLL_Y)}, roller_radius = {q10(ROLL_R)};",
            f"const opxy::i32 loop_left = {q10(LOOP_L)}, loop_right = {q10(LOOP_R)}, loop_top = {q10(LOOP_TOP)}, loop_arc = {q10(LOOP_ARC)};",
            f"const opxy::i32 flywheel_x = {q10(FLY_X)}, flywheel_y = {q10(FLY_Y)};",
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {index[name]}," for name in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    print(f"wrote panel.svg.fragment ({len(text.encode())} bytes) and art.h: {bound} nodes, {statics} static paths")


if __name__ == "__main__":
    build()
