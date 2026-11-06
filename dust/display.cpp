#include "display.h"
#include "dsp.h"

// DUST's screen: an isometric record deck in the stock look. The plinth,
// platter, pillar, spindle and the brush's post are artwork; the groove's
// two halves, the picked bar and the arm are the four reshaping outlines,
// drawn in the projection make_panel.py writes into art.h; the record, its
// label and cue sticker, the counterweight, the headshell and the dust brush
// are nodes placed from the key's controls. The grey is the encoder's: the
// record (SPEED, CUE) hollow, the grooves (PICK, WEAR) 4B4B4B, the arm
// (LIFT, WARP) 969696, the counterweight and brush (WEIGHT, DRAG) CDCDCD.
// A hit drops the head onto the record and spins it a whole number of turns
// at its speed, settling as the hit ends; the head lifts at LIFT.
namespace dust {
namespace view {
using namespace opxy::draw;

// ---------------------------------------------------------------- arithmetic
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
// sin and cos of a turn (65536 a turn), Q14
inline i32 sn(i32 turn) { return sine(static_cast<u32>(turn) << 16) >> 17; }
inline i32 cs(i32 turn) { return sn(turn + 16384); }
// t a Q10 unit eased out: 1 - (1 - t)^2
inline i32 ease_out(i32 t) { t = clampi(t, 0, 1024); return 1024 - (((1024 - t) * (1024 - t)) >> 10); }
// eased in and out: 3t^2 - 2t^3
inline i32 ease(i32 t) { t = clampi(t, 0, 1024); const i32 t2 = (t * t) >> 10; return (3 * t2 - ((2 * t2 * t) >> 10)); }
// x / y for positive values, Q10, through the logarithms (no divide)
inline i32 ratio(i32 x, i32 y) {
    if (x <= 0) return 0;
    if (y <= 0) return 1 << 20;
    return static_cast<i32>(pow2(clampi(log2u(static_cast<u32>(x)) - log2u(static_cast<u32>(y)) + 10 * octave, 0, 30 * octave)));
}
// The direction of (dx, dy) on screen as a turn (Q10 unit vector), found
// by halving the angle on the cross product's sign: no divide, no root.
inline Turn direction(i32 dx, i32 dy) {
    i32 lo = 0;   // 65536 a turn
    // the half-plane first, then the angle within it
    i32 x = dx, y = dy;
    if (y < 0 || (y == 0 && x < 0)) { x = -x; y = -y; lo = 32768; }
    i32 a = 0;
    for (i32 b = 16384; b; b >>= 1) {
        const i32 c = cs(a + b), s = sn(a + b);
        // is (x, y) at or beyond the angle a + b (counter to the screen's clockwise)?
        if ((c >> 4) * (y >> 6) - (s >> 4) * (x >> 6) >= 0 && a + b <= 32768) a += b;
    }
    const i32 t = a + lo;
    Turn r = {cs(t) >> 4, sn(t) >> 4};
    return r;
}

// ---------------------------------------------------------------- the projection
// World points in Q4 (16 a pixel) to screen points in Q10 px: isometric,
// x down to the right, y down to the left, z up.
struct Pt { i32 x, y; };
inline Pt project(i32 x, i32 y, i32 z) {
    Pt p;
    p.x = art::ox + (((x - y) * 14189) >> 8);   // cos 30, Q14
    p.y = art::oy + (x + y) * 32 - z * 64;
    return p;
}
// A point at radius r (Q4) in a level plane, at angle a about its centre as
// the screen sees it (65536 a turn, 0 screen right, clockwise): its offset
// from the centre in Q10 px. The circle is an ellipse sqrt(1.5) wide and
// sqrt(0.5) tall.
inline Pt ring(i32 r, i32 a) {
    Pt p;
    p.x = (((r * cs(a)) >> 4) * 1254) >> 14;
    p.y = (((r * sn(a)) >> 4) * 724) >> 14;
    return p;
}

// ---------------------------------------------------------------- the frame
const Turn upright = {unit, 0};
template <class F>
inline void put(F& f, u32 node, Pt p, i32 scale = unit, Turn t = upright) {
    place(f, node, p.x, p.y, scale, 0, keep);
    turn(f, node, t);
}
// A reshaping outline: its points (Q10 px about its origin) only when they change.
template <class F>
inline void reshape(F& f, u32 path, const Pt* pts, Pt origin, u32 shade_) {
    u32 key = 2166136261u;
    for (u32 i = 0; i < 64; ++i) {
        key = (key ^ static_cast<u32>(pts[i].x)) * 16777619u;
        key = (key ^ static_cast<u32>(pts[i].y)) * 16777619u;
    }
    if (!key) key = 1;
    if (f.paths[path].key != key) {
        for (u32 i = 0; i < 64; ++i) {
            f.paths[path].points[i].x = pts[i].x;
            f.paths[path].points[i].y = pts[i].y;
        }
        f.paths[path].key = key;
    }
    place(f, path, origin.x, origin.y, unit, shade_, stroke);
}
const u32 groove_shade = 1, bar_shade = 2, arm_shade = 3;

// ---------------------------------------------------------------- captions
const u32 cap = 1 + header_capacity;
inline void number(char* out, i32 v, const char* unit_) {
    char t[cap + 4];
    u32 n = 0;
    if (v < 0) { t[n++] = '-'; v = -v; }
    if (v > 9999) v = 9999;
    if (v >= 1000) t[n++] = static_cast<char>('0' + v / 1000);
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    if (v >= 10) t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_[i] && n + 1 < sizeof t; ++i) t[n++] = unit_[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// tenths as "4.5" with a unit
inline void tenths(char* out, i32 v, const char* unit_) {
    char t[cap + 4];
    u32 n = 0;
    if (v < 0) { t[n++] = '-'; v = -v; }
    if (v > 9999) v = 9999;
    if (v >= 1000) t[n++] = static_cast<char>('0' + v / 1000 % 10);
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_[i] && n + 1 < sizeof t; ++i) t[n++] = unit_[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// hundredths as "0.29" with a unit
inline void hundredths(char* out, i32 v, const char* unit_) {
    char t[cap + 4];
    u32 n = 0;
    v = clampi(v, 0, 999);
    t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_[i] && n + 1 < sizeof t; ++i) t[n++] = unit_[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// seconds from log2 (Q16) as "70ms", "0.29s" or "1.3s"
inline void seconds(char* out, i32 ls) {
    const u32 ms = (pow2(clampi(ls + L(1000.0) + 10 * octave, 0, 30 * octave)) + 512u) >> 10;
    if (ms < 100) number(out, static_cast<i32>(ms), "ms");
    else if (ms < 1000) hundredths(out, static_cast<i32>((ms + 5) / 10), "s");
    else tenths(out, static_cast<i32>((ms + 50) / 100), "s");
}
// the platter's speed in rpm, "45.0"
inline i32 rpm_tenths(i32 r) { return static_cast<i32>((pow2(rec::speed_log(r) + L(450.0) + 10 * octave) + 512u) >> 10); }
inline void caption(char* out, u32 slot, u32 m, i32 r) {
    switch (slot) {
    case rec::SPEED: tenths(out, rpm_tenths(r), ""); return;
    case rec::PICK: {
        char t[cap] = "bar 1";
        t[4] = static_cast<char>('0' + rec::bar(r));
        copy(out, header_capacity, t);
        return;
    }
    case rec::LIFT: seconds(out, rec::lift_log(m, r)); return;
    case rec::WEIGHT: tenths(out, static_cast<i32>((pow2(rec::weight_log(r) + L(10.0) + 10 * octave) + 512u) >> 10), "g"); return;
    case rec::CUE: {
        const i32 t = r < 64 ? -rec::cue_early_tenths(r) : rec::cue_late_tenths(m, r);
        const i32 ms = t < 0 ? -((-t + 5) / 10) : (t + 5) / 10;
        if (ms > 0) { char s[cap] = "+"; number(s + 1, ms, ""); copy(out, header_capacity, s); }
        else number(out, ms, "");
        return;
    }
    case rec::WARP: number(out, rec::warp_cents(r), "ct"); return;
    case rec::DRAG: {
        const i32 d = rec::drag_tenths(r);
        if (!d) number(out, 0, "st");
        else if (d >= 100) number(out, -((d + 5) / 10), "st");
        else tenths(out, -d, "st");
        return;
    }
    default: header_amount(out, header_capacity, r >= 127 ? 0x7fffffff : r << 24);
    }
}
const char* const names[rec::MODELS] = {"kick", "snare", "rim", "hat", "open", "ride", "crash", "tom", "cnga", "bngo", "tamb", "bell", "clap", "shake", "bass"};
// Univers Next Pro Light's advance widths (thousandths of an em) for printable
// ASCII: a value a little wider than its column is drawn a little smaller.
static const u16 advance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560,
    560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745,
    602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589,
    574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
inline i32 em(const char* t) {
    i32 w = 0;
    for (; *t; ++t) w += *t >= 32 && *t <= 126 ? advance[*t - 32] : 600;
    return w;
}
// a value's text slot scaled down so it fits its 53 px at 20 px
template <class F>
inline void fit(F& f, u32 slot) {
    const i32 w = em(f.texts[slot].chars) * 20;   // thousandths of a px at the bright row's size
    if (w > 53000) f.texts[slot].scale = (f.texts[slot].scale * (ratio(53000, w) - 2)) >> 10;   // a hair under, never over
}
// the pad controls' captions, as the engine gives them to the stock header
inline void pad_text(char* out, u32 field, i32 v) {
    if (v > 0) { char s[cap] = "+"; number(s + 1, v, field ? "dB" : ""); copy(out, header_capacity, s); }
    else number(out, v, field ? "dB" : "");
}
const char* const titles8[8] = {"speed", "pick", "lift", "weight", "cue ms", "wear", "warp", "drag"};

// ---------------------------------------------------------------- the hit's time
// The needle's time in a hit (Q10 s): its early lead and the lift, as the voice plays them.
struct Play {
    i32 lead, length, total;   // Q10 s
};
inline Play play_of(u32 m, const i32* r) {
    Play p;
    p.lead = (rec::cue_early_tenths(r[rec::CUE]) * 1024 + 5000) / 10000;
    p.length = static_cast<i32>(pow2(clampi(rec::lift_log(m, r[rec::LIFT]) + 10 * octave, 0, 20 * octave)));
    p.total = p.lead + p.length;
    return p;
}
// The record's spin in a hit: a whole number of turns, as many as its speed
// (drawn 1.6 times life, TUNE as a finer speed) turns in the hit, at least
// one, so it comes to rest where it was cued; and how long they take (Q10 s).
struct Spin {
    i32 turns, time;
};
inline Spin spin_of(const Play& play, i32 speed, i32 tune) {
    const i32 rate = clampi(rec::speed_log(speed) + ST(1) * tune + L(45.0 / 60.0 * 1.6), L(0.4), L(2.5));
    const i32 per = static_cast<i32>(pow2(rate + 10 * octave));   // turns a second, Q10: 410..2560
    Spin s;
    s.turns = clampi((per * play.total + (1 << 19)) >> 20, 1, 8);
    s.time = ratio(s.turns * 1024, per);
    return s;
}
// The record's turn (65536 a turn) at a time into its spin: at speed for
// the first half, then gliding to rest; whole turns at the end.
inline i32 spun(const Spin& s, i32 age) {
    if (age >= s.time) return 0;
    const i32 u = clampi(ratio(age, s.time), 0, 1024);
    const i32 k = u <= 512 ? u : u - (((u - 512) * (u - 512)) >> 10);   // 0..768
    return (s.turns * k * 256) / 3;
}
const i32 drop_time = 102, lift_time = 205;   // Q10 s: the head's drop at the hit and its lift
// the arm's outline: how many points each of its eight edges takes, 64 in all
const u8 runs[8] = {7, 23, 1, 1, 23, 7, 1, 1};
}
using namespace view;

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 24;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const u32 m = input.model < rec::MODELS ? input.model : 0;
    i32 r[8], t[8];
    for (u32 i = 0; i < 8; ++i) {
        const u32 v = input.slots[i] < 0 ? 0u : static_cast<u32>(input.slots[i]) >> 24;
        r[i] = static_cast<i32>(v > 127 ? 127 : v);
        t[i] = (r[i] * 8257) >> 10;   // Q10, 127 -> 1024
    }
    const i32 tune = clampi(input.pad[0], -16, 15);
    const bool still = input.reduced_motion != 0;

    // --- the hit: the selected key's notes only (the platform's serial is the key's)
    const i32 step = clampi(input.elapsed, 0, 1024);
    const bool baseline = !state.initialized || state.track != input.track;
    const bool fresh = !baseline && input.note_serial != state.serial;
    state.serial = input.note_serial;
    state.track = input.track;
    state.initialized = 1;
    if (baseline || still) state.age = 1 << 24;
    else if (fresh) state.age = 0;
    else state.age = clampi(state.age + step, 0, 1 << 24);
    const i32 age = state.age;

    const Play play = play_of(m, r);
    const Spin spin = spin_of(play, r[rec::SPEED], tune);
    const i32 rho = spun(spin, age);   // the record's turn, clockwise as the screen sees it
    // the head: down on the record from the hit until the hit's end, then lifting (Q10)
    const i32 held = play.total > drop_time ? play.total : drop_time;
    i32 down = 0;
    if (age < drop_time) down = ease_out(ratio(age, drop_time));
    else if (age < held) down = 1024;
    else if (age < held + lift_time) down = 1024 - ease(ratio(age - held, lift_time));

    // --- E1, the record: SPEED sizes it, 33 a 12-inch, 45 a 7-inch, 78 a 10-inch (Q4 radius)
    const i32 rs = r[rec::SPEED];
    const i32 R = rs <= 64 ? 704 - ((rs * 4506) >> 10) : 422 + (((rs - 64) * 2861) >> 10);
    const i32 scale = (R * 1024) / art::r_ref;
    const Pt centre = project(art::rx, art::ry, art::z_top);
    put(frame, art::record_edge, centre, scale);
    put(frame, art::record_top, centre, scale);
    put(frame, art::label, centre, scale);
    // the grooves' field and its eight bars, outermost first (Q4 radii)
    const i32 g_in = ((R * 369) >> 10) + 8, g_out = (R * 952) >> 10;
    const i32 bw = (g_out - g_in) >> 3;
    // E2, PICK: the picked bar, sliding across the eight bars (Q10, 0..7)
    const i32 c = (r[rec::PICK] * 7168) / 127;
    const i32 bc = g_out - (((2 * c + 1024) * bw) >> 11);

    // --- the stylus over the picked bar: where the arm's circle about the pivot meets it
    const i32 si = clampi(bc - art::stylus_lo, 0, (art::stylus_n - 1) * 16 - 1);
    const i32 e = si >> 4, f = si & 15;
    const i32 sx = art::stylus_x[e] + (((art::stylus_x[e + 1] - art::stylus_x[e]) * f) >> 4);
    const i32 sy = art::stylus_y[e] + (((art::stylus_y[e + 1] - art::stylus_y[e]) * f) >> 4);
    const i32 phi = art::stylus_phi[e] + (((art::stylus_phi[e + 1] - art::stylus_phi[e]) * f) >> 4);

    Pt pts[64];
    // --- paths 0 and 1, E2: the groove, two walls a half-pitch apart spiralling
    // in 8 px a turn from the label to the outer edge, in two halves (the outer
    // half path 0). WEAR makes each point wander by its own fixed amount; the
    // record's turn carries them round.
    {
        const i32 U = (g_out - g_in - 64) * 512;   // the walls' length in turns (65536 a turn)
        const i32 amp = (20 * t[rec::WEAR]) >> 10;   // up to 1.25 px
        for (u32 k = 0; k < 2; ++k) {
            const i32 ua = k ? 0 : U >> 1, ub = k ? U >> 1 : U;
            for (u32 i = 0; i < 32; ++i) {
                const i32 u = ua + ((ub - ua) * static_cast<i32>(i)) / 31;
                const i32 r1 = g_in + 64 + (u >> 9);
                const i32 a = u + rho;
                pts[i] = ring(r1 + ((amp * art::wander[k][0][i]) >> 7), a);
                pts[63 - i] = ring(r1 - 64 + ((amp * art::wander[k][1][i]) >> 7), a);
            }
            reshape(frame, k, pts, centre, groove_shade);
        }
    }
    // --- path 2, E2: the picked bar, a solid band: a thin ribbon round the
    // record stroked wide, its ends meeting under the stylus
    {
        const i32 lo = phi + 91, span = 65536 - 182;
        for (u32 i = 0; i < 32; ++i) {
            const i32 a = lo + (span * static_cast<i32>(i)) / 31;
            pts[i] = ring(bc + 5, a);
            pts[63 - i] = ring(bc - 5, a);
        }
        reshape(frame, 2, pts, centre, bar_shade);
    }
    // --- E1 page 2, CUE: the cue sticker on the rim, ahead of the stylus for an
    // early landing, behind it for a late one; it turns with the record
    {
        const i32 a = phi - (64 - r[rec::CUE]) * 171 + rho;
        const Pt o = ring(R - 48, a);
        Pt p = {centre.x + o.x, centre.y + o.y};
        put(frame, art::sticker, p);
    }

    // --- path 3, E3: the arm, from the counterweight's tail through the pivot
    // to the headshell, one outline of the bar's top and side. LIFT: parked
    // higher the sooner it lifts; WARP: the head rides the disc as it turns.
    {
        const i32 ux = ((sx - art::pivot_x) * 16384) / art::arm, uy = ((sy - art::pivot_y) * 16384) / art::arm;   // Q14
        i32 nx = -uy, ny = ux;   // across the arm, toward the viewer
        if (nx + ny < 0) { nx = -nx; ny = -ny; }
        const i32 warp = (64 * rec::warp_share(r[rec::WARP])) >> 16;   // the disc's rise at the stylus, up to 4 px (Q4)
        const i32 rest = 24 + ((112 * (1024 - t[rec::LIFT])) >> 10) + ((warp * (16384 + cs(0))) >> 15);
        const i32 ride = (warp * (16384 + cs(2 * rho))) >> 15;
        const i32 h = rest + (((ride - rest) * down) >> 10);   // the needle's tip over the record (Q4)
        const i32 zh = art::z_top + h + art::stylus + art::head_d;   // the arm's top at the head
        // a point of the arm: a along it from the pivot, b across, d under its top (Q4)
        struct Arm {
            i32 ux, uy, nx, ny, zh;
            Pt at(i32 a, i32 b, i32 d) const {
                const i32 x = art::pivot_x + ((a * ux + b * nx) >> 14), y = art::pivot_y + ((a * uy + b * ny) >> 14);
                const i32 z = (a > 0 ? art::pivot_z + ((zh - art::pivot_z) * a) / art::arm : art::pivot_z) - d;
                return project(x, y, z);
            }
        } arm = {ux, uy, nx, ny, zh};
        // the corners: the far top edge from the tail to the head, across the
        // head's end, down its side, the near side's foot back to the tail
        const i32 hw = art::arm_w / 2, len = art::arm, T = -art::tail, d = art::arm_t;
        const i32 ca[8] = {T, 0, len, len, len, 0, T, T}, cb[8] = {-hw, -hw, -hw, hw, hw, hw, hw, hw}, cd[8] = {0, 0, 0, 0, d, d, d, 0};
        Pt raw[64];
        u32 n = 0;
        for (u32 k = 0; k < 8; ++k) {
            const u32 j = (k + 1) & 7;
            for (u32 i = 0; i < runs[k]; ++i) {
                const i32 q = static_cast<i32>(i), s = static_cast<i32>(runs[k]);
                // runs of 1, 7 and 23 points: steps along the edge by a constant divide
                const i32 a = ca[k] + (s == 23 ? ((ca[j] - ca[k]) * q) / 23 : s == 7 ? ((ca[j] - ca[k]) * q) / 7 : 0);
                const i32 b = cb[k] + (s == 23 ? ((cb[j] - cb[k]) * q) / 23 : s == 7 ? ((cb[j] - cb[k]) * q) / 7 : 0);
                const i32 dd = cd[k] + (s == 23 ? ((cd[j] - cd[k]) * q) / 23 : s == 7 ? ((cd[j] - cd[k]) * q) / 7 : 0);
                raw[n++] = arm.at(a, b, dd);
            }
        }
        // about the middle of tail-end and head; starting along the tail's top,
        // so the closing edge is a long one
        const Pt t0 = arm.at(T, 0, 0), t1 = arm.at(len, 0, 0);
        const Pt mid = {(t0.x + t1.x) >> 1, (t0.y + t1.y) >> 1};
        for (u32 i = 0; i < 64; ++i) {
            pts[i].x = raw[(i + 3) & 63].x - mid.x;
            pts[i].y = raw[(i + 3) & 63].y - mid.y;
        }
        reshape(frame, 3, pts, mid, arm_shade);
        // the headshell rides the arm's end, turned with it; the needle hangs under it
        const Pt hp = arm.at(len, 0, art::head_d);
        const Turn dir = direction(((ux - uy) * 14189) >> 14, (ux + uy) >> 1);
        const Turn by = {(dir.cos * art::head_cos + dir.sin * art::head_sin) >> 10, (dir.sin * art::head_cos - dir.cos * art::head_sin) >> 10};
        put(frame, art::head, hp, unit, by);
        put(frame, art::needle, hp);
        // E4, WEIGHT: the counterweight slides along the tail, nearer the pivot the heavier
        const i32 w = 304 - ((176 * t[rec::WEIGHT]) >> 10);
        const Pt cw = arm.at(-w, 0, 0);
        put(frame, art::weight_side, cw);
        put(frame, art::weight_top, cw);
    }
    // --- E4 page 2, DRAG: the dust brush comes straight down its post onto the record
    {
        Pt b = project(art::post_x, art::post_y, art::brush_z);
        b.y += ((art::brush_drop * rec::drag_share(r[rec::DRAG])) >> 16) * 64;
        put(frame, art::brush_side, b);
        put(frame, art::brush_arm, b);
        put(frame, art::brush_top, b);
    }

    // --- the header: both rows, the edited one bright; while Shift is held
    // both rows show it: the key's mixer channel (MACHINE, CHOKE, PAN, LEVEL)
    // over its pad controls and sends (TUNE, TONE, FX I, FX II)
    const char* titles[8];
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = titles8[i];
        caption(values[i], i, m, r[i]);
    }
    if (input.shift) {
        static const char* const chokes[4] = {"none", "1", "2", "3"};
        char pan[cap], level[cap], sends[2][cap], tune_[cap], tone_[cap];
        number(pan, input.pan, "");
        number(level, input.level, "dB");
        for (u32 k = 0; k < 2; ++k) {
            const i32 code = input.pad[2 + k];
            if (code > 0) number(sends[k], ((code > 7 ? 7 : code) - 1) * 100 / 6 + (code == 3 || code == 6 ? 1 : 0), "");
            else copy(sends[k], cap, "TRK");
        }
        pad_text(tune_, 0, tune);
        pad_text(tone_, 1, clampi(input.pad[1], -8, 7));
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"TUNE", "TONE", "FX I", "FX II"}};
        const char* const shift_values[2][4] = {{names[m], chokes[input.choices[1] & 3u], pan, level}, {tune_, tone_, sends[0], sends[1]}};
        for (u32 line = 0; line < 2; ++line)
            for (u32 c4 = 0; c4 < 4; ++c4) {
                titles[4 * line + c4] = shift_titles[line][c4];
                copy(values[4 * line + c4], header_capacity, shift_values[line][c4]);
            }
    }
    page_header(state.header, input, frame, titles, values, art::header_node);
    for (u32 slot = 4; slot < 8; ++slot) fit(frame, slot);
    for (u32 slot = 12; slot < 16; ++slot) fit(frame, slot);
}
}
