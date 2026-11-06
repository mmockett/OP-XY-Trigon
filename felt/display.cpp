#include "display.h"

// FELT's screen: one note of an old upright's action, side on. The string
// stands at the left between the pressure bar and the bridge; to its right
// the hammer, the damper, the wippen with its jack and spoon, and the key
// along the bottom on its balance pin. A hit plays the mechanism as a player
// piano would: the key dips by itself, the wippen rises, the damper leaves
// the string, the hammer flies to it and checks on its rebound, the string
// swings; when DECAY's time comes the key rises and the damper lands. The
// string, the hammer's felt, the damper's felt and BOW's thread are the four
// reshaping outlines; everything else is a part placed from the key's
// controls, on the same ranges the sound uses (shape.h).
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
// The nearest step of q (a power of two) under v, so a settled part stays exactly still.
inline i32 snap(i32 v, i32 q) { return v >= 0 ? (v / q) * q : -((-v + q - 1) / q) * q; }
// A tenth-degree turn as a unit vector (clockwise for positive).
inline Turn turned(i32 tenths) {
    const i32 ph = (tenths * 4096) / 225;   // 65536 / 3600
    const Turn t = {cs(ph) >> 4, sn(ph) >> 4};
    return t;
}
// A point (dx, dy) from a pivot, turned, in Q10 px.
inline void spin(const Turn& t, i32 dx, i32 dy, i32& x, i32& y) {
    x = (dx * t.cos - dy * t.sin) / 1024;
    y = (dx * t.sin + dy * t.cos) / 1024;
}

// ---------------------------------------------------------------- placing parts
template <class F>
inline void put(F& f, u32 node, i32 x, i32 y, i32 scale, u32 shade, i32 tenths = 0) {
    place(f, node, x, y, clampi(scale, 64, 2 * unit), shade, shade ? stroke : keep);
    if (tenths) turn(f, node, turned(tenths));
}
// A part that fades through the greys: parked once it has faded out.
template <class F>
inline void put_shaded(F& f, u32 node, i32 x, i32 y, i32 scale, i32 shade, i32 tenths = 0) {
    if (shade <= 0) { hide(f, node); return; }
    put(f, node, x, y, scale, static_cast<u32>(shade > 8 ? 8 : shade), tenths);
}
// An assembly turned about its pivot: every part shares the pivot and the turn.
template <class F>
inline void put_about(F& f, u32 node, i32 px, i32 py, i32 tenths, u32 shade) { put(f, node, px, py, unit, shade, tenths); }

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

// ---------------------------------------------------------------- the models' parts
// Which control sets each shared part: the hammer's hardness, its knock
// (thinner felt), its blow (GLIDE), the preparation's size and its spot
// (-1: none, or fixed).
struct Look { signed char hardness, knock, glide, size, spot; unsigned char struck; };
const Look looks[model_count] = {
    {2, 5, 4, 3, -1, 1},    // THUD
    {2, -1, -1, 3, 4, 1},   // RATTLE
    {2, 5, 4, 3, -1, 1},    // TOM
    {-1, -1, -1, -1, -1, 0},// LID
    {2, 4, -1, 3, 5, 1},    // TACK: TACK hardens, MUTE is the felt strip at SPOT
    {2, -1, -1, 3, 4, 1},   // BELL
    {-1, -1, -1, 3, 5, 0},  // BRUSH
    {3, -1, -1, 5, -1, 1},  // PLANK
    {-1, -1, -1, 2, -1, 0}, // CHAIN
    {2, -1, -1, 3, 4, 1},   // BOLT
    {-1, -1, -1, 4, 3, 0},  // PLUCK
    {-1, -1, -1, 2, 5, 0},  // BOW
};
// A point on the string's centre line at height y (Q10 px) along a string
// from top to bottom swung by amp (Q10 px) in its first mode.
inline i32 swing_at(i32 y, i32 top, i32 bottom, i32 amp) {
    const i32 len = bottom - top;
    if (len <= 0 || y <= top || y >= bottom) return 0;
    const i32 u = ((y - top) / 64) * 32768 / (len / 64);   // 0..32768: half a turn
    return (amp * sn(u)) / 16384;
}
}

using namespace view;

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 24;
    state.roll = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const u32 m = input.model < model_count ? input.model : 0;
    const Look& look = looks[m];
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
    else if (fresh) {
        // the roll moves on a notch a note, turning the spools as it plays
        if (!still) state.roll = (state.age < (1 << 24) - 4096 ? state.roll + 120 : state.roll) % 3600;
        state.age = 0;
    }
    else state.age = clampi(state.age + step, 0, 1 << 24);
    const i32 age = still ? (1 << 24) : state.age;   // reduced motion: always at rest
    // the damper's time (DECAY), and for BOW the draw before it
    i32 hold = secs(shape::along(shape::damper[m], t[1] * 64));
    if (m == M::BOW) hold = secs(shape::along(shape::draw, t[3] * 64));
    if (m == M::LID) hold = 0;
    const bool active = age < (1 << 24) - 4096;
    const i32 t_hit = 31;   // Q10 s: the hammer's flight, 30 ms

    // --- the key: dips by itself on a hit, rises when the damper's time comes (Q10 of its travel)
    i32 dip = 0;
    if (active && (look.struck || m == M::PLANK)) {
        if (age < hold) dip = 1024 - (fall(age, 10) >> 5);
        else dip = (fall(age - hold, 42) >> 5) * (1024 - (fall(hold, 10) >> 5)) / 1024;
        dip = snap(dip, 16);
    }
    // the damper's lift: with the key (or, for a model that does not strike,
    // the damper rail lifted for it), and the pedal's lift always
    i32 lift = 0;
    if (active && m != M::LID) {
        if (look.struck || m == M::PLANK) lift = clampi((dip - 300) * 1024 / 724, 0, 1024);
        else if (age < hold) lift = 1024 - (fall(age, 25) >> 5);
        else lift = (fall(age - hold, 50) >> 5) * (1024 - (fall(hold, 25) >> 5)) / 1024;
        lift = snap(lift, 16);
    }
    const i32 lift_all = clampi(lift + (pedal * 3) / 4, 0, 1024);

    // --- the string: a unison pair from the pressure bar to the bridge
    const bool string_tune = m != M::LID && m != M::CHAIN;
    const i32 bridge = string_tune ? lerp(art::bridge_lo, art::bridge_hi, t[0]) : (art::bridge_lo + art::bridge_hi) / 2;
    const i32 top = art::string_top;
    const i32 gap = (3 << 10) + age_ * 4;             // the unisons apart: AGE spreads them
    // how far it swings now (Q10 px), and its visual frequency
    i32 amp = 0;
    if (active) {
        // a free ring, Q10 s, as the model's own: rubber and felt mutes
        // shorten it, a gong or a plank lengthens it
        i32 ring_tau = 700;
        if (m == M::THUD || m == M::TOM) ring_tau = 700 - t[3] / 2;
        if (m == M::TACK) ring_tau = 500 - t[3] / 3;
        if (m == M::BOLT || m == M::PLANK || m == M::BELL) ring_tau = 1500;
        i32 env = fall(age, ring_tau);
        if (age >= hold && m != M::LID) env = (env * (fall(age - hold, 52) >> 5)) / 1024;
        i32 a0 = 4 << 10;
        if (m == M::RATTLE) a0 = (2 << 10) + t[5] * 3;   // RING: the note left under the paper
        if (m == M::LID || m == M::CHAIN) a0 = (pedal * 2);
        if (m == M::BRUSH) a0 = 1 << 10;
        if (m == M::BOW) a0 = age < hold ? (1 << 10) + ((age * 2048) / (hold + 1)) : 3 << 10;
        const i32 start = look.struck || m == M::PLANK ? t_hit : 0;
        if (age >= start) {
            const i32 hz_ = 7 + ((t[0] * 6) >> 10);
            const i32 ph = static_cast<i32>((static_cast<u32>(age - start) * static_cast<u32>(hz_) * 64u) & 0xffffu);
            amp = (((a0 * env) >> 15) * cs(ph)) >> 14;
        }
        if (m == M::PLUCK) {
            // the quill pulls the string aside, then lets it go
            if (age < 24) amp = -((3 << 10) * age) / 24;
        }
        amp = snap(amp, 256);
    }
    // the BOW's tuning lever bends the string's pitch (and the pluck's pull is a kink at the quill)
    const i32 spot_y = look.spot >= 0 ? lerp(art::strike_y + (14 << 10), bridge - (14 << 10), t[look.spot]) : art::strike_y + (26 << 10);
    {
        const i32 mid = (top + bridge) / 2;
        u32 key = 2166136261u;
        key = (key ^ static_cast<u32>(amp)) * 16777619u;
        key = (key ^ static_cast<u32>(gap)) * 16777619u;
        key = (key ^ static_cast<u32>(bridge)) * 16777619u;
        key = (key ^ static_cast<u32>(m == M::PLUCK ? spot_y : 0)) * 16777619u;
        if (!key) key = 1;
        if (frame.paths[0].key != key) {
            Frame::path_type& o = frame.paths[0];
            for (u32 i = 0; i < 32; ++i) {
                // down the right string, then up the left one; both 1/2 size (drawn at scale 2)
                const i32 y = top + 512 + ((bridge - top - 1024) * static_cast<i32>(i)) / 31;
                i32 d = swing_at(y, top, bridge, amp);
                if (m == M::PLUCK && amp < 0 && age < 24) {
                    // a kink at the quill rather than a curve
                    d = y < spot_y ? (amp * (y - top) / 64) / ((spot_y - top) / 64) : (amp * (bridge - y) / 64) / ((bridge - spot_y) / 64);
                }
                o.points[i].x = (d + gap / 2) / 2;
                o.points[i].y = (y - mid) / 2;
                o.points[63 - i].x = (d - gap / 2) / 2;
                o.points[63 - i].y = (y - mid) / 2;
            }
            o.key = key;
        }
        place(frame, 0, art::string_x, mid, 2 * unit, 9, stroke);
    }
    put(frame, art::bridge, art::string_x, bridge, unit, 5);
    put(frame, art::bridge_pin, art::string_x, bridge, unit, 6);
    // the neighbouring strings behind: dim, and ringing when the pedal holds
    // their dampers off; PLANK's plank lays more of them down
    {
        const i32 n = m == M::PLANK ? 1024 + t[2] * 5 : 2048;   // Q10 strings shown
        for (u32 k = 0; k < 6; ++k) {
            const i32 f = clampi(n - static_cast<i32>(k) * 1024, 0, 1024);
            const i32 x = art::string_x - (7 << 10) - static_cast<i32>(k) * (m == M::PLANK ? 3584 + t[2] * 2 : 4096);
            i32 dx = 0;
            if (active && pedal > 64 && age > t_hit) {
                const i32 env = fall(age, 900);
                dx = snap(((pedal * env) >> 15) * sn(static_cast<i32>((static_cast<u32>(age) * (9u + k) * 64u) & 0xffffu)) / 8192, 512);
            }
            const i32 y = (top + bridge) / 2 + (static_cast<i32>(k & 1) << 11);
            put_shaded(frame, art::neighbour_0 + k, x + dx, y, unit, f ? 1 + ((f * (1 + ((pedal * 3) >> 10))) >> 10) : 0);
        }
    }

    // --- the hammer about its centre pin: its felt's shape, its blow, its flight
    const i32 hard = look.hardness >= 0 ? t[look.hardness] : 512;
    const i32 knock = look.knock >= 0 ? t[look.knock] : 0;
    const i32 depth = (15 << 10) - hard * 4 - knock * 6;   // the felt: plump when soft, thinner over the core with KNOCK
    // the angle at which the felt's tip meets the string (tenths of a degree;
    // clockwise turns the head away from the string)
    const i32 tip_x = art::hammer_face_x - depth;      // local, Q10
    const i32 reach = (art::string_x + gap / 2 + 512) - art::hammer_x;
    const i32 a_strike = ((reach - tip_x) * 573) / -art::hammer_tip_y;   // radians over the tip's height, as tenths
    const i32 blow = (10 << 10) + (look.glide >= 0 ? t[look.glide] * 12 : 4 << 10) - soft * 5;   // px off the string at rest
    const i32 a_rest = a_strike + (blow * 573) / -art::hammer_tip_y;
    const i32 a_check = a_strike + (a_rest - a_strike) * 11 / 20;
    i32 a = a_rest;
    if (active && (look.struck || m == M::PLANK)) {
        if (age < t_hit) a = a_rest + ((a_strike - a_rest) * ((age * 1024 / t_hit) * (age * 1024 / t_hit) / 1024)) / 1024;
        else if (age < hold) a = a_check + ((a_strike - a_check) * (fall(age - t_hit, 14) >> 5)) / 1024;
        else a = a_rest + ((a_check - a_rest) * (fall(age - hold, 45) >> 5)) / 1024;
        a = snap(a, 2);
    }
    {
        const u32 sh = 7;
        put_about(frame, art::hammer_butt, art::hammer_x, art::hammer_y, a, 6);
        put_about(frame, art::hammer_butt_felt, art::hammer_x, art::hammer_y, a, 4);
        put_about(frame, art::hammer_catcher, art::hammer_x, art::hammer_y, a, 5);
        put_about(frame, art::hammer_shank, art::hammer_x, art::hammer_y, a, sh);
        put_about(frame, art::hammer_molding, art::hammer_x, art::hammer_y, a, 6);
        put(frame, art::hammer_pin, art::hammer_x, art::hammer_y, unit, 5);
        // TACK: the drawing pin in the felt
        if (m == M::TACK) put_shaded(frame, art::hammer_tack, art::hammer_x, art::hammer_y, unit, 2 + ((t[2] * 6) >> 10), a);
        else hide(frame, art::hammer_tack);
        // the felt: plump and round when soft, drawn to a point when hard;
        // AGE cuts grooves where the strings have struck it
        u32 key = 0x811c9dc5u;
        key = (key ^ static_cast<u32>(hard)) * 16777619u;
        key = (key ^ static_cast<u32>(depth)) * 16777619u;
        key = (key ^ static_cast<u32>(age_)) * 16777619u;
        if (!key) key = 1;
        if (frame.paths[1].key != key) {
            Frame::path_type& o = frame.paths[1];
            const i32 yc = art::hammer_tip_y, hh = 12288;   // 12 px
            for (u32 i = 0; i < 40; ++i) {
                // the face from top to bottom: s from -1 to 1 (Q10)
                const i32 s = -1024 + static_cast<i32>(i) * 2048 / 39;
                const i32 as = iabs(s);
                // round, cos(pi |s| / 2), to pointed, 1 - |s|
                const i32 c = cs(static_cast<i32>((as * 16384) / 1024)) >> 4;
                const i32 point = 1024 - as;
                i32 p = c + (((point - c) * hard) >> 10);
                // the grooves: three shallow cuts across the striking face
                for (i32 g = -1; g <= 1; ++g) {
                    const i32 dd = iabs(s - g * 260);
                    if (dd < 90) p -= ((90 - dd) * 2 * age_) >> 10;   // up to 2 px deep
                }
                o.points[i].x = (art::hammer_face_x - ((depth / 32) * p) / 32) / 2;
                o.points[i].y = (yc + (s * hh) / 1024) / 2;
            }
            for (u32 i = 0; i < 24; ++i) {
                // back up the molding's face, a hair inside it
                const i32 y = yc + hh - static_cast<i32>(i + 1) * (2 * hh) / 25;
                o.points[40 + i].x = (art::hammer_face_x + 256) / 2;
                o.points[40 + i].y = y / 2;
            }
            o.key = key;
        }
        place(frame, 1, art::hammer_x, art::hammer_y, 2 * unit, 9, stroke);
        turn(frame, 1, turned(a));
        // the hammer rail: the shank rests on it; SOFT moves it in
        // (the shank's right edge at the rail's height, at rest)
        i32 rx, ry;
        const i32 ly = art::rail_y - art::hammer_y;   // local, Q10
        const i32 lx = 1331 + ((ly + 16179) * 233) / 1024;   // along its edge, (1.3, -15.8) to (-10.2, -66)
        spin(turned(a_rest), lx, ly, rx, ry);
        put(frame, art::hammer_rail, art::hammer_x + rx + 512, art::rail_y, unit, 5);
        put(frame, art::hammer_rail_felt, art::hammer_x + rx + 512, art::rail_y, unit, 4);
    }

    // --- the damper: its felt (DECAY: thick, a dead note; thin, a long one)
    // pressed on the string; lifted off by the spoon, or by the pedal's rail
    {
        const i32 d = (10 << 10) - t[1] * 8;   // the felt's thickness
        // turned so its felt just meets the string
        const i32 face = art::damper_face_x;   // local x of the head's face
        const i32 target = art::string_x + gap / 2 + 512 - art::damper_x;
        const i32 r = art::damper_y - art::damp_y;   // the felt's height over the pin, Q10 px
        const i32 rest = ((target - (face - d)) * 573) / r;
        const i32 da = rest + (lift_all * 90) / 1024;   // up to 9 degrees away
        const i32 dq = snap(da, 2);
        put_about(frame, art::damper_lever, art::damper_x, art::damper_y, dq, 6);
        put_about(frame, art::damper_wire, art::damper_x, art::damper_y, dq, 6);
        put_about(frame, art::damper_head, art::damper_x, art::damper_y, dq, 6);
        put(frame, art::damper_pin, art::damper_x, art::damper_y, unit, 5);
        u32 key = 0x9e3779b9u ^ static_cast<u32>(d);
        if (!key) key = 1;
        if (frame.paths[2].key != key) {
            Frame::path_type& o = frame.paths[2];
            const i32 y0 = art::damp_y - art::damper_y;   // local
            const i32 x1 = face - 256, x0 = face - d;
            // a felt block: 6
            // points across its top, 26 down its face, 6 back along its foot,
            // 26 up its back against the head
            for (u32 i = 0; i < 64; ++i) {
                i32 x, y;
                const i32 h = 10 << 10;
                if (i < 6) { x = x1 - ((x1 - x0) * static_cast<i32>(i)) / 6; y = y0 - h; }
                else if (i < 32) { x = x0; y = y0 - h + ((2 * h) * static_cast<i32>(i - 6)) / 26; }
                else if (i < 38) { x = x0 + ((x1 - x0) * static_cast<i32>(i - 32)) / 6; y = y0 + h; }
                else { x = x1; y = y0 + h - ((2 * h) * static_cast<i32>(i - 38)) / 26; }
                o.points[(i + 48) & 63].x = x / 2;
                o.points[(i + 48) & 63].y = y / 2;
            }
            o.key = key;
        }
        place(frame, 2, art::damper_x, art::damper_y, 2 * unit, 9, stroke);
        turn(frame, 2, turned(dq));
    }

    // --- the wippen and key: the key's front dips, its tail lifts the wippen
    {
        const i32 ka = (dip * 22) / 1024;          // the key, up to 2.2 degrees
        const i32 wa = -(dip * 44) / 1024;         // the wippen, up to 4.4 degrees the other way
        put_about(frame, art::key, art::key_x, art::key_y, ka, 6);
        put_about(frame, art::key_front, art::key_x, art::key_y, ka, 4);
        put_about(frame, art::key_capstan, art::key_x, art::key_y, ka, 5);
        put_about(frame, art::key_lead_0, art::key_x, art::key_y, ka, 4);
        put_about(frame, art::key_lead_1, art::key_x, art::key_y, ka, 4);
        put(frame, art::key_pin, art::key_x, art::key_y, unit, 5);
        put_about(frame, art::wippen, art::wippen_x, art::wippen_y, wa, 6);
        put_about(frame, art::wippen_jack, art::wippen_x, art::wippen_y, wa, 6);
        put_about(frame, art::wippen_spoon, art::wippen_x, art::wippen_y, wa, 5);
        put(frame, art::wippen_pin, art::wippen_x, art::wippen_y, unit, 5);
    }

    // --- every model's own parts: parked unless this model uses them
    for (u32 n = art::rubber; n <= art::plank_grain; ++n) hide(frame, n);
    if (m != M::BOW) {
        // the thread's outline, parked: a small ring so it stays a valid outline
        if (frame.paths[3].key != 7u) {
            for (u32 i = 0; i < 64; ++i) {
                const i32 ph = static_cast<i32>(i) * 1024;
                frame.paths[3].points[i].x = (cs(ph) * 6) >> 4;
                frame.paths[3].points[i].y = (sn(ph) * 4) >> 4;
            }
            frame.paths[3].key = 7u;
        }
        hide(frame, 3);
        hide(frame, art::tuning_lever);
    }
    const i32 size = look.size >= 0 ? t[look.size] : 512;
    const i32 here = swing_at(spot_y, top, bridge, amp);   // the string's swing where the preparation sits
    const i32 sx = art::string_x + here;
    if (m == M::THUD || m == M::TOM) {
        // RUBBER: a rubber wedge between the unisons, close by the hammer
        const i32 sc = 400 + ((size * 700) >> 10);
        put(frame, art::rubber, sx, spot_y, sc, 7);
        put(frame, art::rubber_lip, sx, spot_y, sc, 5);
    }
    if (m == M::RATTLE) {
        // PAPER: a strip woven through the strings at SPOT, wider the more
        // of it; it chatters on a hit. RING: a felt mute by the bridge, drawn
        // out as the note is let ring
        const i32 jig = active && age < hold ? ((sn(static_cast<i32>((static_cast<u32>(age) * 2400u) & 0xffffu)) * ((size * 30) >> 10)) >> 14) : 0;
        put(frame, art::paper, sx, spot_y, 420 + ((size * 600) >> 10), 7, snap(jig, 10));
        put(frame, art::mute, art::string_x + (t[5] * 12), bridge - (10 << 10), unit, 5);
        put(frame, art::mute_line, art::string_x + (t[5] * 12), bridge - (10 << 10), unit, 4);
    }
    if (m == M::TACK || m == M::PLUCK) {
        // MUTE: a felt strip across the strings, thicker the more it mutes
        const i32 my = m == M::TACK ? spot_y : bridge - (12 << 10);
        const i32 mt = m == M::TACK ? t[3] : t[4];
        put(frame, art::mute, art::string_x + swing_at(my, top, bridge, amp), my, 560 + ((mt * 640) >> 10), 6);
        put(frame, art::mute_line, art::string_x + swing_at(my, top, bridge, amp), my, 560 + ((mt * 640) >> 10), 4);
    }
    if (m == M::BELL || m == M::BOLT || m == M::PLUCK || m == M::PLANK) {
        // MASS: a screw or bolt through the strings at SPOT, larger with its
        // mass; LOOSE backs it off and cocks it, and it chatters on a hit
        const i32 loose = m == M::PLUCK ? t[5] : m == M::PLANK ? 0 : t[5];
        i32 sc = 600 + ((size * 640) >> 10);
        i32 py = spot_y, px = sx;
        if (m == M::PLUCK) { sc = 640; py = bridge - (26 << 10); px = art::string_x + swing_at(py, top, bridge, amp); }
        if (m == M::PLANK) { sc = 520 + ((t[5] * 500) >> 10); py = art::strike_y + (40 << 10); px = art::string_x + swing_at(py, top, bridge, amp); }
        const i32 chat = active && age < hold + 200 && loose > 0 ? snap((sn(static_cast<i32>((static_cast<u32>(age) * 3100u) & 0xffffu)) * ((loose * 40) >> 10)) >> 14, 10) : 0;
        const i32 cock = (loose * 120) >> 10;
        if (m == M::BELL) {
            // backed out of the strings as a whole
            put(frame, art::screw_shank, px + loose * 7, py, sc, 7, cock + chat);
            put(frame, art::screw_thread, px + loose * 7, py, sc, 4, cock + chat);
            put(frame, art::screw_head, px + loose * 7, py, sc, 7, cock + chat);
        } else {
            const i32 shade = m == M::PLANK ? (t[5] > 0 ? 2 + ((t[5] * 5) >> 10) : 0) : 7;
            put_shaded(frame, art::bolt_shank, px, py, sc, shade, cock / 3);
            put_shaded(frame, art::bolt_head, px, py, sc, shade, cock / 3);
            // the nut, backed off along the bolt and cocked on it
            put_shaded(frame, art::bolt_nut, px + ((loose * 9 * sc) >> 10), py, sc, shade, cock + chat);
            put_shaded(frame, art::bolt_nut_face, px + ((loose * 9 * sc) >> 10), py, sc, shade - 3, cock + chat);
        }
    }
    if (m == M::BRUSH) {
        // the brush at SPOT, its bristles on the string; a hit sweeps it up
        // the string over STROKE's time; BRISTLE splays its bristles; METAL
        // the screws wedged in the strings above
        const i32 stroke_t = secs(shape::along(shape::stroke, t[2] * 64));
        const i32 travel = (10 << 10) + t[2] * 26;
        i32 up = 0;
        if (active) {
            if (age < stroke_t + 40) up = (travel * clampi((age * 1024) / (stroke_t + 40), 0, 1024)) / 1024;
            else up = (travel * (fall(age - stroke_t - 40, 160) >> 5)) / 1024;
            up = snap(up, 512);
        }
        const i32 by = spot_y + t[2] * 10 - up, bx = art::string_x - gap / 2 - 1024 + swing_at(by, top, bridge, amp);   // a longer stroke starts lower
        put(frame, art::brush_handle, bx, by, unit, 6, 150);
        put(frame, art::brush_ferrule, bx, by, unit, 5, 150);
        put(frame, art::brush_block, bx, by, unit, 6, 150);
        for (u32 k = 0; k < 4; ++k) {
            const i32 splay = ((static_cast<i32>(k) * 2 - 3) * t[4] * 70) >> 10;   // tenths
            put(frame, art::brush_bristle_0 + k, bx, by, unit, 7, 150 - splay);
        }
        for (u32 k = 0; k < 3; ++k) {
            const i32 y = art::damp_y + ((18 + 14 * static_cast<i32>(k)) << 10);
            put_shaded(frame, art::string_screw_0 + k, art::string_x + swing_at(y, top, bridge, amp), y, 600 + ((t[3] * 400) >> 10),
                       (t[3] * 9 + 1000 - static_cast<i32>(k) * 2048) >> 10, 450);
        }
    }
    if (m == M::CHAIN) {
        // a chain of glass beads draped over the strings, hanging off to the
        // right: BEADS how many, TUNE how small (the smaller, the higher),
        // METAL the jingles among them; a hit sets them hopping, front-loaded
        // or swelling (SWELL) over SHAKE's time
        const i32 n = 3 * 1024 + t[2] * 7;
        const i32 bead_s = 1300 - ((t[0] * 600) >> 10);
        const i32 shake_t = secs(shape::along(shape::shake, t[3] * 64));
        for (u32 k = 0; k < 10; ++k) {
            const i32 f = clampi(n - static_cast<i32>(k) * 1024, 0, 1024);
            // along a catenary from the string down to the right
            const i32 j = static_cast<i32>(k);
            // SHAKE lets the chain hang slacker; SWELL lifts its far end
            const i32 slack = 600 + t[3];
            const i32 x = art::string_x - ((j * 7 + 2) << 10) * bead_s / 1024;
            const i32 y = art::strike_y + (30 << 10) + (((j * j * 140) << 10) / 1024 * bead_s / 1024) * slack / 1024 - j * t[5] * 2;
            i32 hop = 0;
            if (active && age < shake_t + 120) {
                const i32 pos = clampi((age * 1024) / (shake_t + 1), 0, 1024);
                const i32 shape_ = (1024 - pos) + (((pos - (1024 - pos)) * t[5]) >> 10);
                const i32 env = clampi(shape_, 0, 1024) * (age < shake_t ? 1024 : (fall(age - shake_t, 40) >> 5)) / 1024;
                const i32 h = (3 << 10) + t[3] * 4;
                hop = snap((h * env / 1024) * iabs(sn(static_cast<i32>((static_cast<u32>(age) * (90u + 23u * k)) & 0xffffu))) / 16384, 512);
            }
            put_shaded(frame, art::bead_0 + k, x, y - hop, bead_s, (f * 7) >> 10);
        }
        for (u32 k = 0; k < 4; ++k) {
            const i32 j = static_cast<i32>(k) * 2 + 1;
            const i32 slack = 600 + t[3];
            const i32 x = art::string_x - ((j * 7 + 6) << 10) * bead_s / 1024;
            const i32 y = art::strike_y + (34 << 10) + (((j * j * 140) << 10) / 1024 * bead_s / 1024) * slack / 1024 - j * t[5] * 2;
            put_shaded(frame, art::jingle_0 + k, x, y, 700 + ((t[4] * 500) >> 10), t[4] > 0 ? (t[4] * 8 + 1100 - static_cast<i32>(k) * 1536) >> 10 : 0, 300);
        }
    }
    if (m == M::PLUCK) {
        // the quill from up and to the right, its tip at SPOT: NAIL from a
        // rounded fingertip pad to a sharp quill; it pulls and lets go
        i32 qx = art::string_x - gap / 2 - 512, qy = spot_y;
        if (active) {
            i32 pull = 0;
            if (age < 24) pull = ((3 << 10) * age) / 24;
            else pull = snap((((10 << 10)) * (1024 - (fall(age - 24, 30) >> 5))) / 1024, 512) - (((10 << 10) * (age > 400 ? 1024 - (fall(age - 400, 120) >> 5) : 0)) / 1024);
            qx -= pull;
            qy -= pull / 2;
        }
        put(frame, art::quill, qx, qy, unit, 6);
        put(frame, art::quill_vane, qx, qy, unit, 4);
        put_shaded(frame, art::quill_tip, qx, qy, 700 + ((t[2] * 500) >> 10), (t[2] * 8) >> 10);
        put_shaded(frame, art::quill_nail, qx, qy, 1200 - ((t[2] * 500) >> 10), 7 - ((t[2] * 8) >> 10));
    }
    if (m == M::BOW) {
        // the rosined thread looped round the string at SPOT and out to its
        // toggle; PRESS pulls it taut, DRAW how far a hit draws it; BEND turns
        // the tuning lever on the pin, and further over the draw
        const i32 draw_t = hold;
        i32 drawn = 0;
        if (active) {
            if (age < draw_t) drawn = ((age * 1024) / (draw_t + 1));
            else drawn = fall(age - draw_t, 300) >> 5;
            drawn = snap(drawn, 16);
        }
        const i32 reach_ = (8 << 10) + t[3] * 8;              // DRAW: how far it goes
        const i32 tx = art::string_x - (34 << 10) - t[3] * 12 - ((reach_ * drawn) >> 10);
        const i32 ty = spot_y + (8 << 10) - ((t[2] * 6));
        put(frame, art::toggle, tx, ty, unit, 6);
        put(frame, art::toggle_grain, tx, ty, unit, 4);
        // the thread: from the loop round the string to the toggle, sagging
        // when slack (PRESS low), straight when taut
        const i32 sag = (7 << 10) - t[2] * 6;
        u32 key = 0x01000193u;
        key = (key ^ static_cast<u32>(tx)) * 16777619u;
        key = (key ^ static_cast<u32>(sag)) * 16777619u;
        key = (key ^ static_cast<u32>(spot_y)) * 16777619u;
        key = (key ^ static_cast<u32>(here)) * 16777619u;
        if (!key) key = 1;
        const i32 x0 = sx, y0 = spot_y;
        const i32 ox = (x0 + tx) / 2, oy = (y0 + ty) / 2;
        if (frame.paths[3].key != key) {
            Frame::path_type& o = frame.paths[3];
            // round the string (a small loop), then out to the toggle and back, 1/2 size
            for (u32 i = 0; i < 64; ++i) {
                i32 x, y;
                if (i < 12) {
                    // the loop round the unisons, open on the left where the thread leaves
                    const i32 ph = 27307 - static_cast<i32>(i) * 54613 / 11;   // from its lower left round to its upper left
                    x = x0 + ((((gap / 2) + 1536) * cs(ph)) >> 14);
                    y = y0 + ((1536 * sn(ph)) >> 14);
                } else {
                    // out along the top edge and back along the bottom, 0.5 px apart
                    const u32 j = i < 38 ? i - 12 : 63 - i;   // 0..25 and back 25..0
                    const i32 u = static_cast<i32>(j) * 1024 / 25;
                    const i32 bow_ = (sag * ((4 * u * (1024 - u)) >> 10)) >> 10;
                    x = x0 - ((gap / 2 + 1536)) + (((tx - x0 + gap / 2 + 1536) * u) >> 10);
                    y = y0 + (((ty - y0) * u) >> 10) + bow_ + (i < 38 ? -256 : 256);
                }
                o.points[i].x = (x - ox) / 2;
                o.points[i].y = (y - oy) / 2;
            }
            o.key = key;
        }
        place(frame, 3, ox, oy, 2 * unit, 10, stroke);
    }
    {
        // the tuning lever on the pin: still unless BOW bends the pitch
        i32 la = 0;
        if (m == M::BOW) {
            const i32 bend_ = t[4] - 512;   // -512..512
            const i32 drawn = active && age < hold ? (age * 1024) / (hold + 1) : (active ? fall(age - hold, 300) >> 5 : 0);
            la = snap((bend_ * 200) / 512 + ((bend_ * 100 / 512) * drawn) / 1024, 4);
            put(frame, art::tuning_lever, art::tune_pin_x, art::tune_pin_y, unit, 6, la);
        }
    }
    {
        // the fallboard: open against the panel (LID: held at SPREAD's height,
        // TUNE its length; a hit drops it to the keys, where it bounces
        // BOUNCE times, each gap and height a little smaller)
        i32 ang = -840;   // tenths: open, standing up against the panel
        i32 len = unit;
        if (m == M::LID) {
            len = 860 + ((t[0] * 300) >> 10);
            const i32 open = 120 + ((t[3] * 450) >> 10);    // SPREAD: how high it is held
            ang = -open;
            if (active) {
                // the fall: the first gap's worth of time, as a dropped board falls
                const i32 g0 = secs(shape::along(shape::gap, t[3] * 64));
                const i32 bounces = 1 + ((raw[2] * 5 + 63) / 127);
                i32 at = 0, gap_t = g0 + 40, h = open;
                i32 lift_ = 0;
                if (age < 40) lift_ = open - (open * ((age * 1024 / 40) * (age * 1024 / 40) / 1024)) / 1024;
                else {
                    i32 tt = age - 40;
                    lift_ = 0;
                    for (i32 b = 1; b < bounces; ++b) {
                        gap_t = (gap_t * 82) / 100 + 8;
                        h = (h * 50) / 100;
                        if (tt < gap_t) {
                            const i32 p = (tt * 1024) / gap_t;
                            lift_ = (h * ((4 * p * (1024 - p)) >> 10)) >> 10;
                            break;
                        }
                        tt -= gap_t;
                    }
                    if (age > 2000) lift_ = 0;
                    (void)at;
                }
                ang = -lift_;
                // and it is lifted back again, slowly, once its ring has gone
                const i32 back = secs(shape::along(shape::damper[M::LID], t[1] * 64)) + 300;
                if (age > back) ang = -((open * (1024 - (fall(age - back, 120) >> 5))) >> 10);
            }
            ang = snap(ang, 4);
            // BOUNCE: the felt it lands on, thinner the more it bounces; SNAP
            // the board's brass lip; HOLLOW the key slip drawn away
            put(frame, art::landing_felt, art::hinge_x + (50 << 10), art::key_top - (4 << 10), 1400 - ((t[2] * 700) >> 10), 5);
        }
        put(frame, art::fallboard, art::hinge_x, art::hinge_y, len, 6, ang);
        put(frame, art::fallboard_lip, art::hinge_x, art::hinge_y, len, 3, ang);
        // SNAP: a hardwood edge on the board's lip, growing from nothing
        if (m == M::LID) put_shaded(frame, art::fallboard_edge, art::hinge_x, art::hinge_y, (len * (300 + ((t[4] * 724) >> 10))) >> 10, 2 + ((t[4] * 6) >> 10), ang);
        else hide(frame, art::fallboard_edge);
        // the key slip; HOLLOW draws it away from the keys, opening the case
        put(frame, art::key_slip, art::key_front_x + (m == M::LID ? t[5] * 9 : 0), (198 << 10), unit, 4);
    }
    if (m == M::PLANK) {
        // the plank laid on the keys' fronts, pressing them as it settles (BLOOM)
        const i32 bloom_t = secs(shape::along(shape::bloom, t[4] * 64));
        i32 down = 0;
        if (active) down = age < hold ? (1024 - fall(age, bloom_t / 3 + 8) / 32) : (fall(age - hold, 42) >> 5);
        const i32 ka = (dip * 22) / 1024;
        i32 px, py;
        spin(turned(ka), art::key_front_x - (30 << 10) - art::key_x, art::key_top - art::key_y, px, py);
        const i32 rise = ((6 << 10) * (1024 - down)) >> 10;
        (void)rise;
        const i32 lifted = snap((t[4] * 8) * (1024 - down) / 1024, 128);   // BLOOM: held higher, it settles slower
        put(frame, art::plank, art::key_x + px, art::key_y + py - (1 << 10) - lifted, unit, 6, ka);
        put(frame, art::plank_grain, art::key_x + px, art::key_y + py - (1 << 10) - lifted, unit, 4, ka);
    }

    {
        // the player's spools: the paper drawn on by a notch each note, eased
        // over the note's first 0.15 s; the take-up spool, smaller, turns further
        const i32 ease = active ? 1024 - (fall(age, 40) >> 5) : 1024;
        const i32 r = snap(state.roll + ((120 * ease) >> 10), 4);
        put(frame, art::roll_spokes, art::roll_x, art::roll_y, unit, 5, -r);
        put(frame, art::take_up_spokes, art::take_up_x, art::take_up_y, unit, 5, -(r * 4) / 3);
    }

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
            const i32 code = input.pad[2 + k];
            if (code > 0) number(sends[k], ((code > 7 ? 7 : code) - 1) * 100 / 6 + (code == 3 || code == 6 ? 1 : 0), "");
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
