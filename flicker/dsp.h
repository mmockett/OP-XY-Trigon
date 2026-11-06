#pragma once
// FLICKER's fixed-point building blocks. Everything here is integer: the
// OP-XY's Blackfin has no floating point and no divide instruction, so
// frequencies and times are held as log2 values (Q16, 65536 an octave) and
// turned into coefficients with a table of powers of two. Literal constants
// are written through constexpr helpers, which the compiler folds; no
// floating point reaches the device. No left shift of a value that may be
// negative, and no sum that can pass the Q31 range.
#include <opxy/q31.hpp>
#include <opxy/audio.hpp>
#include "tables.h"
#include "units.h"

namespace flicker {
using opxy::i16;
using opxy::i32;
using opxy::i8;
using opxy::u16;
using opxy::u32;
using opxy::u8;
using opxy::q31::multiply;
const unsigned frames = opxy::block_frames;
const i32 one = 0x7fffffff;
const i32 half_scale = 0x40000000;   // the SDK's ceiling for a voice
const i32 octave = 65536;            // log2 values are Q16
// The soundtrack's edge: a drawing that reaches the track's edges is +-E.
const i32 E = 1 << 28;

// ------------------------------------------------------------ constants
constexpr i32 Q31(double x) { return x >= 1.0 ? one : x <= -1.0 ? -one : ct::round(x * 2147483648.0); }
constexpr i32 Q16(double x) { return ct::round(x * 65536.0); }
constexpr i32 QE(double x) { return ct::round(x * 268435456.0); }   // in track edges
const i32 lsr = L(44100.0);   // log2 of the sample rate

// ------------------------------------------------------------ arithmetic
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 absolute(i32 v) { return v < 0 ? (v == -one - 1 ? one : -v) : v; }
// a + (b - a) t for a Q16 unit t, |a|, |b| < 2^30.
inline i32 mix(i32 a, i32 b, i32 t) { return a + multiply(b - a, clamp(t, 0, 65535) * 32768); }
// a b / 65536, for |a| < 2^24 and |b| < 2^23.
inline i32 mul16(i32 a, i32 b) { return multiply(a * 128, b * 256); }
// A Q16 unit (0..65536) squared.
inline i32 sq16(i32 t) {
    const u32 u = static_cast<u32>(clamp(t, 0, 65536)) >> 1;   // so that 65536 squared fits
    return static_cast<i32>((u * u) >> 14);
}
// A phase increment scaled by a factor in Q30 (1 << 30 is one); increments
// under 2^31.
inline u32 scale_inc(u32 inc, i32 f) { return static_cast<u32>(multiply(static_cast<i32>(inc >> 1), f)) << 2; }

// 2^(l / 65536) as an unsigned integer, for l in [0, 32 octaves).
inline u32 pow2(i32 l) {
    if (l <= 0) return l == 0 ? 1u : 0u;
    if (l >= 32 * octave) return 0xffffffffu;
    const u32 n = static_cast<u32>(l) >> 16, f = static_cast<u32>(l) & 0xffffu;
    const u32 i = f >> 8, r = f & 0xffu;
    const u32 a = tables::exp2[i], b = tables::exp2[i + 1];
    const u32 m = a + (((b - a) * r) >> 8);   // 2^30 .. 2^31
    if (n >= 30) {
        const u32 s = n - 30;
        return s && m >= 0x80000000u ? 0xffffffffu : m << s;
    }
    return m >> (30 - n);
}
// A Q31 fraction 2^(l / 65536), l <= 0; 0 below 2^-31.
inline i32 frac2(i32 l) {
    const u32 v = pow2(l + 31 * octave);
    return v > 0x7fffffffu ? one : static_cast<i32>(v);
}
// log2 of a positive integer, Q16 (note-on work).
inline i32 log2q(u32 x) {
    if (!x) return -32 * octave;
    u32 n = 0, y = x;
    while (y >= 2u) { y >>= 1; ++n; }
    const u32 m = n <= 30 ? x << (30 - n) : x >> (n - 30);   // the mantissa, Q30
    u32 lo = 0, hi = 256;
    while (hi - lo > 1) {
        const u32 mid = (lo + hi) >> 1;
        if (tables::exp2[mid] <= m) lo = mid; else hi = mid;
    }
    const u32 a = tables::exp2[lo], d = tables::exp2[lo + 1] - a;
    u32 rl = 0, rh = 256;
    while (rh - rl > 1) {
        const u32 mid = (rl + rh) >> 1;
        if (a + ((d * mid) >> 8) <= m) rl = mid; else rh = mid;
    }
    return static_cast<i32>(n * 65536u + lo * 256u + rl);
}
// exp(-x), Q31, for x = 2^(lx / 65536).
inline i32 expneg(i32 lx) {
    if (lx < -octave) {   // x < 0.5: the series keeps small x exact
        const i32 x = frac2(lx), x2 = multiply(x, x), x3 = multiply(x2, x);
        return one - x + (x2 >> 1) - multiply(x3, Q31(1.0 / 6.0)) + multiply(multiply(x2, x2), Q31(1.0 / 24.0));
    }
    if (lx > 5 * octave) return 0;
    return frac2(-static_cast<i32>(pow2(lx + L(1.4426950408889634) + 16 * octave)));
}
// A phase increment (2^32 a cycle) for a frequency given as log2 Hz, under
// 2^31 so that a glide's sums stay signed.
inline u32 increment(i32 lhz) { return pow2((lhz < L(18000) ? lhz : L(18000)) - lsr + 32 * octave); }   // held under 18 kHz
// A per-sample decay, Q31, for a time constant given as log2 seconds.
inline i32 decay(i32 ltau) { return expneg(-ltau - lsr); }
// The same decay over a whole block of 64 samples.
inline i32 block_decay(i32 ltau) { return expneg(6 * octave - ltau - lsr); }
// A value between two ends on a log scale: la + (lb - la) t, t a Q16 unit.
inline i32 lmix(i32 la, i32 lb, i32 t) { return la + mul16(lb - la, clamp(t, 0, 65536)); }
// A number of samples for a time given as log2 seconds.
inline i32 samples(i32 lsec) { return static_cast<i32>(pow2(lsec + lsr)); }

// The Chamberlin state-variable filter's frequency coefficient,
// f = 2 sin(pi fc / SR), Q31, for an increment up to about 7 kHz.
inline i32 svf_f(u32 inc) {
    const u32 top = 690000000u;   // 7.08 kHz: f stays under 1
    if (inc > top) inc = top;
    const i32 x = multiply(static_cast<i32>(inc >> 1), Q31(3.14159265358979 / 4.0)) * 4;   // pi * inc / 2^32, Q31
    const i32 x2 = multiply(x, x), x3 = multiply(x2, x), x5 = multiply(x3, x2);
    const i32 s = x - multiply(x3, Q31(1.0 / 6.0)) + multiply(x5, Q31(1.0 / 120.0));
    return s >= Q31(0.5) ? one : s * 2;
}
// Its damping 1 / Q for a mode that rings with time constant tau: 1 / (pi f tau).
inline i32 svf_q(i32 lhz, i32 ltau) {
    const i32 l = -lhz - ltau - L(3.14159265358979);
    return l >= 0 ? Q31(0.97) : clamp(frac2(l), 1, Q31(0.97));
}
// One-pole coefficient k = 1 - exp(-2 pi fc / SR), Q31.
inline i32 onepole_k(i32 lhz) { return one - expneg(lhz + L(6.283185307179586) - lsr); }

// ------------------------------------------------------------ noise
// xorshift: shifts and exclusive-ors only. The state is never zero.
inline u32 rng(u32& s) {
    u32 x = s ? s : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
}
inline i32 noise(u32& s) { return static_cast<i32>(rng(s)); }

// ------------------------------------------------------------ drawn waves
// A sine, Q31 (the table's Q15 interpolated).
inline i32 sine(u32 phase) {
    const u32 i = phase >> 22;
    const i32 r = static_cast<i32>((phase >> 6) & 0xffffu);
    const i32 a = tables::sine[i], b = tables::sine[i + 1];
    return a * 65536 + (b - a) * r;
}
// A triangle, Q31, in phase with the sine (0 at phase 0, rising).
inline i32 triangle(u32 phase) {
    const u32 p = phase + 0x40000000u;         // a quarter on: the peak at a quarter turn
    const u32 f = p & 0x80000000u ? ~p : p;    // 0 .. 2^31 - 1 and back
    return (static_cast<i32>(f) - 0x40000000) * 2;
}
// A drawn wave, Q30 (+-2^30 at its peaks): a sine turned toward a triangle
// (bend, Q31, 0..1), then drawn taller than its height (height, Q8, 256 as
// drawn, up to 2047) and cut off flat at it: a trapezoid with sloped edges,
// the square a pen draws.
inline i32 drawn(u32 phase, i32 bend, i32 height) {
    const i32 s = sine(phase) >> 1, t = triangle(phase) >> 1;
    const i32 w = s + multiply(t - s, bend);
    if (height <= 256) return w;
    const i32 y = multiply(w, height * 1048576);   // w height / 2048
    return clamp(y, -(1 << 27), 1 << 27) * 8;
}
// A drawn pulse, Q30: the triangle cut at a level (duty, Q31: the share of a
// cycle it is high, 0..1) and drawn steep (height, Q8, up to 2047).
inline i32 pulse(u32 phase, i32 level, i32 height) {
    const i32 t = (triangle(phase) >> 1) - level;   // within +-2^31
    const i32 y = multiply(t, height * 1048576);
    return clamp(y, -(1 << 27), 1 << 27) * 8;
}

// ------------------------------------------------------------ envelopes
// An exponential that moves once a block and is ramped through it.
struct Ramp {
    i32 value, factor;   // Q31: now, and its change over a block
    void set(i32 v, i32 f) { value = v; factor = f; }
    // The block's start and its step per sample; advances by a block.
    i32 step(i32& start) {
        start = value;
        value = multiply(value, factor);
        return (value - start) / 64;
    }
};

// ------------------------------------------------------------ filters
struct OnePole {
    i32 s;
    i32 low(i32 x, i32 k) { s += multiply(x - s, k); return s; }
    i32 high(i32 x, i32 k) { s += multiply(x - s, k); return x - s; }
};
// A Chamberlin state-variable filter. It is stable while f + q < 2: steady
// keeps a damping q (given halved, Q31) under 1.98 - f.
inline i32 steady(i32 f, i32 q2) {
    const i32 most = Q31(0.99) - (f >> 1);   // (1.98 - f) / 2
    return q2 < most ? q2 : most;
}
// The filter itself.
struct Svf {
    i32 lp, bp;
    i32 band(i32 x, i32 f, i32 q) {
        lp += multiply(f, bp);
        const i32 hp = x - lp - multiply(q, bp);
        bp += multiply(f, hp);
        return bp;
    }
    i32 high(i32 x, i32 f, i32 q) {
        lp += multiply(f, bp);
        const i32 hp = x - lp - multiply(q, bp);
        bp += multiply(f, hp);
        return hp;
    }
    i32 low(i32 x, i32 f, i32 q) {
        lp += multiply(f, bp);
        const i32 hp = x - lp - multiply(q, bp);
        bp += multiply(f, hp);
        return lp;
    }
};
}
