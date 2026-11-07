#!/usr/bin/env python3
"""Writes FLICKER's artwork, panel.svg.fragment, and art.h (the node numbers
and the geometry display.cpp places them by). Run it after changing anything
here and commit both.

    python3 flicker/tools/make_panel.py

The screen is a strip of 16 mm sound film lying on a near-black slat, drawn
isometric (30 degree edges, no vanishing point): four frames of it on the
slat, a frame every 50 px along the film, the film 56 px across. Along the
film's back edge one perforation a frame and the empty picture frames, dim;
along its front edge the soundtrack, where display.cpp lays the selected
key's drawing in each frame. Under the clear film the slat carries the
animator's pencil guides for each frame and a fixed dim scale along its
front edge. A small gate bridges the film at its middle: two posts and a bar
that FLUTTER lifts, with the slit's light across the track under it. The film
runs on over both ends of the slat into the dark: black masks over the dark
past each end, and the slat's end face, are drawn over what moves, so a frame
pulled off the slat goes over its edge.

Everything a control or a note moves is a bound node, drawn around its own
pivot (0, 0); display.cpp places it. Parts that never move are plain
artwork, drawn back to front (artwork order is stacking order). Lines are
thin closed ribbons; no path has more than one subpath or 64 points, none
turns sharper than SHARPEST degrees, neighbouring points stay 1/8 px apart
and every outline closes over at least 1/4 px. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "flicker"
PATHS, POINTS = 4, 64

# ---------------------------------------------------------------- the isometric
C30 = 887 / 1024      # cos 30 degrees as display.cpp has it (Q10)
S30 = 0.5
OX, OY = 177.5, 71.0  # where the film plane's (0, 0) lies before its height
SLAT_H = 8.0          # the slat's thickness: the film lies on it at z = SLAT_H


def iso(x, y, z=SLAT_H):
    """A point x along the film, y across it, z up, on the screen."""
    return (OX + (x - y) * C30, OY + (x + y) * S30 - z)


def rel(dx, dy, dz=0.0):
    """An offset in the same projection, about a node's pivot."""
    return ((dx - dy) * C30, (dx + dy) * S30 - dz)


# ---------------------------------------------------------------- geometry (film px)
PITCH = 50            # a frame
FRAMES = 4
LENGTH = PITCH * FRAMES                     # the slat's length: four frames
FILM_W = 56                                 # the film across
MARGIN = 5                                  # the slat past the film's edges
PERF = (2.0, 6.0, 2.5, 6.0)                 # x0, x1, y0, y1 within a frame
PIC = (9.0, 47.0, 9.0, 26.0)
DIVIDER_Y = 28.5                            # between the pictures and the track
TRACK_C, TRACK_HALF = 42.0, 12.0            # the soundtrack: 30..54 across
DRAW_X0, DRAW_X1 = 2.0, 48.0                # where a frame's drawing lies along it
FACE_HALF_X, FACE_HALF_Y = 33.0, 24.0       # the drawing as display.cpp draws it face on
GATE = LENGTH / 2                           # the gate and the slit: the slat's middle
SLIT_Y0, SLIT_Y1 = TRACK_C - TRACK_HALF - 0.5, TRACK_C + TRACK_HALF + 0.5
BAR_HALF_X, BAR_Y0, BAR_Y1, BAR_THICK = 3.0, -5.5, FILM_W + 5.5, 3.0
BAR_Z = 8.0                                 # the bar's foot above the film at rest, clear of the slit
LIFT = 8.0                                  # how far FLUTTER lifts it
POST_HALF = 1.25
POST_TOP = BAR_Z + LIFT + BAR_THICK + 2.5
POSTS_Y = (-3.0, FILM_W + 3.0)
GUIDE_LENGTH = 21.0                         # a pencil guide's segment, screen px
SPECKS = ((34.5, 51.5), (19.0, 13.5))       # DUST's two specks a frame, decided once
SCALE_STEP = 5
MASK_REACH = 34.0                           # how far past each end the masks cover

# greys
G2, G3, G4 = "#4B4B4B", "#969696", "#CDCDCD"   # the grey code: encoders 2, 3, 4
SLAT, SEAM = "#191919", "#646464"
FILM_BASE, FILM_EDGE, DIVIDER = "#0E0E0E", "#646464", "#262626"
FRAME = "#323232"
SCALE = "#3C3C3C"
LIGHT = "#E6E6E6"
SIDE = "#7D7D7D"     # a light solid's right face: one step down the ramp
BLACK = "#000000"

# The palette: 0 black (parked); 1..6 the drawing's ink at INK's weights;
# 7..9 the pencil guides at INK's weights; 10 the frames and perforations,
# 11 them fading over an end of the slat; 12..14 the drawing fading there.
DRAW_W = [0.45, 0.55, 0.65, 0.75, 0.95, 1.2]
GUIDE_W = [0.4, 0.55, 0.8]
PALETTE = ([(BLACK, 1.0)] + [(G4, w) for w in DRAW_W] + [(G2, w) for w in GUIDE_W]
           + [(FRAME, 0.5), ("#1E1E1E", 0.5), ("#969696", 0.7), ("#646464", 0.7), ("#3A3A3A", 0.7), (BLACK, 1.0)])
assert len(PALETTE) == 16

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
    # Neighbouring points closer than 1/8 px merge on the device, and a
    # closing edge under 1/4 px draws as a long sliver.
    for i in range(n - 1):
        (ax, ay), (bx, by) = out[i], out[i + 1]
        assert abs(bx - ax) >= 0.125 or abs(by - ay) >= 0.125, f"points {i} and {i + 1} merge"
    assert math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) >= 0.25, "closes over under 1/4 px"
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


def segment(a, b, w=0.3):
    return ribbon([a, b], w)


def quad(x0, x1, y0, y1, z=SLAT_H):
    """A rectangle lying on the film plane (or at height z), as a closed outline."""
    return [iso(x0, y0, z), iso(x1, y0, z), iso(x1, y1, z), iso(x0, y1, z)]


# ---------------------------------------------------------------- the scene, back to front
LAYERS = []   # ("static", d, stroke, width, fill), ("path", k) or ("node", name)
NODES = {}    # name -> (points or None, circle or None, fill, stroke, width)


def static(points, col, w=0.5, fill="none"):
    LAYERS.append(("static", poly(points), col, w, fill))


def node(name, points=None, circle=None, fill="none", stroke=G4, w=1.0):
    NODES[name] = (points, circle, fill, stroke, w)
    LAYERS.append(("node", name))


def box(x0, x1, y0, y1, z0, z1, top, left, right, edge, ew):
    """A box's three faces seen: the left (y = y1) and right (x = x1) faces, then the top."""
    static([iso(x0, y1, z1), iso(x1, y1, z1), iso(x1, y1, z0), iso(x0, y1, z0)], edge, ew, left)
    static([iso(x1, y0, z1), iso(x1, y1, z1), iso(x1, y1, z0), iso(x1, y0, z0)], edge, ew, right)
    static([iso(x0, y0, z1), iso(x1, y0, z1), iso(x1, y1, z1), iso(x0, y1, z1)], edge, ew, top)


def slat():
    # a near-black slat: its top and left faces 191919 with 646464 seams;
    # its right face, the end, is drawn over what moves (ends)
    y0, y1 = -MARGIN, FILM_W + MARGIN
    static([iso(0, y1), iso(LENGTH, y1), iso(LENGTH, y1, 0), iso(0, y1, 0)], SEAM, 0.5, SLAT)
    static(quad(0, LENGTH, y0, y1), SEAM, 0.5, SLAT)
    # a fixed dim scale along its front edge: a tick every 5 px, longer at
    # each frame line, where the film's frames come to rest
    for k in range(1, LENGTH // SCALE_STEP):
        x = k * SCALE_STEP
        if abs(x - GATE) < 2:
            continue   # under the gate's front post
        long_ = x % PITCH == 0
        static(segment(iso(x, FILM_W + 1.2), iso(x, FILM_W + (4.2 if long_ else 3.2)), 0.3), SCALE, 0.35)


def film():
    # the clear film the length of the slat, its two edges, and the dim line
    # between its pictures and its soundtrack
    static(quad(0, LENGTH, 0, FILM_W), FILM_BASE, 0.0, FILM_BASE)
    for y in (0.0, FILM_W):
        static(segment(iso(0, y), iso(LENGTH, y), 0.25), FILM_EDGE, 0.4)
    static(segment(iso(0, DIVIDER_Y), iso(LENGTH, DIVIDER_Y), 0.2), DIVIDER, 0.35)


def guides():
    # the pencil guides of each frame's taper, ruled on the slat where the
    # frames come to rest: top and bottom, each one or two straight segments
    # of GUIDE_LENGTH (display.cpp turns, scales and places them)
    for k in range(FRAMES):
        for side in ("t", "b"):
            for seg in ("a", "b"):
                node(f"guide_{k}{side}{seg}", ribbon([(0, 0), (GUIDE_LENGTH, 0)], 0.25), stroke=G2, w=0.55)


def moving():
    # the drawings: the reshaping paths 0..3 (placed by display.cpp)
    for k in range(PATHS):
        LAYERS.append(("path", k))
    # each frame's picture, empty, and its perforation, centred on their nodes
    hx, hy = (PIC[1] - PIC[0]) / 2, (PIC[3] - PIC[2]) / 2
    for k in range(FRAMES):
        node(f"pic_{k}", [rel(-hx, -hy), rel(hx, -hy), rel(hx, hy), rel(-hx, hy)], stroke=FRAME, w=0.5)
    px, py = (PERF[1] - PERF[0]) / 2, (PERF[3] - PERF[2]) / 2
    for k in range(FRAMES):
        node(f"perf_{k}", [rel(-px, -py), rel(px, -py), rel(px, py), rel(-px, py)], stroke=FRAME, w=0.5)
    # DUST: specks on each frame, filled
    for k in range(FRAMES):
        for j in range(len(SPECKS)):
            node(f"speck_{k}{j}", circle=(0, 0, 0.8), fill=G4, stroke=G4, w=0.3)


def ends():
    # The film runs on over both ends of the slat: past the near end it drops
    # behind the slat, so a black mask over the dark there hides what is
    # pulled past it; past the far end, a mask and then the slat's end face
    # (black, with light seams) hide it as it goes over the edge, and the
    # film's two edges run down the end face.
    static(quad(-MASK_REACH, -0.3, -2, FILM_W + 2), BLACK, 0.0, BLACK)
    static(quad(LENGTH + 0.3, LENGTH + MASK_REACH, -2, FILM_W + 2), BLACK, 0.0, BLACK)
    y0, y1 = -MARGIN, FILM_W + MARGIN
    static([iso(LENGTH, y0), iso(LENGTH, y1), iso(LENGTH, y1, 0), iso(LENGTH, y0, 0)], SEAM, 0.5, BLACK)
    for y in (0.0, FILM_W):
        static(segment(iso(LENGTH, y, SLAT_H - 0.3), iso(LENGTH, y, 1.0), 0.25), "#3A3A3A", 0.4)


def slits():
    # SLIT: the light's line across the track, 0.8 to 6 px wide (16 widths)
    for k in range(16):
        w = 0.8 + (6.0 - 0.8) * (k / 15) ** 2.4
        a, b = SLIT_Y0 - TRACK_C, SLIT_Y1 - TRACK_C
        node(f"slit_{k}", [rel(-w / 2, a), rel(w / 2, a), rel(w / 2, b), rel(-w / 2, b)], fill=LIGHT, stroke=LIGHT, w=0.3)


def gate():
    # two posts beside the film, and the bar that bridges it on them; the
    # bar's three faces share one pivot (its foot's middle) and move together
    for py in POSTS_Y:
        box(GATE - POST_HALF, GATE + POST_HALF, py - POST_HALF, py + POST_HALF, SLAT_H, SLAT_H + POST_TOP, G3, G3, SIDE, BLACK, 0.4)
    yc = (BAR_Y0 + BAR_Y1) / 2
    a, b, h, t = BAR_Y0 - yc, BAR_Y1 - yc, BAR_HALF_X, BAR_THICK
    node("bar_left", [rel(-h, b, t), rel(h, b, t), rel(h, b, 0), rel(-h, b, 0)], fill=G3, stroke=BLACK, w=0.4)
    node("bar_right", [rel(h, a, t), rel(h, b, t), rel(h, b, 0), rel(h, a, 0)], fill=SIDE, stroke=BLACK, w=0.4)
    node("bar_top", [rel(-h, a, t), rel(h, a, t), rel(h, b, t), rel(-h, b, t)], fill=G3, stroke=BLACK, w=0.4)


def build():
    slat()
    film()
    guides()
    moving()
    ends()
    slits()
    gate()
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
            stroke = f'stroke="{col}" stroke-width="{fmt(w)}"' if w > 0 else 'stroke="none"'
            lines.append(f'<path d="{d}" fill="{fill}" {stroke}/>')
            statics += 1
        elif kind == "path":
            p = layer[1]
            pts = [(30 * math.cos(2 * math.pi * i / POINTS), 15 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            lines.append(f'<path id="{ID}_node_{p}" transform="matrix(1 0 0 1 640 320)" d="{poly(pts)}" fill="none" '
                         f'stroke="{G4}" stroke-width="0.75"/>')
        else:
            name = layer[1]
            points, circle, fill, stroke, w = NODES[name]
            idx = index[name]
            if circle:
                cx, cy, r = circle
                lines.append(f'<circle id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" cx="{fmt(cx)}" cy="{fmt(cy)}" '
                             f'r="{fmt(r)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
            else:
                lines.append(f'<path id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                             f'fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text.encode()) <= 64 * 1024, "artwork over 64 KiB"
    (ROOT / "panel.svg.fragment").write_text(text)

    q10 = lambda v: int(round(v * 1024))
    # the face-on drawing (x +-33, y +-24 px) laid along the track and projected
    a = (DRAW_X1 - DRAW_X0) / (2 * FACE_HALF_X)
    b = TRACK_HALF / FACE_HALF_Y
    along_x, along_y = round(a * C30 * 1024), round(a * S30 * 1024)
    across_x, across_y = round(b * C30 * 1024), round(b * S30 * 1024)
    # spikes in the drawing (near-vertical turns face on) must open under the
    # projection, never close: x's share across y's direction outweighs y's length
    assert a * C30 > b
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers and the",
            "// geometry display.cpp places them by (Q10 px). Film coordinates run x along",
            "// the film, y across it, on the slat's top; iso_x, iso_y put them on the screen.",
            "#include <opxy/types.hpp>", "",
            "namespace flicker {", "namespace art {",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            f"const opxy::i32 iso_x = {q10(OX)}, iso_y = {q10(OY - SLAT_H)}, cos30 = {round(C30 * 1024)};",
            f"const opxy::i32 pitch = {q10(PITCH)}, pitch_x = {q10(PITCH * C30)}, frames = {FRAMES};",
            f"const opxy::i32 track_x = {q10((DRAW_X0 + DRAW_X1) / 2)}, track_y = {q10(TRACK_C)};",
            f"const opxy::i32 picture_x = {q10((PIC[0] + PIC[1]) / 2)}, picture_y = {q10((PIC[2] + PIC[3]) / 2)};",
            f"const opxy::i32 perf_x = {q10((PERF[0] + PERF[1]) / 2)}, perf_y = {q10((PERF[2] + PERF[3]) / 2)};",
            f"const opxy::i32 speck_x[{len(SPECKS)}] = {{{', '.join(str(q10(s[0])) for s in SPECKS)}}}, "
            f"speck_y[{len(SPECKS)}] = {{{', '.join(str(q10(s[1])) for s in SPECKS)}}};",
            f"const opxy::i32 gate = {q10(GATE)}, bar_y = {q10((BAR_Y0 + BAR_Y1) / 2)}, bar_z = {q10(BAR_Z)}, lift = {q10(LIFT)};",
            f"const opxy::i32 guide_length = {q10(GUIDE_LENGTH)};",
            "// the face-on drawing (Q10 px, x along, y across) on the track: screen x =",
            "// (x along_x - y across_x) / 1024, screen y = (x along_y + y across_y) / 1024",
            f"const opxy::i32 along_x = {along_x}, along_y = {along_y}, across_x = {across_x}, across_y = {across_y};",
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {index[name]}," for name in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    print(f"wrote panel.svg.fragment ({len(text.encode())} bytes) and art.h: {bound} nodes, {statics} static paths")


if __name__ == "__main__":
    build()
