#include "display.h"
#include "dsp.h"

// DUST's screen: a portable record player with a soul 45 on it, seen from
// the front and above. Its plinth, platter, record and fittings are artwork;
// the record's rim, the band of the selected key's hit, the spider adapter
// and the tonearm are the four reshaping outlines, drawn in the projection
// make_panel.py writes into art.h; the rest are parts placed from the key's
// controls. A hit plays the record: it turns by the hit's length at its
// speed while the groove's band runs under the stylus, then the arm lifts,
// the record spins back to the cue and the arm settles.
namespace dust {
namespace view {
using namespace opxy::draw;

// ---------------------------------------------------------------- arithmetic
inline i32 iabs(i32 v) { return v < 0 ? -v : v; }
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
// sin and cos of a turn (65536 a turn), Q14
inline i32 sn(i32 turn) { return sine(static_cast<u32>(turn) << 16) >> 17; }
inline i32 cs(i32 turn) { return sn(turn + 16384); }
// a + (b - a) t, t a Q10 unit, |b - a| < 2^21
inline i32 lerp(i32 a, i32 b, i32 t) { return a + (((b - a) >> 2) * t >> 8); }
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
// World points in sixteenths of a millimetre to screen points in Q10 px.
struct Pt { i32 x, y; };
inline Pt project(i32 x, i32 y, i32 z) {
    Pt p;
    p.x = (art::cx >> 6) + ((art::ax * x + art::ay * y) >> 10);
    p.y = (art::cy >> 6) - ((art::bx * x + art::by * y + art::bz * z) >> 10);
    return p;
}
// a point at radius r (mm Q4) and angle a (a turn 65536) in the record's plane at height z
inline Pt on_circle(i32 r, i32 a, i32 z) { return project((r * cs(a)) >> 14, (r * sn(a)) >> 14, z); }

// ---------------------------------------------------------------- the frame
template <class F>
inline void put(F& f, u32 node, i32 x, i32 y, i32 scale, u32 shade_, Turn t) {
    place(f, node, x, y, clampi(scale, 64, 2 * unit), shade_, shade_ ? stroke : keep);
    turn(f, node, t);
}
const Turn upright = {unit, 0};
template <class F>
inline void put_shaded(F& f, u32 node, i32 x, i32 y, i32 shade_, Turn t = upright) {
    if (shade_ <= 0) { hide(f, node); return; }
    put(f, node, x, y, unit, static_cast<u32>(shade_ > 8 ? 8 : shade_), t);
}

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
const char* const names[rec::MODELS] = {"kick", "snare", "rim", "hat", "open", "ride", "crash", "tom", "conga", "bongo", "tamb", "bell", "clap", "shake", "bass"};
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
// The needle's time in a hit (Q10 s): its early lead and the lift.
struct Play {
    i32 lead, length, total;   // Q10 s
    i32 turns;                 // the record's turn over the whole play, 65536 a turn
    i32 band, gap;             // the band's length and where it starts after the stylus (turns)
};
inline Play play_of(u32 m, const i32* r, i32 tune) {
    Play p;
    p.lead = (rec::cue_early_tenths(r[rec::CUE]) * 1024 + 5000) / 10000;
    p.length = static_cast<i32>(pow2(clampi(rec::lift_log(m, r[rec::LIFT]) + 10 * octave, 0, 20 * octave)));
    p.total = p.lead + p.length;
    // turns a second: rpm / 60, the key's TUNE as a finer speed
    const i32 lw = rec::speed_log(r[rec::SPEED]) + ST(1) * clampi(tune, -16, 15) + L(0.75);
    const i32 per = static_cast<i32>(pow2(lw + 16 * octave));   // Q16 turns a second
    p.turns = (per >> 4) * (p.total >> 2) >> 4;
    p.band = (per >> 4) * (p.length >> 2) >> 4;
    if (p.band > 58982) p.band = 58982;   // at most 0.9 of a turn
    const i32 late = (rec::cue_late_tenths(m, r[rec::CUE]) * 1024 + 5000) / 10000;
    // (drawn four times as far as the groove runs, so a few milliseconds read)
    p.gap = 4 * (((per >> 4) * p.lead >> 6) - ((per >> 4) * late >> 6));
    // a drag slows the platter as it plays
    p.turns -= (p.turns >> 7) * (rec::drag_tenths(r[rec::DRAG]) * 8 / 10) >> 3;
    return p;
}
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
    const i32 tune = clampi(input.pad[0], -16, 15), tone = clampi(input.pad[1], -8, 7);
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

    const Play play = play_of(m, r, tune);
    // the record's turn now (65536 a turn, clockwise seen from above, so the
    // angle falls), the arm's lift (Q10) and whether anything moves
    i32 turned = 0, lift = 0;
    const i32 back_at = play.total + 205, back_len = 307, end = play.total + 563;
    if (age < play.total) {
        turned = (play.turns >> 4) * (ease_out(ratio(age, play.total)) >> 2) >> 4;
    } else if (age < end) {
        lift = age < play.total + 123 ? ease_out(ratio(age - play.total, 123)) : age < back_at + back_len ? 1024 : 1024 - ease(ratio(age - back_at - back_len, end - back_at - back_len));
        const i32 back = age < back_at ? 1024 : 1024 - ease(ratio(age - back_at, back_len));
        turned = (play.turns >> 4) * (back >> 2) >> 4;
    }
    const i32 rho = -turned;   // the record's angle

    // --- the stylus: PICK swings the arm across the record, bar 1 outermost
    const i32 pick = t[rec::PICK];
    const i32 rad = art::r_outer + (((art::r_inner - art::r_outer) * pick) >> 10);
    const i32 at16 = pick * 16;
    const u32 ai = static_cast<u32>(at16 >> 10) < 16 ? static_cast<u32>(at16 >> 10) : 15;
    const i32 af = at16 - static_cast<i32>(ai << 10);
    const i32 theta = art::stylus_angle[ai] + (((art::stylus_angle[ai + 1] - art::stylus_angle[ai]) * af) >> 10);
    // WARP: the disc rises and falls twice a turn, its rise turning with it (mm Q4)
    const i32 warp = (rec::warp_share(r[rec::WARP]) * 7 * 16) >> 16;   // up to 7 mm, drawn larger than life
    const i32 warp_phase = 6000;
    const i32 ride = (warp * sn(2 * (theta - rho) + warp_phase)) >> 14;

    // --- path 0: the record's rim, at half size, rising and falling with the warp
    {
        u32 key = 0x9e3779b9u ^ static_cast<u32>(warp * 7919);
        if (warp) key = (key ^ static_cast<u32>(rho)) * 16777619u;
        if (!key) key = 1;
        if (frame.paths[0].key != key) {
            for (u32 i = 0; i < 64; ++i) {
                const i32 a = static_cast<i32>(i) * 1024 + 512;
                const i32 z = warp ? (warp * sn(2 * (a - rho) + warp_phase)) >> 14 : 0;
                const Pt p = on_circle(art::r_record, a, z);
                frame.paths[0].points[i].x = (p.x - (art::cx >> 6)) / 2;
                frame.paths[0].points[i].y = (p.y - (art::cy >> 6)) / 2;
            }
            frame.paths[0].key = key;
        }
        place(frame, 0, art::cx >> 6, art::cy >> 6, 2 * unit, 13, stroke);
    }

    // --- path 1: the band of the hit's groove, ahead of the stylus, at half size
    {
        // its start and end along the groove (turns; the record's turn carries it
        // to the stylus, which takes it in)
        i32 lo = theta + play.gap - turned;
        const i32 hi = lo + play.band;
        if (turned > 0 && lo < theta) lo = theta;
        const i32 len = hi - lo;
        const i32 minimum = 2900;   // about 16 degrees: shorter, it fades out instead
        const i32 shade_ = len <= 0 ? 0 : len >= minimum ? 8 : (len * 8) / minimum;
        // TONE: the band's sheen, from a dull grey at -8 to white at +7
        const i32 sheen = clampi(shade_ - 8 + 3 + ((tone + 8) * 4) / 15, 0, 7);
        if (sheen <= 0 || shade_ <= 0) {
            hide(frame, 1);
            if (!frame.paths[1].key) frame.paths[1].key = 1;
        } else {
            const i32 span_ = len >= minimum ? len : minimum;
            const i32 a0 = hi - span_;
            u32 key = 0x85ebca6bu ^ static_cast<u32>(a0 * 31 + span_ * 131 + rad + warp * 7 + (warp ? rho * 3 : 0));
            if (!key) key = 1;
            if (frame.paths[1].key != key) {
                for (u32 i = 0; i < 32; ++i) {
                    const i32 a = a0 + ((span_ >> 5) * static_cast<i32>(i) * 32 + 16) / 31;
                    const i32 z = warp ? (warp * sn(2 * (a - rho) + warp_phase)) >> 14 : 0;
                    const Pt o = on_circle(rad + 30, a, z), n = on_circle(rad - 30, a, z);
                    frame.paths[1].points[i].x = (o.x - (art::cx >> 6)) / 2;
                    frame.paths[1].points[i].y = (o.y - (art::cy >> 6)) / 2;
                    frame.paths[1].points[63 - i].x = (n.x - (art::cx >> 6)) / 2;
                    frame.paths[1].points[63 - i].y = (n.y - (art::cy >> 6)) / 2;
                }
                frame.paths[1].key = key;
            }
            place(frame, 1, art::cx >> 6, art::cy >> 6, 2 * unit, static_cast<u32>(8 + sheen), stroke);
        }
    }

    // --- path 2: the spider adapter in the big hole, turning with the record
    {
        u32 key = 0x27d4eb2fu ^ static_cast<u32>(rho);
        if (!key) key = 1;
        if (frame.paths[2].key != key) {
            // three arms, 21 points each and one more where the first begins
            u32 n = 0;
            for (u32 arm_ = 0; arm_ < 3; ++arm_) {
                const i32 base = rho + static_cast<i32>(arm_) * 21845 + 4000;
                // the valley between arms on the hub, out along one side, round the tip, back
                static const i16 da[22] = {-10923, -9300, -7600, -6000, -4700, -3300, -2400, -1900, -1400, -900, -400, 0, 400, 900, 1400, 1900, 2400, 3300, 4700, 6000, 7600, 9300};
                static const i16 rr[22] = {150, 150, 150, 152, 160, 196, 246, 280, 292, 296, 297, 297, 296, 292, 280, 246, 196, 160, 152, 150, 150, 150};
                for (u32 k = arm_ ? 1 : 0; k < 22 && n < 64; ++k) {
                    const Pt p = on_circle(rr[k], base + da[k], 8);
                    frame.paths[2].points[n].x = p.x - (art::cx >> 6);
                    frame.paths[2].points[n].y = p.y - (art::cy >> 6);
                    ++n;
                }
            }
            frame.paths[2].key = key;
        }
        place(frame, 2, art::cx >> 6, art::cy >> 6, unit, 6, stroke);
    }

    // --- the record's marks: the label's printed bar, scratches and dust (WEAR)
    {
        const Pt p = on_circle(33 * 16, rho + 12000, 0);
        const Pt q = on_circle(33 * 16, rho + 12000 - 1024, 0);
        put(frame, art::label_mark, p.x, p.y, unit, 6, direction(p.x - q.x, p.y - q.y));
        static const i16 sr[6] = {1250, 1010, 1180, 900, 1300, 1080};
        static const i32 sa[6] = {2000, 21000, 33000, 45000, 54000, 11000};
        static const i16 dr[8] = {1270, 1100, 960, 1210, 1350, 880, 1160, 1020};
        static const i32 da_[8] = {7000, 15000, 26000, 30500, 40000, 48000, 60000, 51000};
        const i32 wear = t[rec::WEAR];
        for (u32 k = 0; k < 6; ++k) {
            const i32 s = ((wear - static_cast<i32>(k) * 150) * 8) / 200;
            const Pt a = on_circle(sr[k], rho + sa[k], 0), b = on_circle(sr[k], rho + sa[k] - 1024, 0);
            put_shaded(frame, art::scratch_0 + k, a.x, a.y, clampi(s, 0, 7), direction(a.x - b.x, a.y - b.y));
        }
        for (u32 k = 0; k < 8; ++k) {
            const i32 s = ((wear - 60 - static_cast<i32>(k) * 110) * 8) / 160;
            const Pt a = on_circle(dr[k], rho + da_[k], 0);
            put_shaded(frame, art::speck_0 + k, a.x, a.y, clampi(s, 0, 7));
        }
    }

    // --- path 3: the tonearm, from the counterweight's stub through the pivot
    // to the headshell, its finger lift and the stylus: drawn at half size
    // about the arm's middle. It rides the warp and lifts at the hit's end.
    Pt pivot, tip;
    {
        const i32 sx = (rad * cs(theta)) >> 14, sy = (rad * sn(theta)) >> 14;   // the stylus, mm Q4
        const i32 dx = sx - art::pivot_x, dy = sy - art::pivot_y;
        // the arm's unit direction in the plane, Q14 (its length is the arm's, ARM)
        const i32 ux = (dx * 16384) / art::arm, uy = (dy * 16384) / art::arm;
        const i32 lifted = ((lift * 6 * 16) >> 10) + ride;   // mm Q4 at the stylus
        // a point along the arm (d mm Q4 from the pivot, s across it) at the arm's height
        struct A {
            i32 ux, uy, lifted;
            Pt at(i32 d, i32 s, i32 z) const {
                const i32 x = art::pivot_x + ((ux * d - uy * s) >> 14), y = art::pivot_y + ((uy * d + ux * s) >> 14);
                return project(x, y, z + (lifted * d) / art::arm);
            }
        } arm = {ux, uy, lifted};
        pivot = project(art::pivot_x, art::pivot_y, art::pivot_z);
        tip = arm.at(art::arm, 0, 0);
        // the headshell turns 22 degrees inward from the tube (the offset)
        const i32 hx = (ux * 15191 + uy * 6137) >> 14, hy = (uy * 15191 - ux * 6137) >> 14;
        struct H {
            i32 hx, hy;
            const A* a;
            Pt at(i32 d, i32 s, i32 z) const {
                // d along the headshell from the stylus (negative: back), s across
                const i32 x = ((hx * d - hy * s) >> 14), y = ((hy * d + hx * s) >> 14);
                const i32 ax_ = a->ux, ay_ = a->uy;
                // back into the arm's own frame, so A::at adds its lift
                const i32 along = ((ax_ * x + ay_ * y) >> 14), across = ((-ay_ * x + ax_ * y) >> 14);
                return a->at(art::arm + along, across, z);
            }
        } head = {hx, hy, &arm};
        Pt pts[64];
        u32 n = 0;
        const i32 tube_z = art::pivot_z;
        // The tube runs from the pivot to the headshell's root, 23 mm behind
        // the stylus along the headshell and so 8.6 mm to the outer side of
        // the pivot-to-stylus line: in the arm's frame its direction is
        // (16355, 1013) Q14 and it is 2233 (mm Q4) long.
        struct T {
            const A* a;
            Pt at(i32 q, i32 z) const { return a->at((q * 16355) >> 14, (q * 1013) >> 14, z); }
        } tube = {&arm};
        const i32 root = 2233;
        const Pt t0 = tube.at(-art::stub, tube_z), t1 = tube.at(root, tube_z - 48);
        const Turn d = direction(t1.x - t0.x, t1.y - t0.y);
        const i32 ox = (-d.sin * 1434) >> 10, oy = (d.cos * 1434) >> 10;   // 1.4 px across, Q10
        // left edge: from the stub's end to the headshell, 39 points; the tube
        // falls 3 mm from the pivot to the headshell
        for (u32 k = 0; k < 39; ++k) {
            const i32 at = -art::stub + ((root + art::stub) * static_cast<i32>(k)) / 38;
            const Pt c = tube.at(at, tube_z - (at > 0 ? (at * 48) / root : 0));
            pts[n].x = c.x + ox; pts[n].y = c.y + oy; ++n;
        }
        // the headshell: a plate from its root to the stylus, its finger lift
        // curling up from its outer side; traced from the side the left edge
        // ends on
        static const i16 hd[14] = {-368, -256, -112, -88, -64, -24, 0, -24, 48, 96, 96, 48, -256, -368};
        static const i16 hs[14] = {88, 104, 120, 240, 340, 352, 300, 140, 120, 64, -64, -120, -104, -88};
        static const i16 hz[14] = {112, 112, 112, 116, 122, 124, 120, 112, 112, 112, 112, 112, 112, 112};
        const Pt first = head.at(hd[0], hs[0], hz[0]), last = head.at(hd[13], -hs[13] * 0 + hs[13], hz[13]);
        const i32 ex = pts[n - 1].x, ey = pts[n - 1].y;
        const bool forward = iabs(first.x - ex) + iabs(first.y - ey) <= iabs(last.x - ex) + iabs(last.y - ey);
        for (u32 k = 0; k < 14; ++k) {
            const u32 j = forward ? k : 13 - k;
            pts[n] = head.at(hd[j], hs[j], hz[j]);
            ++n;
        }
        // right edge back to the stub's end, 9 points
        for (u32 k = 0; k < 9; ++k) {
            const i32 at = root - ((root + art::stub) * static_cast<i32>(k)) / 8;
            const Pt c = tube.at(at, tube_z - (at > 0 ? (at * 48) / root : 0));
            pts[n].x = c.x - ox; pts[n].y = c.y - oy; ++n;
        }
        // the stub's round end
        const Pt e = tube.at(-art::stub - 24, tube_z);
        pts[n].x = e.x - ox / 2; pts[n].y = e.y - oy / 2; ++n;
        pts[n].x = e.x + ox / 2; pts[n].y = e.y + oy / 2; ++n;
        // rotate so the long left edge closes the outline
        const Pt mid = {(t0.x + tip.x) / 2, (t0.y + tip.y) / 2};
        u32 key = 0xc2b2ae35u ^ static_cast<u32>(theta * 131 + lifted * 7);
        if (!key) key = 1;
        if (frame.paths[3].key != key) {
            for (u32 i = 0; i < 64; ++i) {
                const Pt& p = pts[(i + 3) & 63];
                frame.paths[3].points[i].x = (p.x - mid.x) / 2;
                frame.paths[3].points[i].y = (p.y - mid.y) / 2;
            }
            frame.paths[3].key = key;
        }
        place(frame, 3, mid.x, mid.y, 2 * unit, 15, stroke);
        put(frame, art::tip, tip.x, tip.y, unit, lift > 512 ? 5 : 8, upright);
        // WEIGHT: the counterweight slides along its stub, nearer the pivot for a heavier arm
        const i32 w = 260 + (((800 - 260) * (1024 - t[rec::WEIGHT])) >> 10);   // mm Q4 behind the pivot
        const Pt c = tube.at(-w, tube_z);
        put(frame, art::weight, c.x, c.y, unit, 6, d);
        put(frame, art::weight_ring, c.x, c.y, unit, 4, d);
    }

    // --- the cue lever: LIFT, raised for an early lift, down for a long play
    {
        const Pt c = project(art::cue_x, art::cue_y, art::cue_z + 8 * 16);
        const i32 deg = -620 + ((t[rec::LIFT] * 560) >> 10) - ((lift * 200) >> 10);   // tenths of a degree
        put(frame, art::cue_lever, c.x, c.y, unit, 6, rotation(deg));
        put(frame, art::cue_knob, c.x, c.y, unit, 7, rotation(deg));
    }
    // --- the speed selector: SPEED, its lever from 33 through 45 to 78
    {
        const Pt c = project(art::speed_x, art::speed_y, art::speed_z);
        const i32 deg = -900 + ((r[rec::SPEED] - 64) * 7);
        put(frame, art::speed_lever, c.x, c.y, unit, 8, rotation(deg));
        put(frame, art::speed_hub, c.x, c.y, unit, 6, upright);
    }
    // --- the dust brush: DRAG lowers it onto the record and bends its bristles
    {
        const Pt pv = project(art::brush_x, art::brush_y, art::brush_z);
        const i32 drag = t[rec::DRAG];
        const i32 down = drag < 200 ? drag * 5 : 1024;          // Q10: touching from a fifth of the way
        const i32 hz_ = ((1024 - down) * 11 * 16) >> 10;        // its height over the record, mm Q4
        const Pt h = project(art::brush_at_x, art::brush_at_y, hz_ + 6 * 16);
        const i32 ex = h.x - pv.x, ey = h.y - pv.y;
        const Turn d = direction(ex, ey);
        // the arm's length on screen against its 60 px drawing
        const i32 len = ((iabs(ex) > iabs(ey) ? iabs(ex) + (iabs(ey) >> 2) : iabs(ey) + (iabs(ex) >> 2)));
        const i32 sc = ratio(len, 60 << 10);
        put(frame, art::brush_arm, pv.x, pv.y, sc, 6, d);
        put(frame, art::brush_head, h.x, h.y, unit, 6, upright);
        const i32 bend = drag > 200 ? ((drag - 200) * 380) / 824 : 0;   // tenths of a degree
        put(frame, art::brush_tuft, h.x, h.y + 3 * unit, unit, 5, rotation(bend));
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
        pad_text(tone_, 1, tone);
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"TUNE", "TONE", "FX I", "FX II"}};
        const char* const shift_values[2][4] = {{names[m], chokes[input.choices[1] & 3u], pan, level}, {tune_, tone_, sends[0], sends[1]}};
        for (u32 line = 0; line < 2; ++line)
            for (u32 c = 0; c < 4; ++c) {
                titles[4 * line + c] = shift_titles[line][c];
                copy(values[4 * line + c], header_capacity, shift_values[line][c]);
            }
    }
    page_header(state.header, input, frame, titles, values, art::header_node);
    for (u32 slot = 4; slot < 8; ++slot) fit(frame, slot);
    for (u32 slot = 12; slot < 16; ++slot) fit(frame, slot);
}
}
