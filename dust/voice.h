#pragma once
// One voice of DUST: one hit from a soul 45, played on a portable record
// player. Two halves, as the record has them:
//
//   the recording (1970, a small room, cut to vinyl):
//     a strike -> the drum's modes (up to eight resonators)
//              -> shaped noise (two layers: wires, jingles, seeds, hands)
//              -> metal (six square partials, the cymbals' dense ring)
//     -> two microphones: close, and the room (a four-line network at a
//        quarter of the sample rate)
//     -> a valve compressor and tape -> the cut's bandwidth
//   the playback (the eight controls):
//     the platter's speed, wow and drag scale every frequency and time of
//     the recording as it is synthesised; then the stylus's tracking, the
//     wear of the copy, the player's tone control, the needle's landing and
//     its lift.
//
// Signals inside a voice are Q31 integers near U = 2^27. Every part's
// coefficients and levels move once a block; the per-sample loops are kept
// short and simple, one part at a time, so the Blackfin's compiler holds
// their state in registers.
#include "dsp.h"

namespace dust {
// ------------------------------------------------------------ envelopes
// An envelope in the log domain: an attack (a raised ramp from 0, done at
// 65536) and an exponential decay, both moving once a block, faster or
// slower with the record's speed. The value is ramped across each block.
struct Env {
    i32 lvl;        // log2 of the decaying level, Q16 (0 = full)
    i32 slope;      // its change a block at 45 rpm, Q16 (negative)
    i32 atk;        // the attack's progress, Q16 unit
    i32 atk_step;   // its step a block at 45 rpm
    i32 amp;        // the value at the end of the last block, Q31
    i32 hold;       // blocks the level holds before it decays (a shake's swell)
};
// Envelopes, and the parts they drive, stop 72 dB down.
const i32 env_floor = -12 * octave;
inline i32 env_value(const Env& e) {
    const i32 a = e.atk >= 65536 ? one : multiply(q31(e.atk), q31(e.atk) >> 1) + (q31(e.atk) >> 1);   // (t^2 + t) / 2
    return e.lvl <= env_floor ? 0 : multiply(a, frac2(e.lvl));
}
// Advances the envelope a block at the speed r16 (Q16, 1 = 45 rpm).
inline void env_step(Env& e, i32 r16) {
    if (e.atk < 65536) e.atk = clamp(e.atk + mul16(e.atk_step, r16), 0, 65536);
    else if (e.hold > 0) --e.hold;
    else if (e.lvl > env_floor) e.lvl += mul16(e.slope, r16);
    e.amp = env_value(e);
}
inline void env_set(Env& e, i32 level, i32 latk, i32 ldecay) {
    e.lvl = level;
    e.atk = 0;
    e.atk_step = static_cast<i32>(pow2(clamp(L(64.0) - lsr - latk + 16 * octave, 0, 20 * octave)));
    if (e.atk_step > 65536) e.atk_step = 65536;
    e.slope = block_slope(ldecay);
    e.amp = 0;
    e.hold = 0;
}

// ------------------------------------------------------------ the parts
// A mode of the drum: a Chamberlin resonator, its band state the sound.
struct Mode {
    i32 lp, bp;
    i32 f, q;      // coefficient now (Q31) and damping (Q31)
    i32 lhz;       // log2 Hz at 45 rpm, before the strike's pitch glide
    i32 gin;       // the strike's gain into it, Q24
    i32 glides;    // bit 0: it follows the strike's pitch glide (a head's tension); bit 1: it runs at half the rate
    i32 left;      // blocks it still rings for
};
const u32 mode_count = 8;
// A noise layer: noise (or sparse grains) through one state-variable
// filter whose low, band and high outputs are mixed, under its envelope;
// bursts restart the envelope (a clap's hands, a shaker's seeds).
const u32 burst_count = 8;
struct Layer {
    u32 seed;
    i32 sl, sb;                 // the filter's states
    i32 lf;                     // its frequency (log2 Hz at 45 rpm)
    i32 f, q;                   // its coefficient and damping (Q30)
    i32 gl, gb, gh;             // the low, band and high outputs' shares (Q31)
    i32 amp;                    // its level, Q31
    i32 follow;                 // how far the drum's head gates it (Q31: snare wires)
    u32 grain;                  // grains: a sample is a seed's hit with this chance in 2^32 (0: plain noise)
    Env env;
    u32 bursts, next;           // the bursts and the next one
    u32 at[burst_count];        // their starts, samples after the hit
    i32 level[burst_count];     // their levels (log2, Q16)
    i32 shift[burst_count];     // their band's shift (log2, Q16): each hand its own
    i32 ldecay;                 // a burst's decay (log2 s)
    u32 on;
};
// Metal: six square partials, three of them ring-modulated in pairs (the two
// cymbals of a hat, a ride's bow), with noise, through a state-variable
// filter (its high and band outputs: the cymbal's tone), under its
// envelope; bursts as a layer's (a tambourine's jingles). Its top, which
// darkens as it rings, is the chain's low-pass (engine.cpp).
struct Metal {
    u32 ph[6], inc[6];
    i32 lhz[6];
    u32 seed;
    i32 sl, sb, lp;
    i32 lb, ll;                 // the tone filter and the top (log2 Hz at 45 rpm)
    i32 f, q, kl;               // their coefficients (q Q30)
    i32 gh, gb;                 // the high and band outputs' shares
    i32 dark, dark_all, dark_k; // the top's fall still to come, all of it (log2, Q16), and its fall a block
    i32 nmix;                   // the noise's share (Q31)
    i32 amp;
    Env env;
    u32 bursts, next;
    u32 at[burst_count];
    i32 level[burst_count];
    i32 ldecay;
    u32 on;
};
// The room: a four-line feedback network at an eighth of the sample rate,
// its four lines interleaved in one buffer of 32 steps each.
const u32 room_samples = 128;
const u16 room_lines[4] = {15, 19, 25, 32};   // at 45 rpm: 2.7 to 5.8 ms, a small, boxy room

// What a block did, for tools/cpu_blocks.cpp's count of the worst block:
// a bit a part, and how many modes and layers ran.
enum Work : u32 { W_BEGIN = 1, W_STRIKE = 2, W_ROOM = 4, W_TAIL = 8, W_VALVE = 16, W_QUIET = 32, W_LIGHT = 64, W_DARK = 128,
                  W_HEAVY = 256, W_WEAR = 512, W_TONE = 1024, W_BRUSH = 2048, W_NEEDLE = 4096, W_LIMIT = 8192, W_KNEE = 16384,
                  W_METAL = 32768, W_SPLIT = 65536 };
struct Voice {
    u32 owner, model, next, pending, done, quiet;
    u32 work, struck_modes, ringing_modes, noise_layers, grain_layers;   // what the last block did
    i32 tune, tone;            // the key's pad controls (drum_pad)
    u32 seed;                  // the record's surface noise (WEAR)
    u32 t;                     // samples since the note began
    u32 pre;                   // samples before the hit (CUE early)
    u32 hit_t;                 // samples since the hit began
    i32 gain;                  // the output gain, Q31 of 64
    i32 lim;                   // the transient limiter's gain now
    // speed: log2 of the platter's rate over 45 rpm, now and as set
    i32 lrate, lbase, r16;
    i32 warp_depth, drag_depth, drag_x, drag_k;   // WARP (log2 Q16), DRAG (log2 Q16), its progress
    u32 warp_phase, warp_inc;
    // the strike
    i32 ex_s, ex_s1, ex_c;     // the pulse: a sine by recursion (now, before, 2 cos, Q30)
    u32 ex_left;
    i32 ex_amp;                // the pulse's peak
    i32 click, click_k;        // the click's level and its fall a sample
    i32 click_amp;
    i32 click_l, click_f, click_kb;   // the click's low corner, and its fall a block
    u32 click_seed;
    i32 direct_pulse, direct_click;   // how much of the strike goes straight to the close microphone (Q31)
    // the drum's modes
    u32 nmodes;
    Mode mode[mode_count];
    i32 glide, glide_k;        // the strike's pitch glide (log2 Q16) and its fall a block
    i32 head;                  // the head's motion over the last block (for the wires)
    Layer layer[2];
    Metal metal;
    // the room
    u32 rlen[4], rpos;
    i32 rlp[4], rg[4], rk;
    i32 send, rprev, rmix;
    i32 tail;                  // CUE: the last bar's tail fed to the room before the hit
    // the record's chain
    i32 comp, comp_g, comp_back, comp_thr;   // the valve compressor: detector, gain, release a block
    i32 cut_l, cut_h, cut_kl, cut_kh;
    // playback
    i32 light, heavy;          // WEIGHT: how far from 2 g, each way (Q31)
    i32 w_thr, w_low_thr;      // the highs and the lows a light stylus holds
    i32 w_s, mode_h, w_bb;     // its split; the half-rate modes' last sample
    i32 w_ks;
    i32 w_d2, w_lo, w_kd, w_kl;   // the darkening's second pole; a heavy arm's fuller low end
    u32 round_k;               // WEIGHT, heavy: the shift that sets its peaks against the ceiling
    u32 w_seed;
    i32 wear, worn, w1;        // WEAR (Q31); the level the worn grooves fuzz over; how worn (Q16)
    i32 hiss, k_hiss, k_dull;
    i32 n_crackle, n_hiss, n_rumble;   // the surface's noise, against the output's level
    i32 rumble, rumble_to;     // the rumble: a slow walk, a value a block
    u32 crackle_p;             // a crackle's chance a block (2^32)
    i32 warp_to, warp_k;       // WARP: the level and the top as the stylus rides the crests
    i32 tone_l, tone_l2, tone_k, tone_lo, tone_hi;
    i32 brush, brush_l;        // DRAG's brush noise
    // the needle: landing late (a fade in) and lifting (a fade out, a thump)
    u32 land_len, lift_at, lift_len, end_at;
    i32 out_g, land_step, lift_step, land_noise;
    i32 drop;                  // the landing scratch's last noise sample
    i32 tail_l;
    u32 thump_phase, thump_inc;
    i32 thump_amp;
    // the room's lines, last: the note's start clears the rest word by word
    i16 room[room_samples];
};

// ------------------------------------------------------------ the strike
// A half-sine contact of 2^lc seconds peaking at amp, and a click: noise
// over a corner at lh (the cut is its top) falling with 2^lclick seconds.
__attribute__((cold)) inline void strike(Voice& v, i32 lc, i32 amp, i32 click_amp, i32 lclick, i32 lh, i32 ll) {
    const u32 n = clamp(static_cast<i32>(samples(lc - v.lbase)), 4, 2000);
    v.ex_left = n;
    // sin(pi k / n) by s[k+1] = 2 cos(pi / n) s[k] - s[k-1]
    const u32 inc = pow2(31 * octave - log2u(n));   // half a cycle over the contact
    v.ex_c = shl(sine(inc + 0x40000000u) >> 1, 0);   // cos, Q30
    v.ex_s1 = 0;
    v.ex_s = sine(inc) >> 1;                        // sin(pi / n), Q30
    v.ex_amp = amp;
    v.click_amp = click_amp;
    v.click = click_amp;
    v.click_k = sample_decay(lclick - v.lbase);
    v.click_kb = frac2(block_slope(lclick - v.lbase));
    // the click's low corner (its top is the cut's: ll is kept for the
    // models' reading)
    (void)ll;
    v.click_f = onepole_k(clamp(lh + v.lbase, L(30), L(16000)));
    v.click_l = 0;
    v.click_seed = 0x2545f491u + v.model * 977u;
}
// The strike for this block: its pulse into e (at an eighth of U, for the
// modes) and straight to the close microphone in x, its click into x; how
// many of the block's samples the pulse lasts.
inline unsigned strike_block(Voice& v, i32* e, i32* x) {
    if (!v.ex_left && v.click < (one >> 12)) return 0;
    v.work |= W_STRIKE;
    unsigned n = 0;
    if (v.ex_left) {
        // the contact: a half sine
        n = v.ex_left < frames ? v.ex_left : frames;
        i32 s = v.ex_s, s1 = v.ex_s1;
        const i32 c = v.ex_c, a = v.ex_amp, dp = v.direct_pulse;
        for (unsigned i = 0; i < n; ++i) {
            const i32 p = multiply(s, a) << 1;
            e[i] = p;
            x[i] = add(x[i], shl(multiply(p, dp), 3));
            const i32 s2 = shl(multiply(s, c), 2) - s1;
            s1 = s;
            s = s2 > 0 ? s2 : 0;
        }
        v.ex_s = s;
        v.ex_s1 = s1;
        v.ex_left -= n;
    }
    if (v.click >= (one >> 12)) {
        // the click: noise over its low corner (the cut's low-pass is its
        // top), falling sample by sample
        i32 g = multiply(v.click, v.direct_click);
        const i32 k = v.click_k, f = v.click_f;
        i32 l = v.click_l;
        u32 seed = v.click_seed;
        for (unsigned i = 0; i < frames; ++i) {
            const i32 w = noise(seed);
            l += multiply(w - l, f);
            x[i] = add(x[i], multiply(w - l, g));
            g = multiply(g, k);
        }
        v.click_l = l;
        v.click_seed = seed;
        v.click = multiply(v.click, v.click_kb);
    }
    return n;
}

// ------------------------------------------------------------ the modes
// Mode k at 2^lhz Hz (45 rpm), ringing 2^ltau s, reaching amplitude amp (Q31
// of U) for the strike's pulse (contact 2^lc s, peak ex_amp).
__attribute__((cold)) inline void set_mode(Voice& v, u32 k, i32 lhz, i32 ltau, i32 amp, i32 lc, bool glides) {
    Mode& m = v.mode[k];
    m.lp = m.bp = 0;
    m.lhz = lhz;
    m.q = svf_q(lhz, ltau);
    const i32 l = lhz + v.lbase;
    // a mode that stays under 3 kHz, its glide and the speed's wow included,
    // runs at half the sample rate (its coefficient for that rate)
    const bool half = l + (glides ? v.glide : 0) <= L(3000);
    m.glides = (glides ? 1 : 0) | (half ? 2 : 0);
    m.f = svf_f(increment(half ? l + octave : l));
    // fade a mode out as the speed takes it past the resonators' top; one
    // too quiet to hear is left out
    if (l > L(6000)) amp = l >= L(6700) ? 0 : multiply(amp, one - shl(clamp((l - L(6000)) * 5, 0, 65535), 15));
    if (amp < Q31(0.025)) amp = 0;
    // the pulse's area is about 0.64 of its peak times its length; the mode
    // takes f times what enters it, so this gain brings it to amp
    const i32 lsamples = lc - v.lbase + lsr - (half ? octave : 0);
    const i32 lf = log2u(static_cast<u32>(m.f > 0 ? m.f : 1)) - 31 * octave;
    const i32 lg = log2u(static_cast<u32>(amp > 0 ? amp : 1)) - 31 * octave + L(1.0 / 0.637) - lf - lsamples + log2u(U) - log2u(static_cast<u32>(v.ex_amp > 0 ? v.ex_amp : 1));
    m.gin = amp > 0 ? static_cast<i32>(pow2(clamp(lg + 24 * octave, 0, 30 * octave))) : 0;   // at most 64
    // it rings until 72 dB down: 8.3 time constants, in real blocks
    m.left = amp > 0 ? static_cast<i32>(samples(ltau - v.lbase + L(8.3)) >> 6) + 4 : 0;
    if (k + 1 > v.nmodes) v.nmodes = k + 1;
}
// The modes for this block: struck by e for its first n samples (the
// pulse), then ringing; their sum into x, N samples of it. Coefficients move
// once a block. Two modes at once (so the block's samples are read and
// written once for both): struck by e for n samples, then ringing.
template <unsigned N> inline void mode_pair(Mode& a, Mode& b, const i32* e, unsigned n, i32* x) {
    const i32 fa = a.f, qa = a.q, fb = b.f, qb = b.q, ga = a.gin, gb = b.gin;
    i32 la = a.lp, ba = a.bp, lb = b.lp, bb = b.bp;
    for (unsigned i = 0; i < n; ++i) {
        la += multiply(fa, ba);
        ba += multiply(fa, shl(multiply(e[i], ga), 7) - la - multiply(qa, ba));
        lb += multiply(fb, bb);
        bb += multiply(fb, shl(multiply(e[i], gb), 7) - lb - multiply(qb, bb));
        x[i] = add(x[i], add(ba, bb));
    }
    for (unsigned i = n; i < N; ++i) {
        la += multiply(fa, ba);
        ba -= multiply(fa, la + multiply(qa, ba));
        lb += multiply(fb, bb);
        bb -= multiply(fb, lb + multiply(qb, bb));
        x[i] = add(x[i], add(ba, bb));
    }
    a.lp = la;
    a.bp = ba;
    b.lp = lb;
    b.bp = bb;
}
template <unsigned N> inline void mode_one(Mode& m, const i32* e, unsigned n, i32* x) {
    const i32 f = m.f, q = m.q, g = m.gin;
    i32 lp = m.lp, bp = m.bp;
    for (unsigned i = 0; i < n; ++i) {
        lp += multiply(f, bp);
        bp += multiply(f, shl(multiply(e[i], g), 7) - lp - multiply(q, bp));
        x[i] = add(x[i], bp);
    }
    for (unsigned i = n; i < N; ++i) {
        lp += multiply(f, bp);
        bp -= multiply(f, lp + multiply(q, bp));
        x[i] = add(x[i], bp);
    }
    m.lp = lp;
    m.bp = bp;
}
template <unsigned N> inline void modes_run(Voice& v, const u32* active, u32 count, const i32* e, unsigned n, i32* x) {
    u32 k = 0;
    for (; k + 1 < count; k += 2) mode_pair<N>(v.mode[active[k]], v.mode[active[k + 1]], e, n, x);
    if (k < count) mode_one<N>(v.mode[active[k]], e, n, x);
}
// The modes for this block: struck by e for its first n samples (the
// pulse), then ringing; their sum into x, and those at half the rate into h
// (struck by every other sample of the pulse); whether any ran at half.
inline bool modes_block(Voice& v, const i32* e, unsigned n, i32* x, i32* h) {
    u32 active[mode_count], count = 0, halves[mode_count], nh = 0;
    for (u32 k = 0; k < v.nmodes; ++k) {
        Mode& m = v.mode[k];
        if (m.left <= 0) continue;
        --m.left;
        const i32 l = m.lhz + v.lrate + ((m.glides & 1) ? v.glide : 0);
        if (m.glides & 2) {
            m.f = svf_f(increment(l + octave));
            halves[nh++] = k;
        } else {
            if (l <= L(6700)) m.f = svf_f(increment(l));
            active[count++] = k;
        }
    }
    if (n) v.struck_modes += count + nh;
    else v.ringing_modes += count + nh;
    modes_run<frames>(v, active, count, e, n, x);
    if (nh) {
        i32 eh[frames / 2];
        const unsigned nn = (n + 1) / 2;
        for (unsigned j = 0; j < nn; ++j) eh[j] = e[2 * j];
        modes_run<frames / 2>(v, halves, nh, eh, nn, h);
    }
    // the head's motion (its low state is half as large at half the rate)
    const Mode& m0 = v.mode[0];
    v.head = imax(absolute(m0.bp), (m0.glides & 2) ? imin(absolute(m0.lp), U) << 1 : absolute(m0.lp));
    return nh != 0;
}
// The half-rate parts h back at the full rate into x: a straight line
// between their samples, half a sample late.
inline void half_add(Voice& v, const i32* h, i32* x) {
    i32 prev = v.mode_h;
    for (unsigned j = 0; j < frames / 2; ++j) {
        const i32 y = h[j];
        x[2 * j] = add(x[2 * j], (prev >> 1) + (y >> 1));
        x[2 * j + 1] = add(x[2 * j + 1], y);
        prev = y;
    }
    v.mode_h = prev;
}

// ------------------------------------------------------------ noise layers
// (a layer at half the rate, on 3, its coefficient for that rate)
inline void layer_tune(Layer& n, i32 lrate, i32 shift) {
    n.f = n.on & 2 ? svf_f(increment(clamp(n.lf + lrate + shift, L(20), L(3300)) + octave))
                   : svf_f(increment(clamp(n.lf + lrate + shift, L(20), L(6600))));
}
// Layer k: level amp (Q31 of U); its filter at 2^lf Hz with damping q
// (Q30, 1/Q: from 0.3, ringing, to 1.9, broad), its low, band and high
// outputs mixed by gl, gb and gh; attack and decay (log2 s).
__attribute__((cold)) inline void set_layer(Voice& v, u32 k, i32 amp, i32 lf, i32 q, i32 gl, i32 gb, i32 gh, i32 latk, i32 ldecay) {
    Layer& n = v.layer[k];
    n.seed = 0x6c8e9cf5u + v.model * 7919u + k * 104729u;
    n.sl = n.sb = 0;
    n.lf = lf;
    n.q = q;
    n.gl = gl;
    n.gb = gb;
    n.gh = gh;
    n.amp = amp;
    n.follow = 0;
    n.grain = 0;
    n.bursts = n.next = 0;
    n.ldecay = ldecay;
    env_set(n.env, 0, latk, ldecay);
    // a layer of the filter's band alone (no high-pass share) under 2.4 kHz
    // runs at half the sample rate (not the clap's body: there it came out
    // 1.6 LU louder, measured)
    n.on = amp > 0 ? (gh == 0 && lf + v.lbase <= L(2400) && v.model != 12 ? 3 : 1) : 0;
    layer_tune(n, v.lbase, 0);
}
// A burst of layer k at 2^lat s (record time) with its level (log2) and its band shifted.
inline void add_burst(Voice& v, u32 k, i32 lat, i32 level, i32 shift) {
    Layer& n = v.layer[k];
    if (n.bursts >= burst_count) return;
    n.at[n.bursts] = samples(lat - v.lbase);
    n.level[n.bursts] = level;
    n.shift[n.bursts] = shift;
    ++n.bursts;
}
// One segment of a layer's samples into x at level a (Q31): plain noise,
inline void layer_noise(Layer& n, i32* x, unsigned from, unsigned to, i32 gb, i32 gh) {
    i32 sl = n.sl, sb = n.sb;
    const i32 f = n.f, q = n.q;
    u32 seed = n.seed;
    for (unsigned i = from; i < to; ++i) {
        const i32 w = noise(seed);
        sl += multiply(f, sb);
        const i32 qb = multiply(q, sb);
        const i32 h = w - sl - qb - qb;
        sb += multiply(f, h);
        x[i] = add(x[i], multiply(sb, gb) + multiply(h, gh));
    }
    n.sl = sl;
    n.sb = sb;
    n.seed = seed;
}
// or sparse grains: a sample is a seed's hit with the layer's chance
inline void layer_grains(Layer& n, i32* x, unsigned from, unsigned to, i32 gb, i32 gh) {
    i32 sl = n.sl, sb = n.sb;
    const i32 f = n.f, q = n.q;
    const u32 grain = n.grain;
    u32 seed = n.seed;
    for (unsigned i = from; i < to; ++i) {
        const u32 r = rand32(seed);
        const i32 w = r < grain ? static_cast<i32>(rand32(seed)) >> 2 : 0;
        sl += multiply(f, sb);
        const i32 qb = multiply(q, sb);
        const i32 h = w - sl - qb - qb;
        sb += multiply(f, h);
        x[i] = add(x[i], multiply(sb, gb) + multiply(h, gh));
    }
    n.sl = sl;
    n.sb = sb;
    n.seed = seed;
}
inline void layer_run(Voice& v, Layer& n, i32* x, unsigned from, unsigned to, i32 a) {
    // the low output's share joins the band's (the low part of a band is its body)
    const i32 gb = multiply(add(n.gb, n.gl), a), gh = multiply(n.gh, a);
    if (n.on & 2) {
        // at half the rate, into the half-rate buffer (at the same gain: the
        // filter's band comes out as loud there, measured)
        ++v.noise_layers;
        layer_noise(n, x, (from + 1) / 2, (to + 1) / 2, gb, 0);
        return;
    }
    if (n.grain) {
        ++v.grain_layers;
        layer_grains(n, x, from, to, gb, gh);
    } else {
        ++v.noise_layers;
        layer_noise(n, x, from, to, gb, gh);
    }
}
// (x: the block at the full rate, h at half, for a layer on 3)
inline void layer_block(Voice& v, Layer& n, i32* x, i32* h, bool retune) {
    if (n.on & 2) x = h;
    if (!n.on) return;
    // the drum's head gates the layer (a snare's wires buzz as the head moves)
    i32 gate = one;
    if (n.follow) gate = one - n.follow + multiply(n.follow, clamp(shl(clamp(v.head, 0, U), 3), 0, one));
    // a burst starting in this block: the block in two segments
    unsigned split = frames;
    if (n.next < n.bursts && n.at[n.next] < v.hit_t + frames) split = n.at[n.next] > v.hit_t ? n.at[n.next] - v.hit_t : 0;
    if (split < frames) {
        if (split) layer_run(v, n, x, 0, split, multiply(multiply(n.env.amp, n.amp), gate));
        n.env.lvl = n.level[n.next];
        n.env.atk = 65536;
        n.env.slope = block_slope(n.ldecay);
        layer_tune(n, v.lrate, n.shift[n.next]);
        ++n.next;
        env_step(n.env, v.r16);
        layer_run(v, n, x, split, frames, multiply(multiply(n.env.amp, n.amp), gate));
        return;
    }
    if (retune) layer_tune(n, v.lrate, n.next ? n.shift[n.next - 1] : 0);
    env_step(n.env, v.r16);
    layer_run(v, n, x, 0, frames, multiply(multiply(n.env.amp, n.amp), gate));
    if (n.env.atk >= 65536 && n.env.lvl <= env_floor && n.next >= n.bursts) n.on = 0;
}

// ------------------------------------------------------------ metal
inline void metal_filters(Metal& m, i32 lrate) {
    m.f = svf_f(increment(clamp(m.lb + lrate, L(20), L(6600))));
    m.kl = onepole_k(clamp(m.ll - (m.dark_all - m.dark) + lrate, L(20), L(18000)));
    for (u32 k = 0; k < 6; ++k) m.inc[k] = increment(m.lhz[k] + lrate);
}
// Metal: partials at base times the ratios (log2), its tone filter at lh
// (until metal_tone sets it), low-passed at ll, noise share nmix; level,
// attack and decay.
__attribute__((cold)) inline void set_metal(Voice& v, i32 amp, i32 lbase_hz, const i32* ratios, i32 lh, i32 ll, i32 nmix, i32 latk, i32 ldecay) {
    Metal& m = v.metal;
    for (u32 k = 0; k < 6; ++k) {
        m.lhz[k] = lbase_hz + ratios[k];
        m.ph[k] = 0x9e3779b9u * (k + 1);   // the partials start apart
    }
    m.seed = 0x1b873593u + v.model * 31u;
    m.sl = m.sb = m.lp = 0;
    m.lb = lh;
    m.ll = ll;
    m.q = Q31(0.70);
    m.gh = one;
    m.gb = 0;
    m.dark = m.dark_all = m.dark_k = 0;
    m.nmix = nmix;
    m.amp = amp;
    m.bursts = m.next = 0;
    m.ldecay = ldecay;
    env_set(m.env, 0, latk, ldecay);
    m.on = amp > 0 ? 1 : 0;
    metal_filters(m, v.lbase);
}
// The metal's tone: its filter at lb (damping bq, Q31 of 2), its band's
// share bmix against its high-pass, and a top that falls by ldark octaves
// with time constant ltau as it rings.
inline void metal_tone(Voice& v, i32 lb, i32 bq, i32 bmix, i32 ldark, i32 ltau) {
    Metal& m = v.metal;
    m.lb = lb;
    m.q = bq;
    m.gb = bmix;
    m.gh = one - bmix;
    m.dark = m.dark_all = ldark;
    m.dark_k = ldark ? frac2(block_slope(ltau)) : 0;
    metal_filters(m, v.lbase);
}
inline void add_jingle(Voice& v, i32 lat, i32 level) {
    Metal& m = v.metal;
    if (m.bursts >= burst_count) return;
    m.at[m.bursts] = samples(lat - v.lbase);
    m.level[m.bursts] = level;
    ++m.bursts;
}
// The metal's source for a block: the partials, in two passes of three
// oscillators (so their phases stay in registers), at U, and the noise, by
// its share, into w.
inline void metal_partials(Metal& m, i32* w) {
    {
        u32 p0 = m.ph[0], p1 = m.ph[1], p2 = m.ph[2];
        const u32 i0 = m.inc[0], i1 = m.inc[1], i2 = m.inc[2];
        for (unsigned i = 0; i < frames; ++i) {
            p0 += i0; p1 += i1; p2 += i2;
            // each square is +-1 by its top bit, and one pair ring-modulates
            w[i] = static_cast<i32>((p0 >> 31) + (p1 >> 31) + (p2 >> 31) + ((p0 ^ p1) >> 31));
        }
        m.ph[0] = p0; m.ph[1] = p1; m.ph[2] = p2;
    }
    {
        u32 p3 = m.ph[3], p4 = m.ph[4], p5 = m.ph[5];
        const u32 i3 = m.inc[3], i4 = m.inc[4], i5 = m.inc[5];
        const i32 sq = multiply(one - m.nmix, U / 9), nm = m.nmix;
        u32 seed = m.seed;
        for (unsigned i = 0; i < frames; ++i) {
            p3 += i3; p4 += i4; p5 += i5;
            const u32 b = (p3 >> 31) + (p4 >> 31) + (p5 >> 31) + ((p3 ^ p4) >> 31) + ((p4 ^ p5) >> 31);
            w[i] = ((static_cast<i32>(b) + w[i]) * 2 - 9) * sq + multiply(noise(seed), nm);   // the partials -9..9
        }
        m.ph[3] = p3; m.ph[4] = p4; m.ph[5] = p5;
        m.seed = seed;
    }
}
// The metal's filters for samples from..to of its source w, at level a, into x.
inline void metal_run(Metal& m, const i32* w, i32* x, unsigned from, unsigned to, i32 a) {
    i32 sl = m.sl, sb = m.sb;
    const i32 f = m.f, q = m.q;
    const i32 gh = multiply(m.gh, a), gb = multiply(m.gb, a);
    for (unsigned i = from; i < to; ++i) {
        const i32 in = w[i];
        sl += multiply(f, sb);
        const i32 qb = multiply(q, sb);
        const i32 h = in - sl - qb - qb;
        sb += multiply(f, h);
        x[i] = add(x[i], multiply(h, gh) + multiply(sb, gb));
    }
    m.sl = sl;
    m.sb = sb;
}
inline void metal_block(Voice& v, i32* x, bool retune) {
    Metal& m = v.metal;
    if (!m.on) return;
    if (m.dark_k) {
        // the top falls from ll towards ll - dark: dark counts down the fall still to come
        m.dark = multiply(m.dark, m.dark_k);
        retune = true;
    }
    if (retune) metal_filters(m, v.lrate);
    v.work |= W_METAL;
    i32 w[frames];
    metal_partials(m, w);
    unsigned split = frames;
    if (m.next < m.bursts && m.at[m.next] < v.hit_t + frames) split = m.at[m.next] > v.hit_t ? m.at[m.next] - v.hit_t : 0;
    if (split < frames) {
        v.work |= W_SPLIT;
        if (split) metal_run(m, w, x, 0, split, multiply(m.env.amp, m.amp));
        m.env.lvl = m.level[m.next];
        m.env.atk = 65536;   // a jingle strikes at once
        m.env.slope = block_slope(m.ldecay);
        ++m.next;
        env_step(m.env, v.r16);
        metal_run(m, w, x, split, frames, multiply(m.env.amp, m.amp));
        return;
    }
    env_step(m.env, v.r16);
    metal_run(m, w, x, 0, frames, multiply(m.env.amp, m.amp));
    if (m.env.atk >= 65536 && m.env.lvl <= env_floor && m.next >= m.bursts) m.on = 0;
}

// ------------------------------------------------------------ the room
// A small room: four lines whose lengths follow the record's speed (never
// longer than at 45 rpm), a Householder mix, each line darkened and losing
// level so the room rings 2^lrt seconds (to -60 dB); the room microphone's
// share rmix. It runs at an eighth of the sample rate (the room is dark).
__attribute__((cold)) inline void set_room(Voice& v, i32 send, i32 lrt, i32 ldamp, i32 rmix) {
    const i32 scale = static_cast<i32>(pow2(clamp(16 * octave - v.lbase, 15 * octave, 16 * octave)));   // Q16, at most 1
    for (u32 k = 0; k < 4; ++k) {
        u32 n = (static_cast<u32>(room_lines[k]) * static_cast<u32>(scale)) >> 16;
        if (n < 4) n = 4;
        v.rlen[k] = n;
        v.rlp[k] = 0;
        // loss a pass: 2^(-n log2(1000) / (rt SR/8))
        const i32 l = -static_cast<i32>(pow2(clamp(log2u(n) + L(9.965784) - lrt - (lsr - 3 * octave) + v.lbase + 16 * octave, 0, 21 * octave)));
        v.rg[k] = frac2(l);
    }
    v.rpos = 0;
    u32* words = reinterpret_cast<u32*>(v.room);
    for (u32 i = 0; i < room_samples / 2; ++i) words[i] = 0;
    v.rk = onepole_k(clamp(ldamp + v.lbase + 3 * octave, L(40), L(20000)));   // at an eighth of the rate
    v.send = send;
    v.rmix = rmix;
    v.rprev = 0;
}
// The room's block, from x (the close microphone): its eight outputs, one
// each eight samples, into y[1..8], and the last block's in y[0].
inline void room_run(Voice& v, const i32* x, i32* ry) {
    v.work |= W_ROOM;
    ry[0] = v.rprev;
    const i32 k = v.rk, send = v.send, mixg = v.rmix, tail = v.tail;
    i16* const r = v.room;
    u32 w = v.rpos;
    const u32 n0 = v.rlen[0], n1 = v.rlen[1], n2 = v.rlen[2], n3 = v.rlen[3];
    i32 l0 = v.rlp[0], l1 = v.rlp[1], l2 = v.rlp[2], l3 = v.rlp[3];
    const i32 g0 = v.rg[0], g1 = v.rg[1], g2 = v.rg[2], g3 = v.rg[3];
    for (unsigned j = 0; j < frames / 8; ++j) {
        const i32* xs = x + 8 * j;
        const i32 in = multiply((xs[0] >> 2) + (xs[2] >> 2) + (xs[4] >> 2) + (xs[6] >> 2) + tail, send);
        const i32 s0 = r[((w - n0) & 31u) << 2] * 8192, s1 = r[(((w - n1) & 31u) << 2) + 1] * 8192;
        const i32 s2 = r[(((w - n2) & 31u) << 2) + 2] * 8192, s3 = r[(((w - n3) & 31u) << 2) + 3] * 8192;
        const i32 sum = (s0 >> 1) + (s1 >> 1) + (s2 >> 1) + (s3 >> 1);   // Householder: x - (2/4) sum
        l0 += multiply(s0 - sum + in - l0, k);
        l1 += multiply(s1 - sum - in - l1, k);
        l2 += multiply(s2 - sum + in - l2, k);
        l3 += multiply(s3 - sum - in - l3, k);
        i16* const o = r + ((w & 31u) << 2);
        o[0] = static_cast<i16>(clamp(multiply(l0, g0) >> 13, -32767, 32767));
        o[1] = static_cast<i16>(clamp(multiply(l1, g1) >> 13, -32767, 32767));
        o[2] = static_cast<i16>(clamp(multiply(l2, g2) >> 13, -32767, 32767));
        o[3] = static_cast<i16>(clamp(multiply(l3, g3) >> 13, -32767, 32767));
        ++w;
        ry[j + 1] = multiply((s0 >> 2) + (s1 >> 2) - (s2 >> 2) - (s3 >> 2), mixg);
    }
    v.rpos = w;
    v.rlp[0] = l0; v.rlp[1] = l1; v.rlp[2] = l2; v.rlp[3] = l3;
    v.rprev = ry[frames / 8];
}
// The room's outputs ry back at the full rate, a straight line between them,
// added into x.
inline void room_add(const i32* ry, i32* x) {
    for (unsigned j = 0; j < frames / 8; ++j) {
        const i32 prev = ry[j], y = ry[j + 1], d = (y - prev) >> 3;
        i32* xs = x + 8 * j;
        xs[0] = add(xs[0], prev + d);
        xs[1] = add(xs[1], prev + 2 * d);
        xs[2] = add(xs[2], prev + 3 * d);
        xs[3] = add(xs[3], prev + 4 * d);
        xs[4] = add(xs[4], prev + 5 * d);
        xs[5] = add(xs[5], prev + 6 * d);
        xs[6] = add(xs[6], prev + 7 * d);
        xs[7] = add(xs[7], y);
    }
}
}
