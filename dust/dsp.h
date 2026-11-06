#pragma once
// DUST's fixed-point building blocks. Everything is integer: the OP-XY's
// Blackfin has no floating point and no divide instruction, so frequencies
// and times are held as log2 values (Q16, 65536 an octave) and turned into
// coefficients through a table of powers of two. Shifts of signed values
// go through unsigned arithmetic (shl), so nothing here is undefined.
#include <opxy/q31.hpp>
#include <opxy/audio.hpp>
#include "lit.h"
#include "tables.h"

namespace dust {
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
const i32 U = 1 << 27;               // a source's nominal full level inside a voice
const i32 lsr = L(44100.0);          // log2 of the sample rate

// ------------------------------------------------------------ arithmetic
// (as the Blackfin's MIN and MAX instructions: no branches in a loop)
inline i32 imin(i32 a, i32 b) { return a < b ? a : b; }
inline i32 imax(i32 a, i32 b) { return a > b ? a : b; }
inline i32 clamp(i32 v, i32 lo, i32 hi) { return imax(imin(v, hi), lo); }
// |v|, or |v| - 1 for a negative v: no branch and no overflow (the most
// negative value gives the most positive)
inline i32 absolute(i32 v) { return v ^ (v >> 31); }
// v * 2^s without the undefined left shift of a negative value.
inline i32 shl(i32 v, unsigned s) { return static_cast<i32>(static_cast<u32>(v) << s); }
// A saturating sum (one instruction on the Blackfin, and it keeps GCC from
// holding running sums in pointer registers).
inline i32 add(i32 a, i32 b) { return opxy::q31::add(a, b); }
// a b / 65536 for Q16 values, |a| < 2^24 and |b| < 2^23.
inline i32 mul16(i32 a, i32 b) { return multiply(shl(a, 7), shl(b, 8)); }
// a + (b - a) t for a Q16 unit t (0..65536), |b - a| < 2^31.
inline i32 mix(i32 a, i32 b, i32 t) { return a + multiply(b - a, shl(clamp(t, 0, 65535), 15)); }
// A Q16 unit as Q31.
inline i32 q31(i32 t) { return shl(clamp(t, 0, 65535), 15); }

// ------------------------------------------------------------ powers and logs
// 2^(l / 65536) as an unsigned integer, l in [0, 32 octaves).
inline u32 pow2(i32 l) {
    if (l <= 0) return l == 0 ? 1u : 0u;
    if (l >= 32 * octave) return 0xffffffffu;
    const u32 n = static_cast<u32>(l) >> 16, f = static_cast<u32>(l) & 0xffffu;
    const u32 i = f >> 8, r = f & 0xffu;
    const u32 a = tables::exp2[i], b = tables::exp2[i + 1];
    const u32 m = a + (((b - a) * r) >> 8);   // 2^30 .. 2^31
    if (n >= 30) return m << (n - 30);
    return m >> (30 - n);
}
// A Q31 fraction 2^(l / 65536), for l <= 0.
inline i32 frac2(i32 l) {
    if (l >= 0) return one;
    if (l <= -31 * octave) return 0;
    return static_cast<i32>(pow2(l + 31 * octave));
}
// log2 of a positive integer, Q16.
inline i32 log2u(u32 x) {
    if (!x) return -32 * octave;
    i32 n = 31;
    if (x < 0x10000u) { x <<= 16; n -= 16; }
    if (x < 0x1000000u) { x <<= 8; n -= 8; }
    if (x < 0x10000000u) { x <<= 4; n -= 4; }
    if (x < 0x40000000u) { x <<= 2; n -= 2; }
    if (x < 0x80000000u) { x <<= 1; n -= 1; }
    const u32 i = (x >> 23) & 0xffu, r = (x >> 15) & 0xffu;
    const i32 a = tables::log2m[i], b = tables::log2m[i + 1];
    return n * 65536 + a + (((b - a) * static_cast<i32>(r)) >> 8);
}
// exp(-x), Q31, for x = 2^(lx / 65536).
inline i32 expneg(i32 lx) {
    if (lx < -6 * octave) {   // x < 1/64: the series
        const i32 x = frac2(lx);
        return one - x + (multiply(x, x) >> 1);
    }
    if (lx > 5 * octave) return 0;
    const u32 y = pow2(lx + L(1.4426950408889634) + 16 * octave);   // x log2(e), Q16
    return frac2(-static_cast<i32>(y));
}

// ------------------------------------------------------------ coefficients
// A phase increment (2^32 a cycle) for a frequency given as log2 Hz.
inline u32 increment(i32 lhz) { return pow2(clamp(lhz - lsr + 32 * octave, 0, 32 * octave - 1)); }
// The sine of a phase (2^32 a cycle), Q31.
inline i32 sine(u32 phase) {
    const u32 i = phase >> 22, r = (phase >> 6) & 0xffffu;
    const i32 a = tables::sine[i], b = tables::sine[i + 1];
    return a * 65536 + (b - a) * static_cast<i32>(r);
}
// The Chamberlin state-variable filter's f = 2 sin(pi fc / SR), Q31, for an
// increment up to 6.8 kHz, where it stays stable with any damping we use.
const u32 svf_top = 662000000u;
inline i32 svf_f(u32 inc) {
    if (inc > svf_top) inc = svf_top;
    return shl(sine(inc >> 1), 1);
}
// Its damping 1 / Q for a mode of frequency 2^lhz ringing with time constant
// 2^ltau seconds: 1 / (pi f tau), Q31, at most 0.9.
inline i32 svf_q(i32 lhz, i32 ltau) {
    const i32 l = -lhz - ltau - L(3.14159265358979);
    return l >= -L(1.0 / 0.9) ? Q31(0.9) : clamp(frac2(l), 64, Q31(0.9));
}
// A one-pole filter's k = 1 - exp(-2 pi fc / SR), Q31.
inline i32 onepole_k(i32 lhz) { return one - expneg(lhz + L(6.283185307179586) - lsr); }
// A decay over a block of 64 samples for a time constant of 2^ltau seconds,
// as a log2 step (Q16, negative): -64 / (tau SR) log2(e).
inline i32 block_slope(i32 ltau) { return -static_cast<i32>(pow2(L(64.0 * 1.4426950408889634) - lsr - ltau + 16 * octave)); }
// The same per sample, Q31: exp(-1 / (tau SR)).
inline i32 sample_decay(i32 ltau) { return expneg(-ltau - lsr); }
// Seconds (log2) as samples.
inline u32 samples(i32 ls) { return pow2(clamp(ls + lsr, 0, 30 * octave)); }

// ------------------------------------------------------------ noise
// xorshift32: shifts and exclusive-ors only. Its state is never zero: every
// seed starts odd, and the generator never reaches zero from anything else.
inline u32 rand32(u32& s) {
    u32 x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
}
// White noise at U (a source's full level).
inline i32 noise(u32& s) { return static_cast<i32>(rand32(s)) >> 4; }

// ------------------------------------------------------------ filters
struct OnePole {
    i32 s;
    i32 low(i32 x, i32 k) { s += multiply(x - s, k); return s; }
    i32 high(i32 x, i32 k) { s += multiply(x - s, k); return x - s; }
};
}
