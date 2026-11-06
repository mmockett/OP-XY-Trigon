#pragma once
// FELT's fixed-point building blocks. Everything is integer: the OP-XY's
// Blackfin has no floating point and no divide instruction. Pitches are log2
// values (Q16, 65536 an octave); losses are rates (Q16, per second), because
// losses add; amplitudes are Q31. Literals go through constexpr helpers the
// compiler folds. No left shift ever meets a negative value and no signed sum
// can pass the 32-bit range (the host and the Blackfin must agree bit for bit).
#include <opxy/q31.hpp>
#include <opxy/audio.hpp>
#include "tables.h"
#include "units.h"

namespace felt {
using opxy::i16;
using opxy::i32;
using opxy::i8;
using opxy::u16;
using opxy::u32;
using opxy::u8;
const unsigned frames = opxy::block_frames;
const i32 one = 0x7fffffff;
const i32 half_scale = 0x40000000;   // the SDK's ceiling for a voice
const i32 octave = 65536;            // log2 values are Q16

constexpr i32 Q31(double x) { return x >= 1.0 ? one : ct::round(x * 2147483648.0); }
constexpr i32 Q16(double x) { return ct::round(x * 65536.0); }
const i32 lsr = L(44100.0);   // log2 of the sample rate

// ------------------------------------------------------------ arithmetic
inline i32 mul(i32 a, i32 b) { return opxy::q31::multiply(a, b); }
inline i32 add(i32 a, i32 b) { return opxy::q31::add(a, b); }
inline i32 imin(i32 a, i32 b) { return a < b ? a : b; }
inline i32 imax(i32 a, i32 b) { return a > b ? a : b; }
// (lo <= hi; as MIN and MAX, never a branch)
inline i32 clamp(i32 v, i32 lo, i32 hi) { return imax(imin(v, hi), lo); }
inline i32 absolute(i32 v) { return v < 0 ? (v < -one ? one : -v) : v; }
// a + (b - a) t for a Q16 unit t; |b - a| < 2^31.
inline i32 mix(i32 a, i32 b, i32 t) { return a + mul(b - a, clamp(t, 0, 65535) * 32768); }
// a b / 65536 for |a| < 2^24, |b| < 2^23.
inline i32 mul16(i32 a, i32 b) { return mul(a * 128, b * 256); }

// 2^(l / 65536) as an unsigned integer, l in [0, 32 octaves).
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
// A Q31 fraction 2^(l / 65536), l <= 0.
inline i32 frac2(i32 l) {
    const u32 v = pow2(l + 31 * octave);
    return v > 0x7fffffffu ? one : static_cast<i32>(v);
}
// log2 of a positive integer, Q16: its top bit by halving, its mantissa's
// log by the table, interpolated.
inline i32 whole_log2(u32 x) {   // the integer part, Q16
    u32 n = 0;
    if (x >= 1u << 16) { x >>= 16; n += 16; }
    if (x >= 1u << 8) { x >>= 8; n += 8; }
    if (x >= 1u << 4) { x >>= 4; n += 4; }
    if (x >= 1u << 2) { x >>= 2; n += 2; }
    if (x >= 1u << 1) { n += 1; }
    return static_cast<i32>(n * 65536u);
}
inline i32 log2q(u32 x) {
    if (!x) return -32 * octave;
    const i32 whole = whole_log2(x);
    const u32 n = static_cast<u32>(whole) >> 16;
    const u32 m = n <= 30 ? x << (30 - n) : x >> (n - 30);   // the mantissa, Q30: 2^30 .. 2^31
    const u32 i = (m >> 22) & 255u, f = (m >> 14) & 255u;
    const i32 a = tables::log2[i], b = tables::log2[i + 1];
    return whole + a + (((b - a) * static_cast<i32>(f)) >> 8);
}
// log2 of a Q16 value (a ratio), and the Q16 value of a log2.
inline i32 log16(i32 v) { return log2q(static_cast<u32>(v > 1 ? v : 1)) - 16 * octave; }
inline i32 exp16(i32 l) { return static_cast<i32>(pow2(clamp(l + 16 * octave, 0, 31 * octave - 1))); }
// exp(-x) in Q31 for x Q31 in [0, 0.25]: the series, exact where a decay is slow.
inline i32 exp_small(i32 x) {
    const i32 x2 = mul(x, x), x3 = mul(x2, x), x4 = mul(x2, x2);
    return one - x + x2 / 2 - mul(x3, Q31(1.0 / 6.0)) + mul(x4, Q31(1.0 / 24.0));
}
// exp(-n sigma / SR), Q31: a loss rate sigma (Q16, per second) over n samples.
inline i32 decay_over(i32 sigma, u32 n) {
    const i32 x1 = mul(clamp(sigma, 0, 1 << 27), Q31(32768.0 / 44100.0));   // over one sample, Q31
    if (x1 <= (1 << 29) / static_cast<i32>(n)) return exp_small(x1 * static_cast<i32>(n));
    const i32 l = ((mul(x1, Q31(1.4426950408889634 / 2.0)) >> 6) * static_cast<i32>(n)) >> 8;   // log2 e times x, Q16
    return frac2(-l);
}
// A rate (Q16, per second) for a time constant given as log2 seconds.
inline i32 rate_of(i32 ltau) { return exp16(-ltau); }
// A phase increment (2^32 a cycle) for a frequency given as log2 Hz.
inline u32 increment(i32 lhz) { return pow2(lhz - lsr + 32 * octave); }
// sin(pi inc / 2^32), Q31, for an increment up to 2^30 (11 kHz): the magic
// circle's coefficient (it rings at exactly the frequency whose 2 sin it holds).
inline i32 sin_inc(u32 inc) {
    if (inc > (1u << 30)) inc = 1u << 30;
    const i32 x = mul(static_cast<i32>(inc), Q31(3.14159265358979 / 4.0)) * 2;
    const i32 x2 = mul(x, x), x3 = mul(x2, x), x5 = mul(x3, x2), x7 = mul(x5, x2);
    return x - mul(x3, Q31(1.0 / 6.0)) + mul(x5, Q31(1.0 / 120.0)) - mul(x7, Q31(1.0 / 5040.0));
}

// ------------------------------------------------------------ noise
// xorshift: shifts and exclusive-ors only. The state is never zero.
inline u32 lcg(u32& s) {
    u32 x = s ? s : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
}
inline i32 noise(u32& s) { return static_cast<i32>(lcg(s)); }

// ------------------------------------------------------------ sine
inline i32 sine(u32 phase) {   // Q31
    const u32 i = phase >> 23, r = (phase >> 7) & 0xffffu;
    const i32 a = tables::sine[i], b = tables::sine[i + 1];
    return a * 65536 + (b - a) * static_cast<i32>(r);
}
// |sin(pi x)| for x a Q16 unit (any value; it wraps), Q31.
inline i32 sinpi(u32 x16) { return absolute(sine(x16 << 15)); }

// ------------------------------------------------------------ filters
// One-pole: k = 1 - exp(-2 pi fc / SR), Q31.
inline i32 onepole_k(i32 lhz) {
    const i32 x = exp16(clamp(lhz, L(5.0), L(16000.0)) + L(6.283185307179586) - lsr);   // 2 pi fc / SR, Q16
    if (x < 16384) return one - exp_small(x * 32768);
    return one - frac2(-mul(x, Q31(1.4426950408889634 / 2.0)) * 2);
}
struct OnePole {
    i32 s;
    i32 low(i32 x, i32 k) { s += mul(x - s, k); return s; }
    i32 high(i32 x, i32 k) { s += mul(x - s, k); return x - s; }
};
// A Chamberlin state-variable filter for noise bands: f = 2 sin(pi fc / SR)
// (Q31, fc under 7 kHz) and its damping q (Q30, 2 at its broadest).
struct Svf {
    i32 lp, bp;
    i32 band(i32 x, i32 f, i32 q30) {
        lp += mul(f, bp);
        const i32 hp = x - lp - mul(q30, bp) * 2;
        bp += mul(f, hp);
        return bp;
    }
};
inline i32 svf_f(i32 lhz) { return sin_inc(increment(clamp(lhz, L(10.0), L(7000.0)))) * 2; }
}
