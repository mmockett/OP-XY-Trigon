#include "display.h"
#include "ranges.h"

// FLICKER's screen: a strip of 16 mm sound film lying on a near-black slat,
// isometric, four frames of it on the slat. Its soundtrack carries the
// selected key's sound drawn as a bilateral variable-area shape, the same
// drawing in every frame, laid along the track in hairlines; the four
// drawings are the reshaping paths. A note pulls the film one frame along
// over the hit's own length, under a small gate whose slit lights the track,
// so the next frame's identical drawing takes the place of the last. The grey
// is the encoder's: the drawing CDCDCD (PITCH, and the model's own control on
// page 2), the pencil guides of its taper 4B4B4B (DECAY; INK their weight and
// the ink's), the gate 969696 (FLUTTER lifts it), DUST's specks CDCDCD.
namespace flicker {
namespace view {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;
struct M { enum : u32 { SWEEP, SCRATCH, RING, STROKES, DOT, CARD, HATCH, ORNAMENT, STIPPLE, BOOP, WAVE, CLAW }; };
const u32 models = 12;

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
static const opxy::u32 octave_steps[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682, 96785, 101070,
                                           105545, 110218, 115098, 120194, 125515, 131072};
inline u32 pow2v(i32 l) {
    if (l < -16 * 65536) return 0;
    if (l > 14 * 65536) l = 14 * 65536;
    const i32 n = l >> 16;   // floor
    const u32 f = static_cast<u32>(l - n * 65536);
    const u32 k = f >> 12, r = f & 0xfffu;
    const u32 m = octave_steps[k] + (((octave_steps[k + 1] - octave_steps[k]) * r) >> 12);
    return n >= 0 ? m << n : m >> -n;
}
// exp(-a / b) in Q15, for a and b in the same units.
inline i32 fall(i32 a, i32 b) {
    if (a <= 0) return 32768;
    if (b <= 0 || a > 16 * b || a >= (1 << 21)) return 0;
    const i32 x = ((a * 1024) / b) * 64;                      // a / b, Q16
    const i32 l = x + (x >> 1) - (x >> 4) - (x >> 7);         // times log2 e
    return static_cast<i32>(pow2v(-l) >> 1);
}
inline i32 iabs(i32 v) { return v < 0 ? -v : v; }
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 lerp(i32 a, i32 b, i32 t) { return a + (((b - a) >> 2) * clampi(t, 0, 1024) >> 8); }   // t Q10; |b - a| < 2^21
// a log2 value between two ends, t a Q10 unit (unclamped: -2048..3072)
inline i32 lmix(i32 la, i32 lb, i32 t) { return la + (((lb - la) >> 1) * t >> 9); }
inline i32 lmixc(i32 la, i32 lb, i32 t) { return lmix(la, lb, clampi(t, 0, 1024)); }
// a value from its log2 (Q16), in Q10
inline i32 q10_of(i32 l) { return static_cast<i32>(pow2v(l) >> 6); }
// A deterministic noise in Q14 (-16384..16383) for an index and a seed.
inline i32 hashed(u32 i, u32 seed) {
    u32 h = (i + 1u) * 2654435761u ^ seed;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<i32>(h >> 17) - 16384;
}

// ---------------------------------------------------------------- the drawing
// A drawing is a width profile along one frame's stretch of track: h on a
// grid of half pixels, x = 0 at the frame's near end (the start of the
// sound, which the pull brings to the slit first) to 66 px; h is the half
// width of the clear shape, mirrored about the track's centre line. lay()
// then puts it on the track in isometric.
const i32 grid = 133;            // 66 px in half pixels
const i32 step = 512;            // Q10 px between grid points
const i32 full = 21 * unit;      // the height a full drawing reaches, Q10 px (the track is 24 either side)
const i32 thin = 640;            // the thinnest the shape is drawn (0.6 px): never no area

// SHAPE: sine (0) to triangle (a half) to a drawn square (1), Q14, phase 65536 a turn.
inline i32 wave(i32 phase, i32 shape) {
    // a pen's sine is full and round: s (2 - |s|), so it reads apart from the triangle's straight sides
    const i32 s0 = sn(phase);
    const i32 s = (s0 * (32768 - iabs(s0))) >> 14;
    const u32 p = static_cast<u32>(phase + 16384) & 0xffffu;
    const i32 f = static_cast<i32>(p < 32768u ? p : 65535u - p);
    const i32 tri = f - 16384;
    if (shape <= 512) return s + (((tri - s) * shape) >> 9);
    const i32 g = 1024 + (shape - 512) * 8;   // up to 5 times as tall, cut flat
    return clampi((tri * g) >> 10, -16384, 16384);
}
// A bead: the half width a wave value s (Q14) draws at an amplitude a (Q15), Q10 px.
inline i32 bead(i32 a, i32 s) { return ((clampi(a, 0, 32768) >> 5) * ((16384 + clampi(s, -16384, 16384)) >> 5) >> 10) * (full >> 10); }
// A pitch control's spacing, Q10 px a cycle: wide at 0, close at 1 (log2 px
// ends, Q16); TUNE moves the control's place by its semitones over the range.
inline i32 spacing(i32 t, i32 tune, i32 lwide, i32 lclose, i32 span_st) {
    const i32 tt = clampi(t + (tune * 1024) / span_st, -1024, 2048);
    return q10_of(lmix(lwide, lclose, tt));
}
// A run of cycles: the phase (65536 a turn) at each grid point for a spacing
// that moves from `first` to `last` (Q10 px a cycle) with an exponential of
// length `glide` (Q10 px), wavering by `waver` (Q10 of the spacing) with a
// period of `period` (Q10 px).
inline void cycles(i32* phase, i32 first, i32 last, i32 glide, i32 waver = 0, i32 period = 16 * unit) {
    i32 p = 0;
    for (i32 j = 0; j < grid; ++j) {
        phase[j] = p;
        const i32 x = j * step;
        i32 lam = last + (((first - last) >> 5) * (fall(x, glide) >> 5) >> 5);
        if (waver) lam += ((lam >> 4) * ((waver * sn((x / (period >> 6)) * 1024)) >> 14)) >> 6;
        lam = clampi(lam, 5 * unit, 64 * unit);
        p += 524288 / (lam >> 6);   // a grid step over the spacing, in turns (Q16)
    }
}
// An amplitude that rises over `rise` and tapers to nothing at `len` (Q10 px), Q15:
// a drawn taper, (1 - x / len)^1.5 or so.
inline i32 taper(i32 x, i32 rise, i32 len) {
    if (x >= len) return 0;
    const i32 u = 1024 - (x * 64) / (len >> 4);   // Q10
    i32 a = ((u * (614 + ((u * 410) >> 10))) >> 10) * 32;
    if (rise > 0 && x < rise) a = ((a >> 5) * ((x * 1024) / rise)) >> 5;
    return a;
}
// An integer square root.
inline i32 isqrt(i32 v) {
    if (v <= 0) return 0;
    u32 x = static_cast<u32>(v), r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return static_cast<i32>(r);
}
// A round ink dot of radius r centred at c (Q10 px) at x, drawn `tall` (Q10 of round).
inline i32 dot_at(i32 x, i32 c, i32 r, i32 tall) {
    const i32 d = x - c;
    if (d <= -r || d >= r) return 0;
    return (isqrt(r * r - d * d) * tall) >> 10;
}
// A stripe: a pulse high for `duty` of a cycle (Q15), its edges a pen's width; Q14.
inline i32 stripe(u32 p, i32 duty) {
    const i32 pos = static_cast<i32>(p & 0xffffu) / 2;   // 0..32767
    const i32 inside = duty - pos;
    const i32 edge = 2600;
    if (inside <= 0) return 0;
    const i32 e = inside < pos ? inside : pos;
    return e >= edge ? 16384 : (e * 16384) / edge;
}
inline void raise(i32& h, i32 v) { if (v > h) h = v; }

// Each model's drawing (h, Q10 px, before INK), from its controls (t, Q10).
inline void sketch(u32 m, const i32* t, i32 tune, u32 seed, i32* h) {
    i32 phase[grid];
    const i32 f10 = full >> 10;   // 16
    for (i32 j = 0; j < grid; ++j) h[j] = 0;
    switch (m) {
    case M::SWEEP: {
        // sine waves inked further and further apart, tapering; a tick at the start
        const i32 last = spacing(t[0], tune, L(27), L(12), 21);
        const i32 first = clampi((last * 1024) / (1024 + ((t[2] * 13) >> 3)), 5 * unit, last);
        cycles(phase, first, last, (q10_of(lmix(L(6), L(22), t[1])) * (1024 + t[2] / 2)) >> 10);
        const i32 len = q10_of(lmix(L(26), L(220), t[1]));
        // CLICK: an inked tick ahead of the waves, which start after it
        const i32 lead = t[4] ? 6 + ((t[4] * 8) >> 10) : 0;   // grid points the tick and its gap take
        for (i32 j = 0; j < grid; ++j) {
            const i32 k = j - lead;
            h[j] = k < 0 ? 0 : bead(taper(k * step, 0, len), wave(phase[k], t[3]));
        }
        if (t[4]) {
            const i32 tall = f10 * (250 + ((t[4] * 900) >> 10));
            const i32 wide = 1 + ((t[4] * 3) >> 10);
            // an inked stroke: its pen rises and lifts over a half pixel each side
            raise(h[1], tall / 2);
            for (i32 j = 2; j <= 2 + wide; ++j) raise(h[j], tall);
            raise(h[3 + wide], tall / 2);
        }
        break;
    }
    case M::SCRATCH: {
        // two drawn tones (beads), bent by SNAP, and scratches through the
        // emulsion: fine to coarse by GRAIN, as many as SCRATCH
        const i32 last = spacing(t[0], tune, L(22), L(10), 22);
        const i32 first = (last * 1024) / (1024 + ((t[3] * 3) >> 1));
        cycles(phase, first, last, 5 * unit);
        const i32 len_b = q10_of(lmix(L(16), L(90), t[1]));
        const i32 len_s = q10_of(lmix(L(22), L(200), t[1]));
        const i32 body = 32768 - t[2] * 15;                  // fewer tones as the scratches take over
        const i32 hold = 1 + ((t[4] * 11) >> 10);            // grid points a scratch holds
        const i32 deep = 18000 + ((t[4] * 14000) >> 10);     // coarse scratches cut deeper, Q15
        for (i32 j = 0; j < grid; ++j) {
            const i32 x = j * step;
            const i32 b = bead(((taper(x, 0, len_b) >> 5) * (body >> 5)) >> 5, sn(phase[j]));
            const i32 n = iabs(hashed(static_cast<u32>(j / hold), seed)) * 2;   // 0..32767
            const i32 as = ((taper(x, 0, len_s) >> 5) * t[2]) >> 5;            // Q15
            const i32 sc = ((((as >> 5) * ((11000 + ((n * 21) >> 5)) >> 5)) >> 5) * (deep >> 5)) >> 10;  // Q15
            h[j] = b + (((sc >> 5) * f10 * 26) >> 5);
        }
        break;
    }
    case M::RING: {
        // drawn sines bending a little into PITCH, an inked edge (SLAP) at the attack
        const i32 last = spacing(t[0], tune, L(28), L(9), 43);
        const i32 first = (last * 1024) / (1024 + ((t[2] * 3) >> 2));
        cycles(phase, first, last, 12 * unit);
        const i32 len = q10_of(lmix(L(22), L(220), t[1]));
        // SLAP: an inked edge, a block ahead of the waves, longer and taller as it rises
        const i32 wide = t[4] ? 1 + ((t[4] * 11) >> 10) : 0;
        const i32 lead = wide ? wide + 7 : 0;
        for (i32 j = 0; j < grid; ++j) {
            const i32 k = j - lead;
            h[j] = k < 0 ? 0 : bead(taper(k * step, 0, len), wave(phase[k], t[3]));
        }
        if (wide) {
            const i32 tall = f10 * (420 + ((t[4] * 560) >> 10));
            // an inked stroke: its pen rises and lifts over a half pixel each side
            raise(h[1], tall / 2);
            for (i32 j = 2; j <= 2 + wide; ++j) raise(h[j], tall);
            raise(h[3 + wide], tall / 2);
        }
        break;
    }
    case M::STROKES: {
        // STROKES quick strokes, SPREAD apart (SCATTER unevens them), then the smear
        const i32 n = 1024 + t[2] * 5;                       // 1..6 strokes, Q10
        const i32 gap = q10_of(lmix(L(3.5), L(12), t[3]));
        const i32 hold = 1 + (((1024 - t[0]) * 3) >> 10);    // the grain: finer for a higher band
        i32 at = unit, last_at = unit;
        for (i32 k = 0; k * 1024 < n; ++k) {
            const i32 share = n - k * 1024 >= 1024 ? 1024 : n - k * 1024;
            const i32 jit = (hashed(static_cast<u32>(k), seed ^ 77u) * t[4]) >> 14;    // -1024..1024 of the scatter
            const i32 c = at + (((gap >> 4) * jit) >> 11) * 7;
            const i32 tall_frac = 720 + ((((hashed(static_cast<u32>(k), seed ^ 9u) * t[4]) >> 14) * 300) >> 10);
            const i32 tall = (((f10 * tall_frac) * share) >> 10);
            for (i32 j = 0; j < grid; ++j) {
                const i32 d = iabs(j * step - c);
                if (d > 2 * unit) continue;
                const i32 g = iabs(hashed(static_cast<u32>(j / hold), seed ^ static_cast<u32>(k * 31))) >> 4;   // 0..1023
                raise(h[j], ((((tall * ((2048 - d) >> 1)) >> 10) * (512 + g / 2)) >> 10));
            }
            last_at = c;
            at += gap;
        }
        const i32 len = q10_of(lmix(L(12), L(120), t[1]));
        for (i32 j = 0; j < grid; ++j) {
            const i32 x = j * step - last_at;
            if (x <= 0) continue;
            const i32 g = iabs(hashed(static_cast<u32>(j / hold), seed ^ 5u)) >> 4;
            const i32 v = ((f10 * 400) * (fall(x, len) >> 5)) >> 10;
            raise(h[j], (v * (400 + g / 2)) >> 10);
        }
        break;
    }
    case M::DOT: {
        // an inked dot SIZE across and ECHO dots after it, GAP apart, each
        // smaller; each rings (PITCH, DECAY) in a run of small beads after it
        const i32 r0 = q10_of(lmix(L(4), L(13.5), t[2]));
        const i32 gap = q10_of(lmix(L(6), L(18), t[4]));
        const i32 n = t[3] * 6;                              // echoes, Q10
        const i32 lam = spacing(t[0], tune, L(9), L(5), 47);
        const i32 ring = q10_of(lmix(L(4), L(40), t[1]));
        i32 size = 1024, at = 0;
        for (i32 k = 0; k <= 6; ++k) {
            i32 share = 1024;
            if (k > 0) {
                const i32 left = n - (k - 1) * 1024;
                if (left <= 0) break;
                share = left >= 1024 ? 1024 : left;
            }
            const i32 r = (((r0 * size) >> 10) * (410 + ((share * 614) >> 10))) >> 10;   // an echo grows in as ECHO turns
            if (r < 300) break;
            const i32 c = r0 + 512 + at;
            // its ring runs on until the next dot, if there is one
            const i32 next_at = at + (((r0 + gap) * (((size * 840) >> 10) + 60)) >> 10);
            const i32 stop = k < 6 && n > k * 1024 ? r0 + 512 + next_at - (((r * 3) >> 2) + 1024) : 1 << 20;
            for (i32 j = 0; j < grid; ++j) {
                const i32 x = j * step, d = x - c;
                raise(h[j], (dot_at(x, c, r, 1700) * (k ? 300 + ((share * 724) >> 10) : 1024)) >> 10);
                if (d > r && x < stop) {
                    // its ring: small beads at the pitch's spacing
                    const i32 a = fall(d - r, ring);
                    if (a > 400) {
                        // smaller than the dot: at most two thirds of its height
                        const i32 top = (r * 1700 * 2 / 3) >> 10;
                        const i32 wv = 16384 - sn((((d - r) * 64) / (lam >> 4)) * 16);   // 0..32768
                        raise(h[j], (((top * (a >> 5)) >> 10) * (wv >> 5)) >> 10);
                    }
                }
            }
            size = (size * 840) >> 10;
            at += ((r0 + gap) * (size + 60)) >> 10;   // the next dot, closer as they shrink
        }
        break;
    }
    case M::CARD: {
        // striped cards: two rows of stripes, RATIO apart in spacing, WIDTH
        // their share of a cycle, BEND a squeeze into the note
        const i32 last = spacing(t[0], tune, L(26), L(9), 43);
        const i32 first = (last * 1024) / (1024 + ((t[4] * 3) >> 1));
        cycles(phase, first, last, 14 * unit);
        const i32 ratio = 1024 + t[2];                                 // 1..2, Q10
        // 5%..50% of a cycle (Q15), never under a pen's width of 1.6 px
        const i32 thinnest = (1638 * 32 * 32) / (last >> 5);
        const i32 duty = clampi(1638 + ((t[3] * (16384 - 1638)) >> 10), thinnest + ((t[3] * 2000) >> 10), 16384);
        const i32 len = q10_of(lmix(L(22), L(220), t[1]));
        for (i32 j = 0; j < grid; ++j) {
            const u32 pa = static_cast<u32>(phase[j]);
            const u32 pb = static_cast<u32>(((phase[j] >> 4) * ratio) >> 6);
            const i32 s = (stripe(pa, duty) + stripe(pb, duty)) / 2;   // Q14, 0..1
            const i32 a = taper(j * step, 0, len);
            h[j] = (((a >> 5) * (s >> 4)) >> 10) * f10;
        }
        break;
    }
    case M::HATCH: {
        // ruled lines across the track: teeth at the pitch's spacing, METAL
        // from scratched (uneven) to ruled (even), EDGE how sharp, SPREAD how
        // irregular their spacing; DECAY closed (a few) to open (a long run)
        const i32 lam = spacing(t[0], tune, L(10), L(5.5), 28);
        const i32 len = q10_of(lmix(L(16), L(220), (t[1] * t[1]) >> 10));
        const i32 sharp = 1 + ((t[3] * 3) >> 10);
        i32 p = 0;
        for (i32 j = 0; j < grid; ++j) {
            const i32 tooth = p >> 16;
            const i32 jl = (lam * (1024 + (((hashed(static_cast<u32>(tooth), seed ^ 3u) * t[4]) >> 15) * 7) / 10)) >> 10;
            const i32 f = p & 0xffff;
            const i32 tri = 16384 - iabs(f - 32768) / 2;            // 0..16384, its peak mid-cycle
            i32 v = tri;
            for (i32 k = 1; k < sharp; ++k) v = (v * tri) >> 14;
            const i32 uneven = iabs(hashed(static_cast<u32>(tooth), seed)) / 2 + 6000;
            const i32 tall = uneven + (((16384 - uneven) * t[2]) >> 10);
            const i32 vt = (v * tall) >> 14;
            h[j] = (((vt >> 4) * (fall(j * step, len) >> 5)) >> 10) * f10;
            p += (524288 / (jl > 4096 ? jl : 4096)) * 64;
        }
        break;
    }
    case M::ORNAMENT: {
        // interlaced painted rings: two (to four, RINGS) waves woven, a bell
        // (BELL) struck at the start, a bloom (SWELL) and a long decay
        const i32 lam = spacing(t[0], tune, L(18), L(8), 24);
        const i32 len = q10_of(lmix(L(30), L(320), t[1]));
        const i32 rise = t[4] ? q10_of(lmix(L(2), L(40), t[4])) : 0;
        const i32 r = t[2];
        const i32 rb = t[3] ? 1024 + t[3] * 6 : 0;            // BELL: a round ping ahead of the wash
        const i32 lead = rb ? (2 * rb + 2048) / step : 0;
        i32 p1 = 0;
        for (i32 j = 0; j < grid; ++j) {
            if (j < lead) { h[j] = 0; continue; }
            const i32 s1 = cs(p1), s2 = cs(((p1 >> 4) * 27) >> 4), s3 = sn(((p1 >> 4) * 11) >> 4), s4 = cs(((p1 >> 4) * 22) >> 4);
            const i32 w = (((s1 >> 1) * (s2 >> 1)) >> 12) + (((((s3 >> 1) * (s4 >> 1)) >> 12) * r) >> 10);
            const i32 s = clampi((w * 1024) / (1024 + r / 2), -16384, 16384);
            h[j] = bead(taper((j - lead) * step, rise, len), s);
            p1 += 524288 / (lam >> 6);
        }
        if (rb) for (i32 j = 0; j < lead; ++j) raise(h[j], dot_at(j * step, rb + 768, rb, 1500));
        break;
    }
    case M::STIPPLE: {
        // stippled dots, DENSITY of them; JINGLE gathers them into clusters;
        // SWELL eases them in; PITCH how fine each dot is
        const i32 count = 4 + ((t[2] * 26) >> 10);
        const i32 wide = 768 + (1024 - t[0]);               // a dot's half width, Q10 px
        const i32 len = q10_of(lmix(L(20), L(200), t[1]));
        const i32 rise = q10_of(lmix(L(1), L(30), t[3]));
        for (i32 k = 0; k < count; ++k) {
            // evenly scattered, or bunched into groups of four (JINGLE)
            const i32 u = (k * 64 * 1024) / count;
            const i32 cluster = ((k / 4) * 4 * 64 * 1024) / count + (k % 4) * 3 * unit / 2;
            const i32 jitter = (hashed(static_cast<u32>(k), seed) * 3) >> 4;   // +-3 px
            const i32 c = unit + clampi(u + (((cluster - u) >> 4) * t[4] >> 6) + ((jitter * (1024 - t[4])) >> 10), 0, 64 * unit);
            const i32 tall = f10 * (420 + (iabs(hashed(static_cast<u32>(k), seed ^ 11u)) >> 5));
            const i32 a = taper(c, rise, len);
            for (i32 j = 0; j < grid; ++j) {
                const i32 d = iabs(j * step - c);
                if (d >= wide) continue;
                raise(h[j], ((((tall >> 5) * (a >> 5)) >> 10) * (((wide - d) * 1024) / wide)) >> 5);
            }
        }
        break;
    }
    case M::BOOP: {
        // a tuned drawn tone sliding into its note (SLIDE: from below at 0,
        // above at 127), its edges by SHAPE, a waver (VIBRATO), a soft start
        const i32 last = spacing(t[0], tune, L(28), L(8), 60);
        const i32 slide = t[2] - 512;   // -512..512
        const i32 first = slide >= 0 ? (last * 1024) / (1024 + slide * 2) : (last * (1024 - slide * 2)) >> 10;
        cycles(phase, clampi(first, 5 * unit, 60 * unit), last, 10 * unit, (t[4] * 5) >> 3, 20 * unit);
        const i32 len = q10_of(lmix(L(22), L(220), t[1]));
        for (i32 j = 0; j < grid; ++j) h[j] = bead(taper(j * step, 3 * unit, len), wave(phase[j], t[3]));
        break;
    }
    case M::WAVE: {
        // Oram's drawn bass: a wave of SHAPE whose edges round over the note
        // (SWEEP: sharper at the start), every other cycle taller with SUB
        const i32 lam = spacing(t[0], tune, L(30), L(12), 36);
        cycles(phase, lam, lam, unit);
        const i32 len = q10_of(lmix(L(24), L(260), t[1]));
        const i32 sweep = q10_of(lmix(L(40), L(8), t[3]));
        const i32 start = t[2] + (((1024 - t[2]) * t[3]) >> 10);
        const i32 end = (t[2] * (1024 - t[3] / 2)) >> 10;
        for (i32 j = 0; j < grid; ++j) {
            const i32 x = j * step;
            const i32 shape = end + (((start - end) * (fall(x, sweep) >> 5)) >> 10);
            i32 a = taper(x, 2 * unit, len);
            if ((phase[j] >> 16) & 1) a = ((a >> 5) * (1024 - ((t[4] * 9) >> 4))) >> 5;   // SUB: the octave under
            h[j] = bead(a, wave(phase[j], shape));
        }
        break;
    }
    default: {   // CLAW
        // the claw's ticks, RATE a second: narrow strokes along the track,
        // swelling or fading by SWELL, rough by GRIT, for DECAY's length
        const i32 lam = q10_of(lmix(L(18), L(4.5), t[2]));
        const i32 end = q10_of(lmix(L(14), L(66), t[1]));
        const i32 wide = 700 + (((1024 - t[0]) * 2560) >> 10);   // a tick's half width: broad and low, narrow and high
        const i32 sw = t[3] * 2 - 1024;                          // -1..1
        i32 k = 0;
        for (i32 c = unit; c < end && k < 24; c += lam, ++k) {
            // the ink: fading, steady or swelling along the roll
            const i32 u = ((c - end / 2) * 64) / ((end >> 4) > 0 ? end >> 4 : 1);   // -512..512
            const i32 lv = clampi(600 + ((sw * u * 13) >> 13), 90, 1024);
            const i32 g = (hashed(static_cast<u32>(k), seed) * t[4]) >> 15;   // GRIT: rougher heights
            const i32 tall = f10 * clampi(lv + g, 80, 1024);
            for (i32 j = 0; j < grid; ++j) {
                const i32 d = iabs(j * step - c);
                if (d >= wide) continue;
                i32 v = (tall * (((wide - d) * 1024) / wide)) >> 10;
                if (t[4]) v = (v * (1024 - ((iabs(hashed(static_cast<u32>(j), seed ^ 21u)) * t[4]) >> 15))) >> 10;
                raise(h[j], v);
            }
        }
        break;
    }
    }
}

// INK: a thin ink (up to a sixth of its travel) is drawn paler (ink_shade);
// from there a heavier one is drawn taller (0.9 to 1.9 times) and cut off
// softly at the track's edges, which it spills past by up to 1.6 px.
const i32 ink_full = 169;   // Q10: INK 21, where the ink stops bleeding
inline i32 inked(i32 h, i32 t) {
    const i32 ink = t <= ink_full ? 0 : ((t - ink_full) * 1226) >> 10;   // 0..1024 over the rest
    const i32 y = (h * (922 + ink)) >> 10;
    const i32 top = 24 * unit + ((ink * 1640) >> 10);
    const i32 knee = (top * 3) >> 2, span = top >> 1;
    if (y <= knee) return y;
    const i32 d = y - knee;
    if (d >= span) return top;
    return knee + d - (((d >> 4) * (d >> 4)) / (top >> 8));
}

// The finished drawing as an outline: the profile inked, bitten by DUST,
// its slopes held to a pen's (so no point turns straight back), never thinner
// than `thin`; then 32 points along it, chosen where they keep the most of
// its shape, the top edge left to right and the bottom edge back.
inline void outline(const i32* t, i32 tune, u32 m, u32 seed, Frame::path_type& path) {
    i32 h[grid];
    sketch(m, t, tune, seed, h);
    for (i32 j = 0; j < grid; ++j) h[j] = inked(h[j], t[5]);
    if (t[7]) {
        // DUST: specks bite the drawing, more and deeper as it rises
        const i32 n = t[7] * 5;   // Q10 bites
        for (i32 k = 0; k * 1024 < n; ++k) {
            const i32 share = n - k * 1024 >= 1024 ? 1024 : n - k * 1024;
            const i32 c = 6 + static_cast<i32>((static_cast<u32>(hashed(static_cast<u32>(k), seed ^ 0x51u) + 16384) * 120u) >> 15);
            const i32 depth = ((300 + ((t[7] * 400) >> 10)) * share) >> 10;   // Q10 of the height
            for (i32 j = c - 3; j <= c + 3; ++j) {
                if (j < 0 || j >= grid) continue;
                const i32 cut = (depth * (4 - iabs(j - c))) >> 2;
                h[j] = (h[j] * (1024 - cut)) >> 10;
            }
        }
    }
    // a pen's slope: at most 4 px a half pixel either way
    const i32 most = 10 * unit;
    for (i32 j = 1; j < grid; ++j) if (h[j] > h[j - 1] + most) h[j] = h[j - 1] + most;
    for (i32 j = grid - 2; j >= 0; --j) if (h[j] > h[j + 1] + most) h[j] = h[j + 1] + most;
    for (i32 j = 0; j < grid; ++j) if (h[j] < thin) h[j] = thin;
    h[0] = h[grid - 1] = thin;
    // choose 32 grid points: the ends, then wherever the line so far strays most
    unsigned char pick[grid];
    for (i32 j = 0; j < grid; ++j) pick[j] = 0;
    pick[0] = pick[grid - 1] = 1;
    for (i32 n = 2; n < 32; ++n) {
        i32 best = -1, worst = -1, a = 0;
        for (i32 j = 1; j < grid; ++j) {
            if (!pick[j]) continue;
            // the segment a..j: its interior's distance from the chord
            for (i32 k = a + 1; k < j; ++k) {
                const i32 line_h = h[a] + ((h[j] - h[a]) * (k - a)) / (j - a);
                const i32 dev = iabs(h[k] - line_h);
                if (dev > worst) { worst = dev; best = k; }
            }
            a = j;
        }
        if (best < 0) {
            // nothing strays: the widest gap's middle
            i32 gap = 0;
            a = 0;
            for (i32 j = 1; j < grid; ++j) {
                if (!pick[j]) continue;
                if (j - a > gap && j - a > 1) { gap = j - a; best = (a + j) / 2; }
                a = j;
            }
        }
        if (best < 0) break;
        pick[best] = 1;
    }
    i32 at = 0;
    for (i32 j = 0; j < grid && at < 32; ++j) {
        if (!pick[j]) continue;
        const i32 x = j * step - 33 * unit;
        path.points[at].x = x;
        path.points[at].y = -h[j];
        path.points[63 - at].x = x;
        path.points[63 - at].y = h[j];
        ++at;
    }
}

// ---------------------------------------------------------------- captions
inline void number(char* out, i32 v, const char* unit_) {
    char t[12];
    u32 n = 0;
    if (v < 0) { t[n++] = '-'; v = -v; }
    if (v > 9999) v = 9999;
    if (v >= 1000) t[n++] = static_cast<char>('0' + v / 1000);
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    if (v >= 10) t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_[i] && n < 11; ++i) t[n++] = unit_[i];
    t[n] = 0;
    copy(out, header_capacity + 1, t);
}
// tenths as "1.3" with a unit
inline void tenths(char* out, i32 v, const char* unit_) {
    char t[12];
    u32 n = 0;
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_[i] && n < 11; ++i) t[n++] = unit_[i];
    t[n] = 0;
    copy(out, header_capacity + 1, t);
}
// hundredths as "0.29" with a unit
inline void hundredths(char* out, i32 v, const char* unit_) {
    char t[12];
    u32 n = 0;
    t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit_[i] && n < 11; ++i) t[n++] = unit_[i];
    t[n] = 0;
    copy(out, header_capacity + 1, t);
}
// A time in Q16 seconds as "40ms", "0.25s" or "1.4s".
inline void seconds(char* out, u32 s) {
    const u32 msv = (s * 125u + 4096u) >> 13;   // s * 1000 / 65536
    if (msv < 100) number(out, static_cast<i32>(msv), "ms");
    else if (msv < 1000) hundredths(out, static_cast<i32>((msv + 5) / 10), "s");
    else tenths(out, static_cast<i32>((msv + 50) / 100), "s");
}
enum Kind : unsigned char { amount, note, hz, length, sqlength, time, ms10, st, slide, count, ratio, duty, fps, persec, swell, sqswell, signed_amount };
struct Caption { Kind kind; i32 la, lb; };   // log2 of the ends, Q16; counts and semitones: the ends themselves
const i32 tau40 = L(range::shown);
inline void caption(char* out, const Caption& c, i32 t, i32 tune) {   // t: Q10 unit
    switch (c.kind) {
    case note: {
        const i32 l = lmixc(c.la, c.lb, t) - L(440.0);
        const i32 key = 69 + tune + ((((l >> 4) * 3) / 256 + 128 + 69 * 256 * 0) >> 8) + 0;
        (void)key;
        // midi = 69 + 12 log2(f / 440), in 256ths of a semitone
        const i32 st256 = 69 * 256 + (((l >> 4) * 3072) >> 12) + tune * 256;
        const i32 k = (st256 + 128) >> 8;
        static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const i32 kk = k < 0 ? 0 : k;
        char s[8];
        u32 n = 0;
        for (const char* p = names[kk % 12]; *p; ++p) s[n++] = *p;
        const i32 oct = kk / 12 - 1;
        if (oct < 0) { s[n++] = '-'; s[n++] = static_cast<char>('0' - oct); } else s[n++] = static_cast<char>('0' + oct);
        s[n] = 0;
        copy(out, header_capacity + 1, s);
        return;
    }
    case hz: {
        const i32 l = lmixc(c.la, c.lb, t) + tune * 5461;
        const u32 hzv = (pow2v(l) + 32768) >> 16;
        if (hzv >= 1000) tenths(out, static_cast<i32>((hzv + 50) / 100), "k");
        else number(out, static_cast<i32>(hzv), "");
        return;
    }
    case length: seconds(out, pow2v(lmixc(c.la, c.lb, t) + tau40)); return;
    case sqlength: seconds(out, pow2v(lmixc(c.la, c.lb, (t * t) >> 10) + tau40)); return;
    case time: seconds(out, pow2v(lmixc(c.la, c.lb, t))); return;
    case ms10: {
        // a short time in tenths of a millisecond: "0.4ms" .. "2.5ms"
        const u32 s = pow2v(lmixc(c.la, c.lb, t) + 14 * 65536);   // s * 2^14, Q16
        const u32 dms = (s * 10u / 65536u * 1000u + 8192u) >> 14;  // tenths of a ms
        tenths(out, static_cast<i32>(dms), "ms");
        return;
    }
    case st: number(out, (t * c.la + 512) >> 10, "st"); return;
    case slide: {
        const i32 v = ((t - 512) * 2 * c.la + (t >= 512 ? 512 : -512)) / 1024;
        if (v > 0) { char s[12] = "+"; number(s + 1, v, "st"); copy(out, header_capacity + 1, s); }
        else number(out, v, "st");
        return;
    }
    case count: number(out, c.la + ((t * (c.lb - c.la) + 512) >> 10), ""); return;
    case ratio: hundredths(out, static_cast<i32>((pow2v(lmixc(c.la, c.lb, t)) * 100u + 32768u) >> 16), ""); return;
    case duty: number(out, 5 + ((t * 45 + 512) >> 10), "%"); return;
    case fps: number(out, static_cast<i32>((pow2v(lmixc(c.la, c.lb, t)) + 32768u) >> 16), "fps"); return;
    case persec: number(out, static_cast<i32>((pow2v(lmixc(c.la, c.lb, t)) + 32768u) >> 16), ""); return;
    case sqswell:   // a quarter second times the control's square (STIPPLE's SWELL)
        if (t < 33) copy(out, header_capacity + 1, "0");
        else seconds(out, static_cast<u32>(t * t) / 64u);
        return;
    case swell:
        if (t < 4) copy(out, header_capacity + 1, "0");
        else seconds(out, pow2v(lmixc(c.la, c.lb, t)));
        return;
    case signed_amount: {
        const i32 v = ((t * 2 - 1024) * 100 + (t >= 512 ? 512 : -512)) / 1024;
        if (v > 0) { char s[12] = "+"; number(s + 1, v, ""); copy(out, header_capacity + 1, s); }
        else number(out, v, "");
        return;
    }
    default: header_amount(out, header_capacity + 1, t >= 1024 ? 0x7fffffff : clampi(t, 0, 1023) * 2097152);
    }
}
const Caption A = {amount, 0, 0};
#define R2(k, r) {k, range::r[0], range::r[1]}
const Caption captions[models][8] = {
    {R2(note, sweep_pitch), R2(length, sweep_decay), {st, range::sweep_bend, 0}, A, A, A, A, A},
    {R2(hz, scratch_pitch), R2(length, scratch_decay), A, {st, range::scratch_snap, 0}, A, A, A, A},
    {R2(note, ring_pitch), R2(length, ring_decay), {st, range::ring_bend, 0}, A, A, A, A, A},
    {R2(hz, strokes_pitch), R2(length, strokes_decay), {count, 1, 6}, R2(time, strokes_spread), A, A, A, A},
    {R2(hz, dot_pitch), R2(length, dot_decay), R2(ms10, dot_size), {count, 0, 6}, R2(time, dot_gap), A, A, A},
    {R2(note, card_pitch), R2(length, card_decay), R2(ratio, card_ratio), {duty, 0, 0}, {st, range::card_bend, 0}, A, A, A},
    {R2(hz, hatch_pitch), R2(length, hatch_decay), A, R2(hz, hatch_edge), A, A, A, A},
    {R2(hz, ornament_pitch), R2(length, ornament_decay), A, A, R2(swell, ornament_swell), A, A, A},
    {R2(hz, stipple_pitch), R2(length, stipple_decay), R2(persec, stipple_density), R2(sqswell, stipple_swell), A, A, A, A},
    {R2(note, boop_pitch), R2(length, boop_decay), {slide, range::boop_slide, 0}, A, A, A, A, A},
    {R2(note, wave_pitch), R2(length, wave_decay), A, A, A, A, A, A},
    {R2(hz, claw_pitch), R2(time, claw_decay), R2(fps, claw_rate), {signed_amount, 0, 0}, A, A, A, A},
};
#undef R2
// as the Shift row shows them: lower case
// The Shift row's MACHINE names, each at most 53 px at the header's 20 px in
// Univers Next Pro Light (the OP-XY draws header values at full size).
const char* const names[models] = {"swp", "scrch", "ring", "strks", "dot", "card", "hatch", "orn", "stipl", "boop", "wave", "claw"};
const char* const labels[models][8] = {
    {"PITCH", "DECAY", "BEND", "SHAPE", "CLICK", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "SCRATCH", "SNAP", "GRAIN", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "BEND", "SHAPE", "SLAP", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "STROKES", "SPREAD", "SCATTER", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "SIZE", "ECHO", "GAP", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "RATIO", "WIDTH", "BEND", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "METAL", "EDGE", "SPREAD", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "RINGS", "BELL", "SWELL", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "DENSITY", "SWELL", "JINGLE", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "SLIDE", "SHAPE", "VIBRATO", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "SHAPE", "SWEEP", "SUB", "INK", "FLUTTER", "DUST"},
    {"PITCH", "DECAY", "RATE", "SWELL", "GRIT", "INK", "FLUTTER", "DUST"},
};
// The pad controls' captions: signed whole numbers, as the engine gives them.
inline void signed_text(char* out, i32 v) {
    if (v > 0) { char s[12] = "+"; number(s + 1, v, ""); copy(out, header_capacity + 1, s); }
    else number(out, v, "");
}

// Univers Next Pro Light's advance widths for printable ASCII (32 to 126),
// in thousandths of an em: the OP-XY lays out its header text by them.
static const opxy::i16 advance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560,
    560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745,
    602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589,
    574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
// A header value too wide for its column (a long model name in the Shift
// row) is drawn smaller, so it ends 53 px on, where the next column begins.
template <class F>
inline void fit(F& f, u32 slot, i32 size_px) {
    i32 em = 0;
    for (const char* c = f.texts[slot].chars; *c; ++c) em += *c >= 32 && *c <= 126 ? advance[*c - 32] : 600;
    const i32 wide = (em * size_px * (f.texts[slot].scale >> 2)) >> 8;   // thousandths of a px
    if (wide > 53000) f.texts[slot].scale = (f.texts[slot].scale * 53000) / wide;
}

// Whether `text` is the start of `name` (a name the header cut short).
inline bool begins(const char* name, const char* text) {
    u32 i = 0;
    for (; text[i]; ++i) if (text[i] != name[i]) return false;
    return i > 0;
}

// ---------------------------------------------------------------- the pull
// How long a note pulls its frame through the gate: the hit's own length,
// Q10 seconds, from its decay (the time it falls 26 dB), 0.24 s to 1.6 s.
inline i32 hit_length(u32 m, const i32* t) {
    const Caption& c = captions[m][1];
    u32 s;   // Q16 s
    if (c.kind == time) s = pow2v(lmixc(c.la, c.lb, t[1]));
    else if (c.kind == sqlength) s = pow2v(lmixc(c.la, c.lb, (t[1] * t[1]) >> 10) + L(3.0));
    else s = pow2v(lmixc(c.la, c.lb, t[1]) + L(3.0));
    if (m == M::DOT) s += static_cast<u32>((t[3] * 6) >> 10) * pow2v(lmixc(range::dot_gap[0], range::dot_gap[1], t[4]));
    if (m == M::STROKES) s += static_cast<u32>(1 + ((t[2] * 5) >> 10)) * pow2v(lmixc(range::strokes_spread[0], range::strokes_spread[1], t[3]));
    const i32 q = static_cast<i32>(s >> 6);
    return clampi(q, 246, 1638);
}
// ease in and settle: 3u^2 - 2u^3, Q10, a ratchet's pull from rest to rest
inline i32 ease(i32 u) {
    u = clampi(u, 0, 1024);
    return (((u * u) >> 10) * (3072 - 2 * u)) >> 10;
}

// ---------------------------------------------------------------- the slat
// A point on the film (x along it, y across it, Q10 film px) on the screen.
inline i32 screen_x(i32 x, i32 y) { return art::iso_x + (((x - y) * art::cos30) >> 10); }
inline i32 screen_y(i32 x, i32 y) { return art::iso_y + ((x + y) >> 1); }
// A face-on offset (Q10 px: x along the drawing, y across it) laid on the
// track: 46 film px for the drawing's 66, 12 for each 24 of its height, then
// isometric. Affine, so the outline stays the outline.
inline i32 laid_x(i32 x, i32 y) { return (x * art::along_x - y * art::across_x) >> 10; }
inline i32 laid_y(i32 x, i32 y) { return (x * art::along_y + y * art::across_y) >> 10; }
inline void lay(Frame::path_type& path) {
    for (u32 i = 0; i < 64; ++i) {
        const i32 x = path.points[i].x, y = path.points[i].y;
        path.points[i].x = laid_x(x, y);
        path.points[i].y = laid_y(x, y);
    }
}

// The taper the pencil guides rule: how far along a frame's drawing (Q10 px
// of the face-on 66) its envelope runs out, from DECAY. A taper's guides meet
// where it ends; an exponential's run to twice its length, which bounds it.
struct Envelope { i32 lo, hi; unsigned char kind; };   // kind: 0 a taper, 1 exponential, 2 exponential on DECAY squared
const Envelope envelopes[models] = {
    {L(26), L(220), 0},   // SWEEP
    {L(22), L(200), 0},   // SCRATCH: its scratches
    {L(22), L(220), 0},   // RING
    {L(12), L(120), 1},   // STROKES: the smear
    {L(6), L(40), 1},     // DOT: its rings
    {L(22), L(220), 0},   // CARD
    {L(16), L(220), 2},   // HATCH
    {L(30), L(320), 0},   // ORNAMENT
    {L(20), L(200), 0},   // STIPPLE
    {L(22), L(220), 0},   // BOOP
    {L(24), L(260), 0},   // WAVE
    {L(14), L(66), 0},    // CLAW: where its ticks stop
};
inline i32 envelope_end(u32 m, i32 decay) {
    const Envelope& e = envelopes[m];
    const i32 len = q10_of(lmixc(e.lo, e.hi, e.kind == 2 ? (decay * decay) >> 10 : decay));
    return e.kind ? 2 * len : len;
}
// The weights INK gives the ink and the guides: palette entries 1..6 and 7..9.
inline u32 ink_weight(i32 raw) { return raw < 8 ? 1u : raw < 16 ? 2u : raw < 24 ? 3u : raw < 56 ? 4u : raw < 96 ? 5u : 6u; }
inline u32 guide_weight(i32 raw) { return raw < 24 ? 7u : raw < 80 ? 8u : 9u; }
const u32 frame_shade = 10, frame_faded = 11, ink_faded = 12;   // 12..14, dimmer and dimmer
}

using namespace view;

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 24;
    state.from = state.to = 0;
    state.span = 1024;
    state.drawn = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const u32 m = input.model < models ? input.model : 0;
    i32 t[8], raw[8];
    for (u32 i = 0; i < 8; ++i) {
        const u32 r = input.slots[i] < 0 ? 0u : static_cast<u32>(input.slots[i]) >> 24;
        raw[i] = static_cast<i32>(r > 127 ? 127 : r);
        t[i] = (raw[i] * 8257) >> 10;   // Q10, 127 -> 1024
    }
    const i32 tune = clampi(input.pad[0], -16, 15);
    const i32 slit = clampi(input.pad[1], -8, 7);
    const bool still = input.reduced_motion != 0;

    // --- the pull: the selected key's notes only (the platform's serial is the key's)
    const i32 dt = clampi(input.elapsed, 0, 1024);
    const bool baseline = !state.initialized || state.track != input.track;
    const bool fresh = !baseline && input.note_serial != state.serial;
    state.serial = input.note_serial;
    state.track = input.track;
    state.initialized = 1;
    if (baseline) {
        state.age = 1 << 24;
        state.from = state.to = 0;
    } else if (fresh && !still) {
        // pull one more frame on from where the film is now
        const i32 now = state.age >= state.span ? state.to : state.from + (((state.to - state.from) * ease((state.age * 1024) / state.span)) >> 10);
        const i32 whole = (now / art::pitch) * art::pitch;
        state.from = now - whole;
        state.to = state.to - whole + art::pitch;
        if (state.to - state.from > 2 * art::pitch) state.to -= art::pitch;
        // a pull of more than a frame (notes in quick succession) takes longer
        state.span = (hit_length(m, t) * ((state.to - state.from) >> 6)) / (art::pitch >> 6);
        state.age = 0;
    } else {
        state.age = clampi(state.age + dt, 0, 1 << 24);
    }
    i32 pos;
    if (state.age >= state.span) {
        pos = state.to;
        if (state.from != state.to) { state.to %= art::pitch; state.from = state.to; pos = state.to; }
    } else {
        pos = state.from + (((state.to - state.from) * ease((state.age * 1024) / state.span)) >> 10);
    }
    // the film steps in quarter pixels, so its last slow quarter pixels
    // change the screen only when they show
    pos = (pos >> 8) * 256;
    const i32 off = pos % art::pitch;   // 0..pitch: the film moves back along the slat as it grows

    // --- the drawing: four frames' worth, the same in every frame; reshaped
    // only when a control changes
    u32 seed = 2166136261u;
    for (u32 i = 0; i < 8; ++i) seed = (seed ^ static_cast<u32>(raw[i])) * 16777619u;
    seed = (seed ^ m) * 16777619u;
    u32 key = (seed ^ static_cast<u32>(tune + 64)) * 16777619u;
    if (!key) key = 1;
    if (frame.paths[0].key != key) {
        outline(t, tune, m, seed, frame.paths[0]);
        lay(frame.paths[0]);
        frame.paths[0].key = key;
        for (u32 p = 1; p < 4; ++p) {
            for (u32 i = 0; i < 64; ++i) frame.paths[p].points[i] = frame.paths[0].points[i];
            frame.paths[p].key = key + p;
        }
    }
    const u32 ink = ink_weight(raw[5]);

    // --- the frames: slot k's frame starts k pitches along the slat, less
    // the pull. Slot 0's frame goes back over the near end; halfway through
    // the pull it comes round to the far end, where the next frame comes up
    // over the edge. It dims into the dark as it crosses: by half a pitch it
    // is at its darkest, at both ends, where it changes over.
    const i32 half = art::pitch / 2;
    const i32 far = iabs(off - half);   // 0 at the changeover .. half at rest
    const u32 fade = far >= (half * 3) / 4 ? 0u : far >= half / 2 ? 1u : far >= half / 4 ? 2u : 3u;
    for (u32 k = 0; k < 4; ++k) {
        const i32 start = k ? static_cast<i32>(k) * art::pitch - off : off <= half ? -off : 4 * art::pitch - off;
        const u32 dim = k ? 0u : fade;
        // the drawing: path p is slot p + 1, so path 0 never comes round
        const u32 p = (k + 3) & 3u;
        place(frame, p, screen_x(start + art::track_x, art::track_y), screen_y(start + art::track_x, art::track_y), unit,
              dim ? ink_faded + dim - 1 : ink, stroke);
        place(frame, art::pic_0 + k, screen_x(start + art::picture_x, art::picture_y), screen_y(start + art::picture_x, art::picture_y), unit,
              dim < 2 ? frame_shade : frame_faded, stroke);
        place(frame, art::perf_0 + k, screen_x(start + art::perf_x, art::perf_y), screen_y(start + art::perf_x, art::perf_y), unit,
              dim < 2 ? frame_shade : frame_faded, stroke);
        // DUST: two specks on each frame, the same on every frame, decided
        // once; the first comes with any DUST and grows, the second joins
        // from 48
        for (u32 j = 0; j < 2; ++j) {
            const u32 n = art::speck_00 + 2 * k + j;
            const i32 from_ = j ? 48 : 1;
            if (raw[7] < from_ || dim) { hide(frame, n); continue; }
            const i32 x = start + art::speck_x[j], y = art::speck_y[j];
            place(frame, n, screen_x(x, y), screen_y(x, y), 560 + (raw[7] - from_) * (j ? 6 : 5), 0, keep);
        }
    }

    // --- the pencil guides, ruled on the slat where each frame comes to
    // rest: from the drawing's start at the ink's full height (INK's taller
    // ink widens them) to where DECAY's taper runs out, or on to the frame's
    // end. Each is one segment, or two end to end or overlapping.
    {
        const i32 top = inked(full, t[5]);
        const i32 end = envelope_end(m, t[1]);
        const i32 span_ = 66 * unit;
        i32 tx, ty;   // the top guide's run, face on, from (-33 px, -top)
        if (end <= span_) { tx = clampi(end, 2 * unit, span_); ty = top; }
        else { tx = span_; ty = (top * ((span_ * 16) / (end >> 6))) >> 10; }
        const u32 weight = guide_weight(raw[5]);
        for (u32 side = 0; side < 2; ++side) {
            const i32 sy = side ? top : -top, dy = side ? -ty : ty;
            const i32 vx = laid_x(tx, dy), vy = laid_y(tx, dy);
            const i32 len6 = isqrt((vx >> 4) * (vx >> 4) + (vy >> 4) * (vy >> 4));   // Q6
            const i32 len = len6 > 0 ? len6 * 16 : 1;
            const Turn way = {len6 > 0 ? ((vx >> 4) * unit) / len6 : unit, len6 > 0 ? ((vy >> 4) * unit) / len6 : 0};
            const i32 L0 = art::guide_length;
            i32 scale, second;   // the segments' scale; how far along the second starts (-1: none)
            if (len <= L0) { scale = (len * 16) / (L0 >> 6); second = -1; }
            else if (len <= 2 * L0) { scale = unit; second = len - L0; }
            else { scale = (len * 8) / (L0 >> 6); second = len / 2; }
            scale = clampi(scale, 64, 2 * unit);
            for (u32 k = 0; k < 4; ++k) {
                const i32 cx = static_cast<i32>(k) * art::pitch + art::track_x;
                const i32 x0 = screen_x(cx, art::track_y) + laid_x(-33 * unit, sy), y0 = screen_y(cx, art::track_y) + laid_y(-33 * unit, sy);
                const u32 a = art::guide_0ta + 4 * k + 2 * side;
                place(frame, a, x0, y0, scale, weight, stroke);
                turn(frame, a, way);
                if (second < 0) { hide(frame, a + 1); continue; }
                place(frame, a + 1, x0 + ((way.cos * second) >> 10), y0 + ((way.sin * second) >> 10), scale, weight, stroke);
                turn(frame, a + 1, way);
            }
        }
    }

    // --- the gate: FLUTTER lifts its bar up its posts, away from the film
    // (a gate that no longer holds the film steady), and a lifted bar
    // chatters while the film is pulled, dying away as the pull settles
    {
        i32 chatter = 0;
        if (t[6] && state.age < state.span) {
            const i32 left = ((state.span - state.age) * 1024) / state.span;   // Q10
            const i32 amp = (t[6] * 3) >> 1;                                   // up to 1.5 px
            chatter = (((amp * sn(state.age * 448)) >> 14) * left) >> 10;
            chatter = (chatter >> 9) * 512;   // in half pixels
        }
        const i32 x = screen_x(art::gate, art::bar_y);
        const i32 y = screen_y(art::gate, art::bar_y) - art::bar_z - ((t[6] * art::lift) >> 10) - chatter;
        place(frame, art::bar_left, x, y, unit, 0, keep);
        place(frame, art::bar_right, x, y, unit, 0, keep);
        place(frame, art::bar_top, x, y, unit, 0, keep);
    }
    // SLIT: the light's line across the track, wider as the slit widens
    for (i32 k = 0; k < 16; ++k) {
        if (k == slit + 8) place(frame, art::slit_0 + static_cast<u32>(k), screen_x(art::gate, art::track_y), screen_y(art::gate, art::track_y), unit, 0, keep);
        else hide(frame, art::slit_0 + static_cast<u32>(k));
    }

    // --- the header: both rows, the edited one bright; while Shift is held
    // both rows show it: the key's mixer channel (MACHINE, CHOKE, PAN, LEVEL)
    // over its pad controls and sends (TUNE, SLIT, FX I, FX II)
    const char* titles[8];
    char values[8][header_capacity];
    char text_[header_capacity + 1];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = labels[m][i];
        caption(text_, captions[m][i], t[i], i == 0 ? tune : 0);
        copy(values[i], header_capacity, text_);
    }
    if (input.shift) {
        static const char* const chokes[4] = {"none", "1", "2", "3"};
        char pan[header_capacity + 1], level[header_capacity + 1], sends[2][header_capacity + 1], tune_[header_capacity + 1], slit_[header_capacity + 1];
        number(pan, input.pan, "");
        number(level, input.level, "dB");
        for (u32 k = 0; k < 2; ++k) {
            const i32 code = input.pad[2 + k];
            if (code > 0) number(sends[k], ((code > 7 ? 7 : code) - 1) * 100 / 6 + (code == 3 || code == 6 ? 1 : 0), "");
            else copy(sends[k], header_capacity + 1, "TRK");
        }
        signed_text(tune_, tune);
        signed_text(slit_, slit);
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"TUNE", "SLIT", "FX I", "FX II"}};
        const char* const shift_values[2][4] = {{names[m], chokes[input.choices[1] & 3u], pan, level}, {tune_, slit_, sends[0], sends[1]}};
        for (u32 line = 0; line < 2; ++line)
            for (u32 c = 0; c < 4; ++c) {
                titles[4 * line + c] = shift_titles[line][c];
                copy(values[4 * line + c], header_capacity, shift_values[line][c]);
            }
    }
    page_header(state.header, input, frame, titles, values, art::header_node);
    if (input.shift) {
        // a model name longer than a header value holds (ORNAMENT) is written in full
        for (u32 slot = 4; slot <= 12; slot += 8)
            if (begins(names[m], frame.texts[slot].chars)) copy(frame.texts[slot].chars, text_capacity, names[m]);
    }
    for (u32 c = 0; c < 4; ++c) { fit(frame, 4 + c, 20); fit(frame, 12 + c, 20); }
}
}
