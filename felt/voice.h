#pragma once
// One voice of FELT: one note of a prepared upright, and whatever is wedged
// in its strings. Every model is the same few parts (models.h):
//
//   exciter (the felt hammer's contact, a brush, beads, a bow, the lid)
//     -> the string's partials and anything ringing with them (modes)
//     -> what buzzes against the string (paper, a loose nut, a bristle)
//     -> the pedal's sympathetic strings, driven by all of it
//     -> the soundboard's soft top
//
// A mode is a magic-circle oscillator (it rings at exactly the frequency
// whose 2 sin(pi f / SR) it holds) whose second state loses a little every
// sample. Signals inside a voice stay near 2^27, well under full
// scale; running sums go through opxy::q31::add.
#include "dsp.h"
#include "shape.h"

namespace felt {
const u32 max_modes = 16;   // the string, its unisons, the action's wood, the preparation
const u32 pedal_modes = 4;  // the pedal's sympathetic strings
const i32 pulse_sum = 1 << 26;   // what a hammer's whole contact puts into a mode of unit coupling

struct Mode {
    i32 l, b;     // the two states
    i32 f;        // 2 sin(pi f / SR), Q31; or sin(pi f / SR) for a high mode
    i32 base;     // f as set up, before a glide or a bend
    i32 g;        // its coupling to the exciter, Q31
    i32 r;        // its second state's loss a sample, Q31
    i32 sd;       // its loss rate once the damper is down (Q16 per second): the damper's time sets r from it
    i32 lf;       // its frequency, log2 Hz
};

struct StringSpec {
    i32 lf0;          // the fundamental, log2 Hz
    i32 B;            // inharmonicity, Q24
    u32 partials;     // at most this many
    u32 doubled;      // partials 1..doubled have a second unison string
    i32 detune;       // the unisons' half spread, log2 Q16
    i32 beta;         // the strike point, Q16 unit of the string's length
    i32 tilt;         // partial n is weighted n^-tilt, Q16
    i32 sigma1;       // the fundamental's own loss, Q16 per second
    i32 sigma_exp;    // losses grow as (f_n / f0)^sigma_exp, Q16
    i32 rubber, rubber_x;   // a rubber's loss at an antinode (Q16 per second), and where (Q16)
    i32 mu, mass_x;   // a mass's load (Q16) and where (Q16)
    i32 damper;       // the damper's added loss, Q16 per second
    i32 second;       // the second unison string's share, Q31
    i32 scatter;      // AGE: each partial's own drift, at most this far either way (log2 Q16)
};

struct Voice {
    u32 owner, model, next, pending, done, quiet;
    i32 tune, soft;            // the key's pad controls (drum_pad)
    u32 seed, t;               // noise; samples since the note began
    i32 gain, lim;             // the output gain (Q31 of 64), the transient limiter's gain now
    u32 waiting;               // the note still has something to come (a bounce, a shake, a draw)
    // the modes: [0, n) the string and its company; [16, 20) the pedal's strings
    u32 n, high;               // modes in use; bit 0: coefficients are sin, not 2 sin (modes past 7 kHz); bit 1: low modes may run at half the rate
    u32 live;                  // bit k: mode k still rings
    u32 bent;                  // BOW: the string's modes [0, bent) bend along the draw
    u32 glides;                // THUD, TOM: bit k, mode k glides
    // a string's upper partials, set up in the note's second block (the
    // first sets up its lower ones and strikes them): from spec_next on
    StringSpec spec;
    i32 spec_ltc, spec_lscale;
    u32 spec_next;
    u32 late;                  // 1: a screw's or bolt's own modes, 2: a plank's bolts, to set up in the third block
    Mode mode[max_modes + pedal_modes];
    u32 npedal;
    i32 pedal_g;               // the sympathetic strings' coupling to the voice's sound
    i32 grain;                 // a grooved felt's scratch at the blow (AGE), Q31
    // a noise exciter (brush, plank, bow's rosin): its level now and its change a block
    i32 nlevel, nk, ntarget, nrise;
    OnePole nhp, nlp;
    i32 nhpk, hprev;           // ...; the half-rate modes' last sample
    // scheduled strikes (the lid's bounces, beads): when the next comes, how many left
    u32 at, left;
    i32 samp, sk;              // the next strike's height, and its change from one to the next
    u32 sgap;                  // the gap to the next, samples (Q8)
    i32 sgk;                   // the gap's change, Q31 (shrinking, as a bounce's)
    // a buzz against the string (paper, a loose nut): threshold, level, band
    i32 bthr, blevel, bf, bq;
    Svf bsv;
    OnePole bhp;
    // a noise voice of its own (the lid's slap and tail, the brush's hiss)
    i32 wlevel, wk, wf, wq, whpk;
    Svf wsv;
    OnePole whp, whp2;
    // the glide (tension) or the bow's bend: the pitch over its base now, Q30, and its change
    i32 bend, bend_k;
    u32 halves;                // bit k: mode k runs at half the sample rate
    i32 hprev_pedal;           // the pedal's half-rate strings' last sample
    // the damper: when it lands (samples), and whether it has
    u32 damp_at, damped;
    // the bow: its phase and increment, its envelope
    u32 bphase, binc;
    i32 benv, bpress, blp;
    u32 bdraw;
    // the soundboard's soft top, and an output high-pass (hats)
    OnePole top, hp1, hp2;
    i32 topk, hpk;
    // per-model settings
    i32 k[8];
};

// ------------------------------------------------------------ modes
// Adds a mode at lhz (log2 Hz) with coupling g, free loss sigma and damped
// loss sigma_d (Q16 per second). Returns its index, or -1 if out of room or
// over the voice's top.
// Where the voice allows it (high bit 1), a mode that stays low (under
// 2.4 kHz; under 800 Hz on a gliding string, which starts up to two octaves
// higher) runs at half the sample rate: its coefficient and its loss for
// two samples a step.
inline bool half_rate(const Voice& v, i32 lhz) {
    return (v.high & 2) && lhz < (v.model == M::THUD || v.model == M::TOM ? L(800.0) : L(2400.0));
}
static __attribute__((noinline)) i32 add_mode(Voice& v, i32 lhz, i32 g, i32 sigma, i32 sigma_d) {
    if (v.n >= max_modes) return -1;
    if (lhz > ((v.high & 1) ? L(10500.0) : L(7000.0)) || lhz < L(16.0)) return -1;
    Mode& m = v.mode[v.n];
    const bool half = half_rate(v, lhz);
    const i32 s = sin_inc(increment(half ? lhz + octave : lhz));
    m.f = m.base = (v.high & 1) ? s : s * 2;
    m.g = g;
    m.lf = lhz;
    m.r = decay_over(sigma, half ? 4 : 2);
    if (half) v.halves |= 1u << v.n;
    m.sd = sigma_d;
    m.l = m.b = 0;
    v.live |= 1u << v.n;
    return static_cast<i32>(v.n++);
}
// A pedal string at lhz, ringing for its own loss sigma.
static __attribute__((noinline)) void add_pedal(Voice& v, i32 lhz, i32 sigma) {
    if (v.npedal >= pedal_modes || lhz > L(7000.0) || lhz < L(20.0)) return;
    Mode& m = v.mode[max_modes + v.npedal];
    const bool half = half_rate(v, lhz);
    const i32 s = sin_inc(increment(half ? lhz + octave : lhz));
    m.f = m.base = s * 2;
    m.g = one;
    m.lf = lhz;
    m.r = decay_over(sigma, half ? 4 : 2);
    if (half) v.halves |= 1u << (max_modes + v.npedal);
    m.sd = 0;
    m.l = m.b = 0;
    ++v.npedal;
}

// Runs one mode over a block, adding its sound to out: free, or driven by x.
// Its second state loses r each sample, so the mode's swing loses about
// sqrt(r): one multiply a sample, and no work between samples. (Each loop is
// its own function, so tools/cpu.py can count it in the compiled code.)
template <unsigned N> static __attribute__((noinline)) void run_free(Mode& m, i32* out) {
    i32 l = m.l, b = m.b;
    const i32 f = m.f, r = m.r;
    for (unsigned i = 0; i < N; ++i) {
        l += mul(f, b);
        b = mul(b, r) - mul(f, l);
        out[i] = add(out[i], b);
    }
    m.l = l;
    m.b = b;
}
template <unsigned N> static __attribute__((noinline)) void run_driven(Mode& m, const i32* x, i32* out) {
    i32 l = m.l, b = m.b;
    const i32 f = m.f, g = m.g, r = m.r;
    for (unsigned i = 0; i < N; ++i) {
        l += mul(f, b);
        b = mul(b, r) - mul(f, l) + mul(x[i], g);
        out[i] = add(out[i], b);
    }
    m.l = l;
    m.b = b;
}
// The same for modes above 7 kHz, whose coefficient is held at half.
template <unsigned N> static __attribute__((noinline)) void run_free_high(Mode& m, i32* out) {
    i32 l = m.l, b = m.b;
    const i32 f = m.f, r = m.r;
    for (unsigned i = 0; i < N; ++i) {
        const i32 p = mul(f, b);
        l = add(l, add(p, p));
        const i32 q = mul(f, l);
        b = mul(b, r) - add(q, q);
        out[i] = add(out[i], b);
    }
    m.l = l;
    m.b = b;
}
template <unsigned N> static __attribute__((noinline)) void run_driven_high(Mode& m, const i32* x, i32* out) {
    i32 l = m.l, b = m.b;
    const i32 f = m.f, g = m.g, r = m.r;
    for (unsigned i = 0; i < N; ++i) {
        const i32 p = mul(f, b);
        l = add(l, add(p, p));
        const i32 q = mul(f, l);
        b = mul(b, r) - add(q, q) + mul(x[i], g);
        out[i] = add(out[i], b);
    }
    m.l = l;
    m.b = b;
}
template <unsigned N> inline void run_low(Mode& m, const i32* x, i32* out) {
    if (x && m.g) run_driven<N>(m, x, out); else run_free<N>(m, out);
}
template <unsigned N> inline void run_high(Mode& m, const i32* x, i32* out) {
    if (x && m.g) run_driven_high<N>(m, x, out); else run_free_high<N>(m, out);
}
// Half-rate samples h back at the full rate into out: a straight line
// between them, half a sample late (prev: the last block's last).
inline void half_add(i32& prev, const i32* h, i32* out) {
    i32 p = prev;
    for (unsigned j = 0; j < frames / 2; ++j) {
        const i32 y = h[j];
        out[2 * j] = add(out[2 * j], (p >> 1) + (y >> 1));
        out[2 * j + 1] = add(out[2 * j + 1], y);
        p = y;
    }
    prev = p;
}
// A drive x at half the rate: each pair of its samples summed (halves, then
// doubled with saturation).
inline void half_drive(const i32* x, i32* xh) {
    for (unsigned j = 0; j < frames / 2; ++j) {
        const i32 d = (x[2 * j] >> 1) + (x[2 * j + 1] >> 1);
        xh[j] = add(d, d);
    }
}
// Modes [first, last) over a block, those at half the rate into h; whether
// any ran at half the rate. A mode rung down to a few steps above zero with
// nothing driving it stops: fixed point would keep it circling there.
inline bool run_range(Voice& v, u32 first, u32 last, const i32* x, const i32* xh, i32* out, i32* h, bool stops) {
    bool any = false;
    for (u32 k = first; k < last; ++k) {
        if (k < max_modes && !(v.live & (1u << k))) continue;
        Mode& m = v.mode[k];
        if (v.halves & (1u << k)) {
            any = true;
            if (v.high & 1) run_high<frames / 2>(m, xh, h); else run_low<frames / 2>(m, xh, h);
        } else {
            if (v.high & 1) run_high<frames>(m, x, out); else run_low<frames>(m, x, out);
        }
        if (stops && !x && absolute(m.l) < 256 && absolute(m.b) < 256) {
            m.l = m.b = 0;
            v.live &= ~(1u << k);
        }
    }
    return any;
}
// Every live mode over a block.
inline void run_modes(Voice& v, const i32* x, i32* out) {
    i32 h[frames / 2], xh[frames / 2];
    for (unsigned j = 0; j < frames / 2; ++j) h[j] = 0;
    if (x && v.halves) half_drive(x, xh);
    if (run_range(v, 0, v.n, x, x ? xh : nullptr, out, h, true) || v.hprev) half_add(v.hprev, h, out);
}
// The pedal's strings, driven by what the voice sounds so far, into it.
inline void run_pedal(Voice& v, i32* out) {
    if (!v.npedal) return;
    i32 x[frames], xh[frames / 2], h[frames / 2];
    const i32 g = v.pedal_g;
    for (unsigned i = 0; i < frames; ++i) x[i] = mul(out[i], g);
    for (unsigned j = 0; j < frames / 2; ++j) h[j] = 0;
    if (v.halves >> max_modes) half_drive(x, xh);
    const u32 high = v.high;
    v.high &= ~1u;   // the pedal's strings are low modes, whatever the voice's
    if (run_range(v, max_modes, max_modes + v.npedal, x, xh, out, h, false)) half_add(v.hprev_pedal, h, out);
    v.high = high;
}
// The damper lands: every string mode takes its damped loss.
static __attribute__((noinline)) void land_damper(Voice& v, u32 count) {
    for (u32 k = 0; k < count && k < v.n; ++k)
        if (v.mode[k].sd) v.mode[k].r = decay_over(v.mode[k].sd, (v.halves & (1u << k)) ? 4 : 2);
    v.damped = 1;
}
// The glide: the string's gliding modes at (1 + 3 bend) times their base
// (bend Q30, so up to two octaves). Their coefficients scale with it, near
// enough where it matters: the low partials that carry the glide.
static __attribute__((noinline)) void bend_modes(Voice& v) {
    const i32 m = v.bend;
    for (u32 k = 0; k < v.n; ++k) {
        if (!(v.glides & (1u << k))) continue;
        Mode& md = v.mode[k];
        const i32 room = Q31(0.95) - md.base;
        const i32 up = md.base < ((v.halves & (1u << k)) ? Q31(0.6) : Q31(0.3)) ? mul(md.base, m) * 6 : room;
        md.f = md.base + (up < room ? up : room);
    }
}

// ------------------------------------------------------------ the hammer
// The felt's blow: a raised-cosine contact of log2 ltc seconds, whose whole
// push is pulse_sum, sets each struck mode swinging at once with what the
// contact reaches of it, |sinc(u) / (1 - u^2)| at u = f Tc (tables::reach): a
// harder, shorter blow reaches more of the high partials. The modes then ring
// free (no loop drives them).
static __attribute__((noinline)) void strike(Voice& v, i32 ltc, i32 lscale = 0, u32 advance = 0) {
    for (u32 k = 0; k < v.n; ++k) {
        Mode& m = v.mode[k];
        if (m.g <= 0) continue;
        const i32 lu = m.lf + ltc + 4 * octave;   // log2 (16 u)
        i32 reach;
        if (lu < 0) reach = 0;
        else if (lu >= 5 * octave) reach = tables::reach[32];
        else {
            const u32 x = pow2(lu + 16 * octave);   // 16 u, Q16
            const u32 i = x >> 16;
            const i32 f = static_cast<i32>(x & 0xffffu);
            reach = tables::reach[i] + mul16(tables::reach[i + 1] - tables::reach[i], f);
        }
        // its coupling, scaled by lscale (the string brought to its level), times the reach
        const i32 a = frac2(clamp(log2q(static_cast<u32>(m.g)) - 31 * octave + lscale + reach, -31 * octave, 0));
        const i32 amp = mul(a, pulse_sum);
        if (advance) {
            // struck a little late: where it would be had it been struck at the note's start
            const u32 th = increment(m.lf) * advance;
            m.l = mul(amp, sine(th + 0x40000000u));
            m.b = -mul(amp, sine(th));
        } else {
            m.l = amp;
            m.b = 0;
        }
        m.g = 0;
    }
}

// ------------------------------------------------------------ the string
// A string's partials as modes: n f0 sqrt(1 + B n^2), a mass at mass_x
// lowering those near its antinodes (f / sqrt(1 + 2 mu sin^2(n pi x))), a
// rubber at rubber_x adding loss to the same partials, struck at beta.
// Adds the string's partials first..last; returns the sum of their couplings (Q16 of one),
// so a model can bring it to a level (level_scale).
static __attribute__((noinline)) i32 add_string(Voice& v, const StringSpec& s, u32 first, u32 last) {
    i32 total = 0;
    const i32 ls1 = log16(s.sigma1);
    for (u32 n = first; n <= last && n <= s.partials && n <= 32; ++n) {
        const i32 nn = static_cast<i32>(n * n);
        i32 lf = s.lf0 + tables::lnum[n];
        if (s.scatter) lf += mul16(static_cast<i32>(lcg(v.seed) >> 16) - 32768, s.scatter) * 2;
        if (s.B) lf += (log2q(static_cast<u32>((1 << 24) + s.B * nn)) - 24 * octave) / 2;   // B n^2 < 2^7
        if (s.mu) {
            const i32 w = mul(sinpi(n * static_cast<u32>(s.mass_x)), sinpi(n * static_cast<u32>(s.mass_x)));   // sin^2, Q31
            lf -= (log16(65536 + mul(s.mu, w) * 2) ) / 2;
        }
        const i32 a = mul(sinpi(n * static_cast<u32>(s.beta)), frac2(-mul16(s.tilt, tables::lnum[n])));
        i32 sigma = exp16(ls1 + mul16(s.sigma_exp, lf - s.lf0));
        if (s.rubber) {
            const i32 w = mul(sinpi(n * static_cast<u32>(s.rubber_x)), sinpi(n * static_cast<u32>(s.rubber_x)));
            sigma += mul(s.rubber, w) * 2;
        }
        const i32 sd = sigma + s.damper;
        if (n <= s.doubled) {
            const i32 a2 = mul(a, s.second);
            if (add_mode(v, lf - s.detune, a, sigma, sd) < 0) break;
            total += a >> 15;
            if (a2 > 0 && add_mode(v, lf + s.detune, a2, sigma, sd) >= 0) total += a2 >> 15;
        } else {
            if (add_mode(v, lf, a, sigma, sd) < 0) break;
            total += a >> 15;
        }
    }
    return total > 0 ? total : 1;
}
// log2 of the scale that brings couplings summing to total (Q16 of one) to
// sum to level (Q31), for strike.
inline i32 level_scale(i32 total, i32 level) {
    return log2q(static_cast<u32>(level)) - log2q(static_cast<u32>(total)) - 15 * octave;
}
// The action's wood: the key bed's knock, the hammer shank and the
// soundboard's two low resonances, rung at the strike (their state set, as
// a knock is a click). amount: Q31.
static __attribute__((noinline)) void add_knock(Voice& v, i32 amount, i32 lshift = 0, u32 first = 0) {
    static const i32 hz[5] = {L(92.0), L(228.0), L(640.0), L(1650.0), L(3100.0)};
    static const i32 tau[5] = {L(0.055), L(0.035), L(0.014), L(0.006), L(0.0028)};
    static const i32 amp[5] = {Q31(0.10), Q31(0.12), Q31(0.34), Q31(0.24), Q31(0.12)};
    if (amount <= 0) return;
    for (u32 k = first; k < 5; ++k) {
        const i32 s = rate_of(tau[k]);
        const i32 i = add_mode(v, hz[k] + (k >= 2 ? lshift : 0), 0, s, s);
        if (i < 0) return;
        v.mode[i].b = mul(mul(amp[k], amount), 1 << 27) * 2;
    }
}
}
