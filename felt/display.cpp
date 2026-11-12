#include "display.h"

// FELT's screen: a textbook plate of Cage's prepared piano, isometric. Three
// unison strings run over a near-black iron plate, from the fixed far bridge
// over the near bridge to the hitch rail. Each encoder owns one part and its
// grey, whatever the model: encoder 1 the near bridge (hollow, 191919 with a
// CDCDCD edge), which TUNE slides along the strings; encoder 2 the damper
// (4B4B4B); encoder 3 the striker (969696), from beneath or beside the
// strings; encoder 4 the preparation wedged between them (CDCDCD). A model's
// second-row control keeps its encoder's grey and acts on that part or a
// detail of it. A hit of the selected key strikes and returns, lifts the
// damper until DECAY's time is up, and sets the strings shimmering. The
// strings and the bead chain or the bow's thread are the four reshaping
// outlines; every other part is a box of three faces or a flat piece, placed
// from the key's controls on the ranges the sound uses (shape.h).
namespace felt {
namespace view {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// ---------------------------------------------------------------- arithmetic
// sin of a phase (65536 a turn), Q14.
static const opxy::i16 quarter[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270,
    6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585,
    11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137,
    15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
inline i32 sn(i32 phase) {
    const u32 p = static_cast<u32>(phase) & 0xffffu;
    const u32 q = p >> 14, r = p & 0x3fffu;
    const u32 i = (q & 1u ? 0x4000u - r : r);
    const u32 k = i >> 8, f = i & 0xffu;
    const i32 a = quarter[k], b = quarter[k < 64 ? k + 1 : 64];
    const i32 v = a + (((b - a) * static_cast<i32>(f)) >> 8);
    return q >= 2 ? -v : v;
}
inline i32 cs(i32 phase) { return sn(phase + 16384); }
// 2^(l / 65536) in Q16, for l from -16 to 14 octaves.
static const u32 octave_steps[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682, 96785, 101070,
                                     105545, 110218, 115098, 120194, 125515, 131072};
inline u32 pow2v(i32 l) {
    if (l < -16 * 65536) return 0;
    if (l > 14 * 65536) l = 14 * 65536;
    const i32 n = l >= 0 ? l / 65536 : -((-l + 65535) / 65536);   // floor
    const u32 f = static_cast<u32>(l - n * 65536);
    const u32 k = f >> 12, r = f & 0xfffu;
    const u32 m = octave_steps[k] + (((octave_steps[k + 1] - octave_steps[k]) * r) >> 12);
    return n >= 0 ? m << n : m >> -n;
}
// A time given as log2 seconds (Q16), in Q10 seconds.
inline i32 secs(i32 l) { return static_cast<i32>(pow2v(l) >> 6); }
// exp(-a / b) in Q15, for a and b in the same units (b > 0).
inline i32 fall(i32 a, i32 b) {
    if (a <= 0) return 32768;
    if (b <= 0 || a > 16 * b || a >= (1 << 21)) return 0;
    const i32 x = ((a * 1024) / b) * 64;                     // a / b, Q16
    const i32 l = x + (x >> 1) - (x >> 4) - (x >> 7);        // times log2 e
    return static_cast<i32>(pow2v(-l) >> 1);
}
inline i32 iabs(i32 v) { return v < 0 ? -v : v; }
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 lerp(i32 a, i32 b, i32 t) { return a + (((b - a) / 4) * clampi(t, 0, 1024)) / 256; }   // t Q10; |b - a| < 2^21
// Rounds toward zero to a step of q, so a motion settles exactly onto its rest.
inline i32 toward0(i32 v, i32 q) { return v >= 0 ? (v / q) * q : -((-v / q) * q); }
// A fraction t (Q10) of v, for |v| < 2^21.
inline i32 part(i32 v, i32 t) { return ((v / 32) * t) / 32; }

// ---------------------------------------------------------------- the iso plate
// A point on the plate (x along the strings, y across them, z up; Q10 iso
// units, a pixel along an axis) on screen, in Q10 px: 30-degree edges.
inline i32 iso_x(i32 x, i32 y) { return art::origin_x + ((x - y) * 887) / 1024; }
inline i32 iso_y(i32 x, i32 y, i32 z) { return art::origin_y + (x + y) / 2 - z; }
// The palette: the grey code (each encoder's base grey, its right face one
// entry on), the strings, neutral wood, the encoders' hairlines.
enum Code : u32 { black = 0, e1 = 1, e2 = 3, e3 = 5, e4 = 7, string_mid = 9, string_out = 10, wood = 11,
                  hair1 = 13, hair2 = 14, thread = 15 };
template <class F>
inline void at(F& f, u32 node, i32 x, i32 y, i32 z, i32 scale, u32 shade) {
    place(f, node, iso_x(x, y), iso_y(x, y, z), clampi(scale, 64, 2 * unit), shade, fill_and_stroke);
}
// A box: its left, right and top faces about one pivot, the right a ramp step darker.
template <class F>
inline void box(F& f, u32 first, i32 x, i32 y, i32 z, i32 scale, u32 code) {
    for (u32 k = 0; k < 3; ++k) at(f, first + k, x, y, z, scale, k == 1 ? code + 1 : code);
}
template <class F>
inline void park(F& f, u32 first, u32 count) {
    for (u32 k = 0; k < count; ++k) hide(f, first + k);
}

// ---------------------------------------------------------------- captions
const u32 cap = header_capacity + 1;
inline void number(char* out, i32 v, const char* unit_text, bool sign = false) {
    char t[16];
    u32 n = 0;
    if (v < 0) { t[n++] = '-'; v = -v; }
    else if (sign && v > 0) t[n++] = '+';
    if (v > 9999) v = 9999;
    if (v >= 1000) t[n++] = static_cast<char>('0' + v / 1000);
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    if (v >= 10) t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_text[i] && n < 15; ++i) t[n++] = unit_text[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// tenths as "1.3" with a unit
inline void tenths(char* out, i32 v, const char* unit_text) {
    char t[16];
    u32 n = 0;
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_text[i] && n < 15; ++i) t[n++] = unit_text[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// hundredths as "0.29" with a unit
inline void hundredths(char* out, i32 v, const char* unit_text) {
    char t[16];
    u32 n = 0;
    t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_text[i] && n < 15; ++i) t[n++] = unit_text[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
inline i32 lmix(i32 la, i32 lb, i32 t) { return la + (((lb - la) / 2) * t) / 512; }   // t Q10; log2 Q16
enum Kind : unsigned char { amount, note, hz, ms, count, grams, cm, semis, bend };
struct Caption { Kind kind; i32 la, lb; };
inline void caption(char* out, const Caption& c, i32 t, i32 tune) {   // t: Q10 unit
    switch (c.kind) {
    case note: {
        const i32 l = lmix(c.la, c.lb, t) - L(440.0);
        const i32 st = 69 * 256 + ((l / 16) * 3072) / 4096 + tune * 256;
        const i32 key = (st + 128) / 256;
        static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const i32 k = key < 0 ? 0 : key;
        char s[8];
        u32 n = 0;
        for (const char* p = names[k % 12]; *p; ++p) s[n++] = *p;
        const i32 oct = k / 12 - 1;
        if (oct < 0) { s[n++] = '-'; s[n++] = static_cast<char>('0' - oct); } else s[n++] = static_cast<char>('0' + oct);
        s[n] = 0;
        copy(out, header_capacity, s);
        return;
    }
    case hz: {
        const u32 f = pow2v(lmix(c.la, c.lb, t) + tune * 5461);
        const u32 h = (f + 32768) >> 16;
        if (h >= 1000) tenths(out, static_cast<i32>((h + 50) / 100), "k");
        else number(out, static_cast<i32>(h), "");
        return;
    }
    case ms: {
        const u32 s = pow2v(lmix(c.la, c.lb, t));   // seconds, Q16
        const u32 m = (s * 1000u + 32768u) >> 16;
        if (m < 100) number(out, static_cast<i32>(m), "ms");
        else if (m < 1000) hundredths(out, static_cast<i32>((m + 5) / 10), "s");
        else tenths(out, static_cast<i32>((m + 50) / 100), "s");
        return;
    }
    case count: number(out, c.la + (t * (c.lb - c.la) + 512) / 1024, ""); return;
    case grams: {
        const u32 g = (pow2v(lmix(c.la, c.lb, t)) * 10u + 32768u) >> 16;   // tenths of a gram
        if (g < 100) tenths(out, static_cast<i32>(g), "g");
        else number(out, static_cast<i32>((g + 5) / 10), "g");
        return;
    }
    case cm: number(out, c.la + (t * (c.lb - c.la) + 512) / 1024, "cm"); return;
    case semis: number(out, (t * c.lb + 512) / 1024, "st"); return;
    case bend: number(out, ((t - 512) * c.lb * 2 + (t >= 512 ? 512 : -512)) / 1024, "st", true); return;
    default: header_amount(out, header_capacity, t >= 1024 ? 0x7fffffff : t * 2097152);
    }
}
const Caption A = {amount, 0, 0};
#define NOTE(m) {note, shape::pitch[m].a, shape::pitch[m].b}
#define HZ(m) {hz, shape::pitch[m].a, shape::pitch[m].b}
#define DECAYS(m) {ms, shape::damper[m].a, shape::damper[m].b}
const Caption captions[model_count][8] = {
    {NOTE(M::THUD), DECAYS(M::THUD), A, A, {semis, 0, 24}, A, A, A},
    {NOTE(M::RATTLE), DECAYS(M::RATTLE), A, A, {cm, 2, 30}, A, A, A},
    {NOTE(M::TOM), DECAYS(M::TOM), A, A, {semis, 0, 24}, A, A, A},
    {HZ(M::LID), DECAYS(M::LID), {count, 1, 6}, {ms, shape::gap.a, shape::gap.b}, A, A, A, A},
    {NOTE(M::TACK), DECAYS(M::TACK), A, A, A, {cm, 2, 30}, A, A},
    {NOTE(M::BELL), DECAYS(M::BELL), A, {grams, L(0.5), L(8.0)}, {cm, 2, 30}, A, A, A},
    {HZ(M::BRUSH), DECAYS(M::BRUSH), {ms, shape::stroke.a, shape::stroke.b}, A, A, {cm, 2, 30}, A, A},
    {NOTE(M::PLANK), DECAYS(M::PLANK), {count, 2, 24}, A, {ms, shape::bloom.a, shape::bloom.b}, A, A, A},
    {HZ(M::CHAIN), DECAYS(M::CHAIN), {count, 3, 40}, {ms, shape::shake.a, shape::shake.b}, A, A, A, A},
    {NOTE(M::BOLT), DECAYS(M::BOLT), A, {grams, L(6.0), L(60.0)}, {cm, 2, 30}, A, A, A},
    {NOTE(M::PLUCK), DECAYS(M::PLUCK), A, {cm, 2, 30}, A, A, A, A},
    {NOTE(M::BOW), DECAYS(M::BOW), A, {ms, shape::draw.a, shape::draw.b}, {bend, 0, 12}, {cm, 2, 30}, A, A},
};
#undef NOTE
#undef HZ
#undef DECAYS
// as the Shift row shows them: lower case
const char* const names[model_count] = {"thud", "rattle", "tom", "lid", "tack", "bell", "brush", "plank", "chain", "bolt", "pluck", "bow"};
const char* const labels[model_count][8] = {
    {"TUNE", "DECAY", "HAMMER", "RUBBER", "GLIDE", "KNOCK", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "HAMMER", "PAPER", "SPOT", "RING", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "HAMMER", "RUBBER", "GLIDE", "KNOCK", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "BOUNCE", "SPREAD", "SNAP", "HOLLOW", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "TACK", "MUTE", "KNOCK", "SPOT", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "HAMMER", "MASS", "SPOT", "LOOSE", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "STROKE", "METAL", "BRISTLE", "SPOT", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "WIDTH", "HAMMER", "BLOOM", "BOLTS", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "BEADS", "SHAKE", "METAL", "SWELL", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "HAMMER", "MASS", "SPOT", "LOOSE", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "NAIL", "SPOT", "MUTE", "BUZZ", "PEDAL", "AGE"},
    {"TUNE", "DECAY", "PRESS", "DRAW", "BEND", "SPOT", "PEDAL", "AGE"},
};
// The pad controls' captions, as the engine gives them to the stock header.
inline void tune_text(char* out, i32 v) { number(out, clampi(v, -16, 15), "", true); }
inline void soft_text(char* out, i32 v) {
    v = clampi(v, 0, 15);
    if (!v) copy(out, header_capacity, "OFF");
    else number(out, v, "");
}

// ---------------------------------------------------------------- outlines
// FNV-1a over a few words: an outline's geometry key.
inline u32 mix(u32 key, i32 v) { return (key ^ static_cast<u32>(v)) * 16777619u; }
// A ribbon about a centreline of 32 points (Q10 px from the outline's
// origin), h above and below it: drawn at scale 2, so halved.
template <class F>
inline void ribbon(F& f, u32 p, const i32* cx, const i32* cy, i32 h) {
    typename F::path_type& o = f.paths[p];
    for (u32 j = 0; j < 32; ++j) {
        o.points[j].x = cx[j] / 2;
        o.points[j].y = (cy[j] - h) / 2;
        o.points[63 - j].x = cx[j] / 2;
        o.points[63 - j].y = (cy[j] + h) / 2;
    }
}
// A string from the far bridge to the hitch rail at y: its speaking length,
// to the near bridge at xb, swung up and down by amp (Q10 px) in its first
// mode, or (PLUCK) pulled aside at px by pull (Q10 iso units); the rest of
// it lies still on the bridge and rail.
template <class F>
inline void string_line(F& f, u32 p, i32 y, i32 xb, i32 amp, i32 px, i32 pull, u32 shade) {
    const i32 mx = (art::string_x0 + art::string_x1) / 2;
    const i32 ox = iso_x(mx, y), oy = iso_y(mx, y, art::zs);
    u32 key = mix(mix(mix(mix(2166136261u, xb), amp), px), pull);
    if (!key) key = 1;
    if (f.paths[p].key != key) {
        i32 cx[32], cy[32];
        for (u32 j = 0; j < 32; ++j) {
            const i32 i = static_cast<i32>(j);
            i32 x, d = 0, a = 0;
            if (j < 24) {
                x = art::string_x0 + ((xb - art::string_x0) * i) / 23;
                d = (amp * sn(i * 32768 / 23)) / 16384;
                if (pull) {
                    const i32 u = x - art::string_x0, w = px - art::string_x0, v = xb - x, r = xb - px;
                    a = x < px ? (pull * (u / 64)) / (w / 64 + 1) : (pull * (v / 64)) / (r / 64 + 1);
                }
            } else {
                x = xb + ((art::string_x1 - xb) * (i - 23)) / 8;
            }
            cx[j] = iso_x(x, y + a) - ox;
            cy[j] = iso_y(x, y + a, art::zs + d) - oy;
        }
        ribbon(f, p, cx, cy, 360);
        f.paths[p].key = key;
    }
    place(f, p, ox, oy, 2 * unit, shade, stroke);
}
// CHAIN's chain, along y at x: from the peg's top over the three strings,
// sagging between them, and draped off the last onto the plate. Its height
// at y (Q10 iso units).
struct Chain { i32 top, sag, drape; };
inline i32 chain_z(const Chain& c, i32 y) {
    const i32 on = art::zs + 300;
    if (y < art::string_0) {
        const i32 u = ((y - 2560) / 16) * 1024 / ((art::string_0 - 2560) / 16);
        return c.top + part(on - c.top, u) - part(c.sag / 2, sn(u * 32) >> 4);
    }
    if (y < art::string_2) {
        const i32 u = ((y - art::string_0) % (10 << 10)) * 32768 / (10 << 10);
        return on - part(c.sag, sn(u) >> 4);
    }
    const i32 u = clampi(((y - art::string_2) / 16) * 1024 / (c.drape / 16), 0, 1024);
    return 300 + part(on - 300, 1024 - ((u * u) >> 10));
}
const i32 chain_y0 = 2560, chain_y1 = 54 << 10;

// ---------------------------------------------------------------- the models' parts
// Where each model keeps the second-row controls of encoders 1 and 2.
enum Mark : unsigned char { mark_none, mark_glide, mark_spot, mark_bend };
// BRUSH's screws wedged by the bridge: how far before it (iso units) and between which strings
const opxy::i8 screw_back[3] = {7, 14, 10}, screw_y[3] = {25, 25, 35};
}

using namespace view;

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 24;
    state.spare = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const u32 m = input.model < model_count ? input.model : 0;
    i32 t[8], raw[8];
    for (u32 i = 0; i < 8; ++i) {
        const u32 r = input.slots[i] < 0 ? 0u : static_cast<u32>(input.slots[i]) >> 24;
        raw[i] = static_cast<i32>(r > 127 ? 127 : r);
        t[i] = (raw[i] * 8257) >> 10;   // Q10, 127 -> 1024
    }
    const i32 tune = clampi(input.pad[0], -16, 15);
    const i32 soft = clampi(input.pad[1], 0, 15) * 68;   // Q10
    const bool still = input.reduced_motion != 0;
    const i32 pedal = t[6], age_ = t[7];

    // --- the hit: the selected key's notes only
    const i32 step = clampi(input.elapsed, 0, 1024);
    const bool baseline = !state.initialized || state.track != input.track;
    const bool fresh = !baseline && input.note_serial != state.serial;
    state.serial = input.note_serial;
    state.track = input.track;
    state.initialized = 1;
    if (baseline) state.age = 1 << 24;
    else if (fresh) state.age = 0;
    else state.age = clampi(state.age + step, 0, 1 << 24);
    const i32 age = still ? (1 << 24) : state.age;   // reduced motion: always at rest
    const bool active = age < (1 << 24) - 4096;
    // DECAY's time (Q10 s): the damper lands then; BOW's after the draw
    const i32 ring = secs(shape::along(shape::damper[m], t[1] * 64));
    const i32 draw_t = secs(shape::along(shape::draw, t[3] * 64));
    const i32 land = m == M::BOW ? draw_t + ring : ring;
    const i32 t_hit = 31;   // Q10 s: a striker's flight, 30 ms
    const bool hammer = m == M::THUD || m == M::RATTLE || m == M::TOM || m == M::TACK || m == M::BELL || m == M::BOLT;

    // --- encoder 1: the near bridge, TUNE its place (shorter is higher)
    const i32 xb0 = lerp(art::bridge_lo, art::bridge_hi, t[0]);
    const i32 len = xb0 - art::node_x;   // the speaking length
    // the preparation's place: SPOT from 0.12 to 0.42 of the length back
    // from the bridge, or 0.22 on the models without one (THUD's and TOM's
    // rubber 0.3, clear of the bridge's glide)
    const i32 spot_t = m == M::RATTLE || m == M::BELL || m == M::BOLT ? t[4]
                     : m == M::TACK || m == M::BOW ? t[5] : m == M::PLUCK ? t[3] : -1;
    const i32 prep_x = xb0 - part(len, spot_t >= 0 ? 123 + ((spot_t * 307) >> 10) : m == M::THUD || m == M::TOM ? 307 : 225);
    // encoder 1's mark across the strings: where GLIDE starts the bridge,
    // where BEND takes it, or the preparation's SPOT
    i32 mark = 0;
    Mark kind = mark_none;
    if (m == M::THUD || m == M::TOM) { kind = mark_glide; mark = -part(len, (t[4] * 123) >> 10); }
    if (m == M::BOW) {
        // BEND down lengthens it, but never past the hitch rail
        kind = mark_bend;
        mark = -part(len, ((t[4] - 512) * 256) / 1024);
        if (xb0 + mark > (156 << 10)) mark = (156 << 10) - xb0;
    }
    if (m == M::RATTLE || m == M::BELL || m == M::BOLT) kind = mark_spot;
    // the bridge's travel on a hit: a hard blow's stretch falling back
    // (GLIDE), or the pitch bent over the draw and back (BEND)
    i32 slide = 0;
    if (active && kind == mark_glide) {
        const i32 e = age < 60 ? (age * 1024) / 60 : fall(age - 60, 160) >> 5;
        slide = toward0(part(mark, e), 512);
    }
    if (active && kind == mark_bend) {
        const i32 e = age < draw_t ? (age * 1024) / (draw_t + 1) : fall(age - draw_t, 300) >> 5;
        slide = toward0(part(mark, e), 512);
    }
    const i32 xb = xb0 + slide;
    box(frame, art::bridge_l, xb, (28 << 10), art::zs, unit, e1);
    if (kind == mark_spot) at(frame, art::mark_1, prep_x, art::string_1, art::zs + 300, unit, hair1);
    else if ((kind == mark_glide && mark) || (kind == mark_bend && iabs(mark) >= 1024)) at(frame, art::mark_1, xb0 + mark, art::string_1, art::zs + 300, unit, hair1);
    else hide(frame, art::mark_1);

    // --- the strings: still, or swinging after the striker meets them
    i32 amp = 0, pull = 0, pluck_x = 0;
    if (m == M::PLUCK) pluck_x = prep_x + (5 << 10);
    if (active) {
        // how long the string rings free (Q10 s), and how far it swings (Q10 px)
        i32 tau = 700, a0 = 1536, start = t_hit;
        if (m == M::THUD || m == M::TOM) tau = 700 - t[3] / 2;
        if (m == M::TACK) tau = 500 - t[3] / 3;
        if (m == M::BELL || m == M::BOLT || m == M::PLANK) tau = 1500;
        if (m == M::RATTLE) a0 = 1024 + t[5];                 // RING: the note left under the paper
        if (m == M::LID) { a0 = 768; start = 40; }
        if (m == M::BRUSH || m == M::CHAIN) { a0 = 512; start = 0; }
        if (m == M::PLUCK) { a0 = 2048; start = 50; }
        if (m == M::BOW) { start = 0; a0 = age < draw_t ? 512 + (age * 1024) / (draw_t + 1) : 1536; }
        i32 env = fall(age, tau);
        if (m == M::BOW && age < draw_t) env = 32768;
        if (age >= land) env = (env * (fall(age - land, 50) >> 5)) >> 10;
        if (age >= start) {
            const i32 hz = 7 + ((t[0] * 6) >> 10);
            const i32 ph = static_cast<i32>((static_cast<u32>(age - start) * static_cast<u32>(hz) * 64u) & 0xffffu);
            amp = toward0((((a0 * env) >> 15) * cs(ph)) / 16384, 256);
        }
        if (m == M::PLUCK && age < 50) pull = toward0((3 << 10) * age / 50, 256);   // the nail pulls it aside
    }
    string_line(frame, 0, art::string_0, xb, amp, pluck_x, 0, string_out);
    string_line(frame, 1, art::string_1, xb, m == M::PLUCK && pull ? 0 : amp, pluck_x, pull, string_mid);
    string_line(frame, 2, art::string_2, xb, amp, pluck_x, 0, string_out);

    // --- encoder 3, page 2: PEDAL lifts the felt off the sympathetic
    // strings behind; lifted, they shimmer with the note
    box(frame, art::pedal_l, art::damper_x, art::pedal_y, art::zs + 410 + pedal * 8, unit, e3);
    for (u32 k = 0; k < 2; ++k) {
        i32 dz = 0;
        if (active && pedal > 0 && age > t_hit) {
            const i32 env = fall(age, 900) >> 5;
            const i32 ph = static_cast<i32>((static_cast<u32>(age) * (9u + 2u * k) * 64u) & 0xffffu);
            dz = toward0((((pedal * env) >> 10) * 3 / 4) * sn(ph) / 16384, 256);
        }
        at(frame, art::symp_0 + k, art::symp_mid_x, k ? art::symp_y1 : art::symp_y0, art::zs + dz, unit, string_out);
    }

    // --- encoder 2: the damper on the strings. DECAY: it rests lighter,
    // hovering higher, the longer the note rings; a hit lifts it until
    // DECAY's time is up. RING and HOLLOW draw it off toward the near string.
    {
        const i32 hover = t[1] * 6;
        const i32 aside = m == M::RATTLE || m == M::LID ? t[5] * 14 : 0;
        i32 lift = 0;
        if (active) {
            lift = age < 50 ? ((5 << 10) * age) / 50 : 5 << 10;
            if (age >= land) lift = (lift * (fall(age - land, 40) >> 5)) >> 10;
            lift = toward0(lift, 256);
        }
        box(frame, art::damper_l, art::damper_x, art::string_1 + aside, art::zs + 410 + hover + lift, unit, e2);
    }

    // --- encoder 3: the striker
    // the hammer from beneath through its slot: felt (HAMMER: plump and big
    // when soft, small when hard), its molding (KNOCK: more wood under the
    // felt) and shank; it strikes and returns. SOFT shifts it back (una corda).
    if (hammer) {
        const i32 fs = m == M::TACK ? 840 : 1024 - ((t[2] * 358) >> 10);
        const i32 knock = m == M::THUD || m == M::TOM ? t[5] : m == M::TACK ? t[4] : -1;
        const i32 ks = knock >= 0 ? 700 + ((knock * 614) >> 10) : 1024;
        const u32 kc = m == M::TACK ? e1 : knock >= 0 ? e2 : wood;
        const i32 rest = art::zs - (5 << 10), strike = art::zs - 410;
        i32 rise = 0;
        if (active) {
            if (age < t_hit) rise = ((strike - rest) * ((age * 32) / t_hit) * ((age * 32) / t_hit)) / 1024;
            else rise = toward0(((strike - rest) * (fall(age - t_hit, 45) >> 5)) >> 10, 256);
        }
        const i32 y = art::string_1 - soft * 6;
        const i32 top = rest + rise;
        const i32 foot = top - ((art::felt_h * fs) >> 10);
        const i32 base = foot - ((art::molding_h * ks) >> 10);
        box(frame, art::felt_l, art::hammer_x, y, top, fs, e3);
        box(frame, art::molding_l, art::hammer_x, y, foot, ks, kc);
        at(frame, art::shank, art::hammer_x, y, base, unit, wood);
        // TACK: the drawing pin in the felt
        if (m == M::TACK) at(frame, art::tack, art::hammer_x, y, top, 400 + ((t[2] * 819) >> 10), e3);
        else hide(frame, art::tack);
    } else {
        park(frame, art::felt_l, 3);
        park(frame, art::molding_l, 3);
        if (m != M::PLANK) hide(frame, art::shank);
        hide(frame, art::tack);
    }
    // PLANK: the plank under a run of strings (WIDTH: as long as the keys it
    // presses), its felt pad (encoder 4, HAMMER: narrower as it hardens); it
    // rises to the strings over BLOOM's time and stays till DECAY's
    i32 plank_top = 0, plank_len = 0;
    if (m == M::PLANK) {
        const i32 ps = 600 + ((t[2] * 1434) >> 10);
        const i32 pads = (ps * (1024 - ((t[3] * 460) >> 10))) >> 10;
        const i32 bloom_t = secs(shape::along(shape::bloom, t[4] * 64)) + 34;
        i32 rise = 0;
        if (active) {
            rise = age < bloom_t ? ((5 << 10) * age) / bloom_t : 5 << 10;
            if (age >= land) rise = (rise * (fall(age - land, 45) >> 5)) >> 10;
            rise = toward0(rise, 256);
        }
        const i32 pad_top = art::zs - 410 - (5 << 10) + rise;
        plank_top = pad_top;
        plank_len = (ps * 400) >> 10;
        const i32 pad_foot = pad_top - ((art::pad_h * pads) >> 10);
        box(frame, art::pad_l, art::hammer_x, art::string_1, pad_foot, pads, e4);
        box(frame, art::plank_l, art::hammer_x, art::string_1, pad_foot, ps, e3);
        at(frame, art::shank, art::hammer_x, art::string_1, pad_foot - ((art::plank_h * ps) >> 10), unit, wood);
    } else {
        park(frame, art::plank_l, 3);
        park(frame, art::pad_l, 3);
    }

    // --- every model's own parts: parked unless this model uses them
    for (u32 n = art::peg_l; n <= art::peg_t; ++n) hide(frame, n);
    for (u32 n = art::rubber_a_l; n <= art::brush_screw_2; ++n) hide(frame, n);
    hide(frame, art::mark_2);
    for (u32 n = art::mute_l; n <= art::seam; ++n) hide(frame, n);
    // the preparation's top, where AGE's seam lies: x, y, z and the seam's length
    i32 sx = 0, sy = 0, sz = 0, sl = 0;
    // a nut on a screw or bolt (encoder 2: LOOSE, BUZZ backs it off up the
    // shank; loose, it chatters on a hit)
    i32 nut_at = -1, nut_y = 25 << 10, nut_lo = 0, nut_hi = 0, loose = 0;

    if (m == M::THUD || m == M::TOM) {
        // RUBBER: two rubber wedges between the unisons
        const i32 rs = 560 + ((t[3] * 737) >> 10);
        box(frame, art::rubber_a_l, prep_x, 25 << 10, art::zs, rs, e4);
        box(frame, art::rubber_b_l, prep_x, 35 << 10, art::zs, rs, e4);
        sx = prep_x; sy = 25 << 10; sz = art::zs + 7 * rs; sl = rs;
    }
    if (m == M::RATTLE) {
        // PAPER: a strip woven through the strings at SPOT, which chatters
        // while the string swings
        const i32 ps = 500 + ((t[3] * 717) >> 10);
        i32 jig = 0;
        if (active && age < land) jig = toward0((sn(static_cast<i32>((static_cast<u32>(age) * 2400u) & 0xffffu)) * ((ps * 3) >> 2)) / 16384, 512);
        box(frame, art::paper_l, prep_x + jig, art::string_1, art::zs + 410, ps, e4);
        sx = prep_x + jig; sy = art::string_1; sz = art::zs + 410 + ((art::paper_h * ps) >> 10); sl = ps;
    }
    if (m == M::TACK) {
        // MUTE: a felt strip across the strings at SPOT (encoder 2's mark)
        const i32 ms = 500 + ((t[3] * 717) >> 10);
        box(frame, art::mute_l, prep_x, art::string_1, art::zs + 410, ms, e4);
        at(frame, art::mark_2, prep_x, art::string_1, art::zs + 300, unit, hair2);
        sx = prep_x; sy = art::string_1; sz = art::zs + 410 + 3 * ms; sl = (ms * 600) >> 10;
    }
    if (m == M::BELL || m == M::PLUCK) {
        // MASS: a wood screw through the strings at SPOT; PLUCK's fitting
        // at its SPOT, the nail plucking beside it
        const i32 ss = m == M::BELL ? 600 + ((t[3] * 717) >> 10) : 640;
        at(frame, art::screw_shank, prep_x, 25 << 10, art::zs, ss, e4);
        at(frame, art::screw_head, prep_x, 25 << 10, art::zs, ss, e4);
        nut_at = prep_x; nut_lo = art::zs + 512; nut_hi = art::zs + ((art::screw_top * ss) >> 10) - 1024 - art::nut_h;
        loose = t[5];
        sx = prep_x; sy = 25 << 10; sz = art::zs + ((art::screw_top * ss) >> 10); sl = (ss * 500) >> 10;
    }
    if (m == M::BOLT) {
        // MASS: a bolt through the strings at SPOT, its head under them
        const i32 bs = 600 + ((t[3] * 717) >> 10);
        at(frame, art::bolt_head_side, prep_x, 25 << 10, art::zs, bs, e4 + 1);
        at(frame, art::bolt_head, prep_x, 25 << 10, art::zs, bs, e4);
        at(frame, art::bolt_shank, prep_x, 25 << 10, art::zs, bs, e4);
        nut_at = prep_x; nut_lo = art::zs + 512; nut_hi = art::zs + (((art::screw_top - 1024) * bs) >> 10) - 512 - art::nut_h;
        loose = t[5];
        sx = prep_x; sy = 25 << 10; sz = art::zs - (((8 << 10) - 2560) * bs >> 10); sl = (bs * 600) >> 10;
    }
    if (nut_at >= 0) {
        i32 z = nut_lo + part(nut_hi - nut_lo, loose);
        if (active && loose > 0 && age < land + 200)
            z += toward0((sn(static_cast<i32>((static_cast<u32>(age) * 3100u) & 0xffffu)) * ((loose * 3) >> 1)) / 16384, 512);
        at(frame, art::nut_side, nut_at, nut_y, z, unit, e2 + 1);
        at(frame, art::nut, nut_at, nut_y, z, unit, e2);
    }
    if (m == M::PLANK) {
        // BOLTS (encoder 2): bolts wedged among the strings by the bridge
        if (t[5] > 0) {
            const i32 bs = 300 + ((t[5] * 820) >> 10);
            at(frame, art::plank_bolt_0, xb - (12 << 10), 25 << 10, art::zs, bs, e2);
            at(frame, art::plank_bolt_1, xb - (19 << 10), 35 << 10, art::zs, bs, e2);
        }
        // BLOOM (encoder 1): felt laid against the bridge, which slows the
        // strings setting each other ringing
        box(frame, art::bfelt_l, xb - (3 << 10), art::string_1, art::zs + 410, 300 + ((t[4] * 717) >> 10), e1);
        // AGE on the plank's felt pad
        sx = art::hammer_x; sy = art::string_1; sz = plank_top; sl = plank_len;
    }
    if (m == M::BRUSH) {
        // the brush on the strings, swept to SPOT (encoder 2's mark) over
        // STROKE's time from as far back as STROKE reaches, and drawn back;
        // BRISTLE (encoder 1) coarsens its bristles
        const i32 to = lerp(62 << 10, xb - (10 << 10), t[5]);
        const i32 travel = (6 << 10) + t[2] * 24;
        const i32 stroke_t = secs(shape::along(shape::stroke, t[2] * 64)) + 34;
        i32 swept = 0;
        if (active) {
            if (age < stroke_t) swept = (age * 1024) / stroke_t;
            else swept = fall(age - stroke_t, 200) >> 5;
            swept = toward0(part(travel, swept), 512);
        }
        const i32 bx = to - travel + swept;
        const i32 ts = 600 + ((t[4] * 614) >> 10);
        box(frame, art::tuft_l, bx, art::string_1, art::zs + 410, ts, e1);
        const i32 hz = art::zs + 410 + ((art::tuft_h * ts) >> 10);
        at(frame, art::handle_l, bx, art::string_1, hz, unit, e3);
        at(frame, art::handle_t, bx, art::string_1, hz, unit, e3);
        box(frame, art::brush_l, bx, art::string_1, hz, unit, e3);
        at(frame, art::mark_2, to, art::string_1, art::zs + 300, unit, hair2);
        // METAL: screws wedged among the top strings by the bridge, one by one
        const i32 n = t[3] * 3;
        for (u32 k = 0; k < 3; ++k) {
            const i32 f = clampi(n - static_cast<i32>(k) * 1024, 0, 1024);
            if (f > 0) at(frame, art::brush_screw_0 + (k == 2 ? art::brush_screw_2 - art::brush_screw_0 : k),
                          xb - (screw_back[k] << 10), screw_y[k] << 10, art::zs, 300 + ((f * 700) >> 10), e4);
        }
        if (n > 0) {
            const i32 sc = 300 + ((clampi(n, 0, 1024) * 700) >> 10);
            sx = xb - (7 << 10); sy = 25 << 10; sz = art::zs + (11 * sc) / 2; sl = (sc * 400) >> 10;
        }
    }
    if (m == M::LID) {
        // the lid held over the strings (BOUNCE: the higher, the more it
        // bounces), dropped on a hit onto its rubber pads (SPREAD, encoder
        // 4: the springier, the longer the gap), bouncing each time a little
        // lower, then lifted back once the case's ring has gone. SNAP
        // (encoder 1): its hardwood lip.
        const i32 sp = 500 + ((t[3] * 840) >> 10);
        const i32 pad_top = art::zs + 410 + ((art::lidpad_h * sp) >> 10);
        box(frame, art::lidpad_0_l, prep_x, art::string_0, art::zs + 410, sp, e4);
        box(frame, art::lidpad_1_l, prep_x, art::string_2, art::zs + 410, sp, e4);
        const i32 held = (6 << 10) + t[2] * 14;
        i32 drop = 0;
        if (active) {
            const i32 g0 = secs(shape::along(shape::gap, t[3] * 64));
            const i32 bounces = 1 + (raw[2] * 5 + 63) / 127;
            i32 h = held, lift = 0;
            if (age < 40) lift = held - (held * ((age * 32 / 40) * (age * 32 / 40))) / 1024;
            else if (age < 2000) {
                i32 tt = age - 40, gap_t = g0 + 40;
                for (i32 b = 1; b < bounces; ++b) {
                    gap_t = (gap_t * 82) / 100 + 8;
                    h = h / 2;
                    if (tt < gap_t) {
                        const i32 p = (tt * 1024) / gap_t;
                        lift = (h * ((4 * p * (1024 - p)) >> 10)) >> 10;
                        break;
                    }
                    tt -= gap_t;
                }
            }
            const i32 back = ring + 300;
            if (age > back) lift = held - ((held * (fall(age - back, 120) >> 5)) >> 10);
            drop = toward0(lift - held, 256);
        }
        const i32 lz = pad_top + held + drop;
        box(frame, art::lid_l, prep_x, art::string_1, lz, unit, e3);
        box(frame, art::lip_l, prep_x, art::string_1 + art::lid_d / 2, lz, 400 + ((t[4] * 624) >> 10), e1);
        sx = prep_x; sy = art::string_0; sz = pad_top; sl = (sp * 400) >> 10;
    }
    if (m == M::CHAIN) {
        // a chain of glass beads (BEADS, encoder 3) draped over the strings
        // from its peg (SWELL, encoder 2: the higher its end is held, the
        // more the knocks swell), hanging slacker with SHAKE (encoder 4),
        // the jingles of METAL (encoder 1) among them; a hit sets them
        // hopping over SHAKE's time, front-loaded or swelling
        const i32 pegs = 600 + ((t[5] * 820) >> 10);
        Chain c;
        c.top = (art::peg_h * pegs) >> 10;
        c.sag = 600 + t[3] * 2;
        c.drape = (6 << 10) + t[3] * 6;
        box(frame, art::peg_l, prep_x, chain_y0, 0, pegs, e2);
        const i32 ox = iso_x(prep_x, 28 << 10), oy = iso_y(prep_x, 28 << 10, 7 << 10);
        const i32 h = 360 + ((age_ * 500) >> 10);   // AGE: coarser, duller links
        u32 key = mix(mix(mix(mix(0x9e3779b9u, prep_x), c.top), c.sag), h);
        if (!key) key = 1;
        if (frame.paths[3].key != key) {
            i32 cx[32], cy[32];
            for (u32 j = 0; j < 32; ++j) {
                const i32 y = chain_y0 + ((chain_y1 - chain_y0) / 31) * static_cast<i32>(j);
                cx[j] = iso_x(prep_x, y) - ox;
                cy[j] = iso_y(prep_x, y, chain_z(c, y)) - oy;
            }
            ribbon(frame, 3, cx, cy, h);
            frame.paths[3].key = key;
        }
        place(frame, 3, ox, oy, 2 * unit, hair1, stroke);
        const i32 shake_t = secs(shape::along(shape::shake, t[3] * 64));
        i32 env = 0;
        if (active && age < shake_t + 120) {
            const i32 pos = clampi((age * 1024) / (shake_t + 1), 0, 1024);
            const i32 shape_ = (1024 - pos) + (((pos - (1024 - pos)) * t[5]) >> 10);
            env = clampi(shape_, 0, 1024) * (age < shake_t ? 1024 : (fall(age - shake_t, 40) >> 5)) / 1024;
        }
        const i32 hop_h = (2 << 10) + t[3] * 2;
        const i32 n = 1024 + t[2] * 7;
        for (u32 k = 0; k < 8; ++k) {
            const i32 f = clampi(n - static_cast<i32>(k) * 1024, 0, 1024);
            const i32 y = (13 << 10) + static_cast<i32>(k) * (5 << 10);
            i32 hop = 0;
            if (env) hop = toward0(((hop_h * env) >> 10) * iabs(sn(static_cast<i32>((static_cast<u32>(age) * (90u + 23u * k)) & 0xffffu))) / 16384, 512);
            if (f > 0) at(frame, art::bead_0 + k, prep_x, y, chain_z(c, y) + hop, 600 + ((f * 410) >> 10), e3);
        }
        const i32 nj = t[4] * 4;
        for (u32 k = 0; k < 4; ++k) {
            const i32 f = clampi(nj - static_cast<i32>(k) * 1024, 0, 1024);
            const i32 y = (15 << 10) + 512 + static_cast<i32>(k) * (10 << 10);
            i32 hop = 0;
            if (env) hop = toward0(((hop_h * env) >> 11) * iabs(sn(static_cast<i32>((static_cast<u32>(age) * (70u + 31u * k)) & 0xffffu))) / 16384, 512);
            if (f > 0) at(frame, art::jingle_0 + k, prep_x, y, chain_z(c, y) + 600 + hop, 400 + ((f * 600) >> 10), e1);
        }
    }
    if (m == M::PLUCK) {
        // the nail from beside the strings, its tip at the string by the
        // fitting: NAIL from a round fingertip to a sharp quill; it pulls the
        // string aside and lets it go. MUTE (encoder 1): a palm's felt laid
        // against the bridge.
        i32 py = art::string_1 + 1536;
        if (active) {
            if (age < 50) py += pull;
            else py += toward0(((3 << 10) * (1024 - (fall(age - 50, 30) >> 5))) >> 10, 512)
                     - toward0(((3 << 10) * (age > 400 ? 1024 - (fall(age - 400, 120) >> 5) : 0)) >> 10, 512);
        }
        const i32 tip = art::zs + 1024;
        at(frame, art::fingertip, pluck_x, py, tip, 1100 - ((t[2] * 820) >> 10), e3);
        at(frame, art::quill, pluck_x, py, tip, 350 + ((t[2] * 666) >> 10), e3);
        if (t[4] > 0) box(frame, art::bfelt_l, xb - (3 << 10), art::string_1, art::zs + 410, 300 + ((t[4] * 717) >> 10), e1);
    }
    if (m == M::BOW) {
        // the rosined thread (encoder 3, PRESS: slack to taut) round the
        // middle string at SPOT (encoder 2's mark) out to its toggle
        // (encoder 4, DRAW: the more thread to draw, the further out); a
        // hit draws it out over the draw and lets it back
        at(frame, art::mark_2, prep_x, art::string_1, art::zs + 300, unit, hair2);
        i32 drawn = 0;
        if (active) {
            drawn = age < draw_t ? (age * 1024) / (draw_t + 1) : fall(age - draw_t, 300) >> 5;
            drawn = toward0(drawn * 8, 512);
        }
        const i32 yt = (62 << 10) + t[3] * 12 + drawn;
        const i32 zt = 6 << 10;
        box(frame, art::toggle_l, prep_x, yt, zt, unit, e4);
        const i32 sag = 300 + (1024 - t[2]) * 5;
        const i32 ox = iso_x(prep_x, (yt + art::string_1) / 2), oy = iso_y(prep_x, (yt + art::string_1) / 2, (art::zs + zt) / 2);
        u32 key = mix(mix(mix(0x01000193u, prep_x), yt), sag);
        if (!key) key = 1;
        if (frame.paths[3].key != key) {
            i32 cx[32], cy[32];
            for (u32 j = 0; j < 32; ++j) {
                const i32 u = static_cast<i32>(j) * 1024 / 31;
                const i32 y = art::string_1 + part(yt - art::string_1, u);
                const i32 z = art::zs + part(zt - art::zs, u) - part(sag, sn(u * 32) >> 4);
                cx[j] = iso_x(prep_x, y) - ox;
                cy[j] = iso_y(prep_x, y, z) - oy;
            }
            ribbon(frame, 3, cx, cy, 300);
            frame.paths[3].key = key;
        }
        place(frame, 3, ox, oy, 2 * unit, thread, stroke);
        sx = prep_x; sy = yt; sz = zt + 1536; sl = 600;
    }
    if (m != M::CHAIN && m != M::BOW) {
        // the outline parked: a small ring, so it stays a valid outline
        if (frame.paths[3].key != 7u) {
            for (u32 i = 0; i < 64; ++i) {
                const i32 ph = static_cast<i32>(i) * 1024;
                frame.paths[3].points[i].x = (cs(ph) * 6) >> 4;
                frame.paths[3].points[i].y = (sn(ph) * 4) >> 4;
            }
            frame.paths[3].key = 7u;
        }
        hide(frame, 3);
    }
    // --- encoder 4, page 2: AGE, a dark seam across the preparation (old
    // rubber's crack, old paper's crease, a screw's rust)
    if (age_ > 0 && sl > 0) at(frame, art::seam, sx, sy, sz, (sl * (200 + ((age_ * 824) >> 10))) >> 10, black);

    // --- the header: both rows, the edited one bright; while Shift is held
    // both rows show it: the key's mixer channel (MACHINE, CHOKE, PAN, LEVEL)
    // over its pad controls and sends (TUNE, SOFT, FX I, FX II)
    const char* titles[8];
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = labels[m][i];
        caption(values[i], captions[m][i], t[i], i == 0 ? tune : 0);
    }
    if (input.shift) {
        static const char* const chokes[4] = {"none", "1", "2", "3"};
        char pan[cap], level[cap], sends[2][cap], tune_[cap], soft_[cap];
        number(pan, input.pan, "");
        number(level, input.level, "dB");
        for (u32 k = 0; k < 2; ++k) {
            // SDK 1.15: 0 follows the track's send (TRK), else the key's percent + 1.
            const i32 percent = input.pad[2 + k] - 1;
            if (percent >= 0) number(sends[k], percent > 100 ? 100 : percent, "");
            else copy(sends[k], cap, "TRK");
        }
        tune_text(tune_, input.pad[0]);
        soft_text(soft_, input.pad[1]);
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"TUNE", "SOFT", "FX I", "FX II"}};
        const char* const shift_values[2][4] = {{names[m], chokes[input.choices[1] & 3u], pan, level}, {tune_, soft_, sends[0], sends[1]}};
        for (u32 line = 0; line < 2; ++line)
            for (u32 c = 0; c < 4; ++c) {
                titles[4 * line + c] = shift_titles[line][c];
                copy(values[4 * line + c], header_capacity, shift_values[line][c]);
            }
    }
    page_header(state.header, input, frame, titles, values, art::header_node);
}
}
