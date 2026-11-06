#pragma once
// DUST's fifteen hits: each one the drum as it was played in a small room in
// 1970 and caught by two microphones, set up from the bar of the break the
// needle drops on (PICK: how hard that bar was played, and from bar 7 the
// band bleeding into the microphones). Frequencies and times are those of the
// record at 45 rpm; the voice scales them by the platter's speed.
//
// The figures are the instruments' own physics, written as formulas: a
// circular membrane's mode ratios (1, 1.59, 2.14, 2.30, 2.65), a stiff
// string's partials n f0 sqrt(1 + B n^2), a plucked string's weights
// sin(n pi beta) / n, a bell's and a cymbal's inharmonic sets; nothing is
// sampled.
#include "voice.h"
#include "record.h"

// (A note's setup runs once, in a block of its own: it is marked cold, so
// the compiler keeps it small rather than fast.)

namespace dust {
// What the needle drops on: the bar's force (Q16), the bar itself (1..8)
// and the band's bleed (Q16).
struct Pick {
    i32 force, bar, bleed;
};
// a + (b - a) t for logs (Q16) and t a Q16 unit
inline i32 lmix(i32 a, i32 b, i32 t) { return a + mul16(b - a, clamp(t, 0, 65536)); }
// a filter's damping 1/Q (0.3 ringing .. 1.9 broad) as the layers take it, Q30
constexpr i32 D(double q) { return Q31(q / 2); }
// t squared, Q16
inline i32 sq(i32 t) { t = clamp(t, 0, 65536); return static_cast<i32>((static_cast<u32>(t) * static_cast<u32>(t)) >> 16); }
// the share of t between a and b (Q16 unit), 0 below a and 1 above b
// (no divide at run time: the span's reciprocal is folded by the compiler)
template <i32 A, i32 B> inline i32 span(i32 t) {
    constexpr i32 k = static_cast<i32>((65536.0 * 1024.0) / (B - A));   // Q10
    return clamp(((t - A) >> 2) * k >> 8, 0, 65536);
}
// a Q31 amplitude times a Q16 unit
inline i32 times(i32 a, i32 t) { return multiply(a, q31(t)); }

// The six partials of a hat's two cymbals (log2 ratios over the lowest):
// 317, 431, 587, 709, 853 and 1124 Hz, squares whose odd harmonics and the
// pairs' ring modulation fill the top octaves densely.
const i32 hat_ratios[6] = {0, L(431.0 / 317), L(587.0 / 317), L(709.0 / 317), L(853.0 / 317), L(1124.0 / 317)};
const i32 ride_ratios[6] = {0, L(1.37), L(1.83), L(2.41), L(2.89), L(3.61)};
const i32 crash_ratios[6] = {0, L(1.29), L(1.71), L(2.23), L(2.77), L(3.38)};
const i32 tamb_ratios[6] = {0, L(1.21), L(1.53), L(1.87), L(2.33), L(2.71)};

// A closed hat's metal (also the hats bleeding into the snare's microphone).
// The stick's tip on the top cymbal for the soft bars (dark, tight); its
// shoulder on the edge for the hard ones (brighter, wider, ringing longer).
inline void hat_metal(Voice& v, i32 amp, i32 f) {
    set_metal(v, amp, L(317), hat_ratios, lmix(L(6600), L(4600), f), lmix(L(9500), L(16000), f), lmix(Q31(0.45), Q31(0.25), f), L(0.0003), lmix(L(0.035), L(0.08), f));
    metal_tone(v, lmix(L(6500), L(5200), f), D(0.45), lmix(Q31(0.5), Q31(0.25), f), L(1.7), L(0.05));
}
// The kick drum under another hit, from the band's last bars.
inline void kick_under(Voice& v, u32 k, i32 bleed, i32 lc) {
    set_mode(v, k, L(57), L(0.16), times(Q31(0.55), bleed), lc, false);
    set_mode(v, k + 1, L(118), L(0.05), times(Q31(0.25), bleed), lc, false);
}

__attribute__((cold)) inline void setup_kick(Voice& v, const Pick& p) {
    const i32 f = p.force;
    // a felt beater on a ported 22-inch: a long, soft contact, harder bars shorter
    const i32 lc = lmix(L(0.0048), L(0.0022), f);
    // the beater's slap on the head
    strike(v, lc, U / 8, mix(Q31(0.20), Q31(0.70), f), L(0.0009), L(1400), L(6000));
    v.direct_pulse = Q31(0.20);
    v.direct_click = Q31(0.45);
    // the head's tension rises as it is struck: the pitch falls back in 20 ms
    v.glide = lmix(ST(3.0), ST(7.0), f);
    set_mode(v, 0, L(58), lmix(L(0.13), L(0.11), f), Q31(1.0), lc, true);
    set_mode(v, 1, L(58 * 1.59), L(0.050), Q31(0.45), lc, true);
    set_mode(v, 2, L(58 * 2.14), L(0.038), Q31(0.32), lc, true);
    set_mode(v, 3, L(58 * 2.65), L(0.030), Q31(0.22), lc, true);
    // the shell and the port's air: the body a small speaker carries
    set_mode(v, 4, L(178), L(0.045), Q31(0.38), lc, false);
    set_mode(v, 5, L(330), L(0.018), mix(Q31(0.10), Q31(0.30), f), lc, false);
    // bar 8: the bass player's note under it
    set_mode(v, 6, L(82.41), L(0.40), times(Q31(0.55), p.bleed), lc, false);
    set_mode(v, 7, L(164.8), L(0.20), times(Q31(0.22), p.bleed), lc, false);
    // the felt's thud
    set_layer(v, 0, mix(Q31(0.25), Q31(0.45), f), L(420), D(1.4), 0, Q31(0.9), 0, L(0.0004), L(0.010));
    set_room(v, Q31(0.10), L(0.32), L(3000), Q31(0.6));
    v.comp_thr = U + U / 4;
}

__attribute__((cold)) inline void setup_snare(Voice& v, const Pick& p) {
    const i32 f = p.force;
    const i32 lc = lmix(L(0.0014), L(0.0005), f);
    strike(v, lc, U / 8, mix(Q31(0.30), Q31(0.90), f), L(0.0006), L(2500), L(12000));
    v.direct_pulse = Q31(0.15);
    v.direct_click = Q31(0.60);
    v.glide = lmix(ST(0.5), ST(2.0), f);
    // the batter head's modes, the snare side and shell, and the head's ring
    set_mode(v, 0, L(205), lmix(L(0.06), L(0.075), f), Q31(1.0), lc, true);
    set_mode(v, 1, L(205 * 1.59), L(0.055), Q31(0.60), lc, true);
    set_mode(v, 2, L(205 * 2.14), L(0.045), Q31(0.55), lc, true);
    set_mode(v, 3, L(350), L(0.050), Q31(0.38), lc, false);
    set_mode(v, 4, L(930), L(0.15), mix(Q31(0.03), Q31(0.09), f), lc, false);
    // the wires: shaped noise with their own decay, buzzing as the head moves
    set_layer(v, 0, mix(Q31(0.60), Q31(1.0), f), L(5200), D(1.1), 0, Q31(0.45), Q31(0.7), L(0.0010), lmix(L(0.11), L(0.16), f));
    v.layer[0].follow = Q31(0.4);
    // the crack; from bar 7 on, the hats in the snare's microphone take its place
    set_layer(v, 1, mix(mix(Q31(0.3), Q31(0.6), f), Q31(0.5), q31(p.bleed)), lmix(L(1600), L(6400), p.bleed), lmix(D(1.3), D(0.9), p.bleed),
              0, mix(Q31(0.8), Q31(0.3), q31(p.bleed)), mix(0, Q31(0.9), q31(p.bleed)), L(0.0002), lmix(L(0.020), L(0.045), p.bleed));
    set_room(v, Q31(0.42), L(0.6), L(4500), Q31(0.85));
}

__attribute__((cold)) inline void setup_rim(Voice& v, const Pick& p) {
    const i32 f = p.force;
    // early bars a cross-stick (the stick laid on the head, its butt on the
    // hoop), late bars a rimshot (head and hoop at once)
    const i32 shot = sq(span<Q16(0.35), Q16(1.0)>(f));
    const i32 lc = lmix(L(0.0012), L(0.0005), f);
    strike(v, lc, U / 8, mix(Q31(0.50), Q31(1.0), f), L(0.0006), L(1500), L(12000));
    v.direct_pulse = Q31(0.10);
    v.direct_click = Q31(0.65);
    v.glide = times(ST(1.5), shot);
    set_mode(v, 0, L(470), L(0.030), mix(Q31(0.80), Q31(0.30), shot), lc, false);
    set_mode(v, 1, L(1130), L(0.020), mix(Q31(0.50), Q31(0.25), shot), lc, false);
    set_mode(v, 2, L(1700), L(0.014), mix(Q31(0.35), Q31(0.20), shot), lc, false);
    set_mode(v, 3, L(185), lmix(L(0.04), L(0.10), shot), mix(Q31(0.25), Q31(1.0), shot), lc, true);
    set_mode(v, 4, L(185 * 1.59), L(0.08), mix(Q31(0.10), Q31(0.70), shot), lc, true);
    // the hoop's crack, a rimshot's
    set_mode(v, 5, L(2350), L(0.035), times(Q31(0.50), shot), lc, false);
    set_layer(v, 0, mix(Q31(0.08), Q31(0.60), shot), L(5000), D(1.1), 0, Q31(0.45), Q31(0.7), L(0.0015), L(0.15));
    v.layer[0].follow = Q31(0.5);
    set_room(v, mix(Q31(0.18), Q31(0.30), shot), L(0.4), L(4500), Q31(0.8));
}

__attribute__((cold)) inline void setup_hat(Voice& v, const Pick& p) {
    const i32 f = p.force;
    strike(v, L(0.0005), U / 8, mix(Q31(0.10), Q31(0.30), f), L(0.0004), L(3000), L(14000));
    v.direct_pulse = 0;
    v.direct_click = Q31(0.4);
    hat_metal(v, Q31(1.0), f);
    // the two cymbals' chick, strongest from a light foot
    set_layer(v, 0, mix(Q31(0.45), Q31(0.12), f), L(1250), D(1.0), Q31(0.2), Q31(0.8), 0, L(0.0004), L(0.007));
    kick_under(v, 0, p.bleed, L(0.004));
    set_room(v, Q31(0.20), L(0.35), L(6000), Q31(0.7));
}

__attribute__((cold)) inline void setup_open(Voice& v, const Pick& p) {
    const i32 f = p.force;
    strike(v, L(0.0006), U / 8, mix(Q31(0.20), Q31(0.45), f), L(0.0005), L(3000), L(14000));
    v.direct_pulse = 0;
    v.direct_click = Q31(0.4);
    set_metal(v, Q31(0.85), L(317), hat_ratios, lmix(L(6200), L(4000), f), lmix(L(10000), L(16000), f), lmix(Q31(0.45), Q31(0.25), f), L(0.0008), lmix(L(0.3), L(0.7), f));
    metal_tone(v, lmix(L(6500), L(5000), f), D(0.5), lmix(Q31(0.5), Q31(0.25), f), L(1.4), L(0.4));
    // the two cymbals rattling against each other
    set_layer(v, 0, Q31(0.20), L(6500), D(1.2), 0, Q31(0.3), Q31(0.9), L(0.004), L(0.35));
    kick_under(v, 0, p.bleed, L(0.004));
    set_room(v, Q31(0.18), L(0.4), L(5000), Q31(0.7));
}

__attribute__((cold)) inline void setup_ride(Voice& v, const Pick& p) {
    const i32 f = p.force;
    // the stick on the bow, and from the late bars on the bell
    const i32 bell = span<Q16(0.55), Q16(1.0)>(f);
    const i32 bow = sq(65536 - bell);
    const i32 lc = lmix(L(0.0006), L(0.0003), f);
    strike(v, lc, U / 8, mix(Q31(0.25), Q31(0.50), f), L(0.0004), L(3000), L(15000));
    v.direct_pulse = 0;
    v.direct_click = Q31(0.4);
    // the ping: a cluster of the bow's partials, quick to fade into the wash
    // (a soft bar's tip barely pings; a hard one rings it)
    const i32 ping = mul16(bow, 16384 + mul16(f, 49152));
    set_mode(v, 0, L(2860), L(0.30), times(Q31(0.30), ping), lc, false);
    set_mode(v, 1, L(3410), L(0.22), times(Q31(0.26), ping), lc, false);
    set_mode(v, 2, L(4130), L(0.18), times(Q31(0.21), ping), lc, false);
    set_mode(v, 3, L(5290), L(0.12), times(Q31(0.16), ping), lc, false);
    // the bell: lower, stronger, ringing on
    set_mode(v, 4, L(622), L(0.9), times(Q31(0.45), bell), lc, false);
    set_mode(v, 5, L(1487), L(0.7), times(Q31(0.36), bell), lc, false);
    set_mode(v, 6, L(2311), L(0.55), times(Q31(0.26), bell), lc, false);
    set_mode(v, 7, L(3180), L(0.40), times(Q31(0.18), bell), lc, false);
    // the wash: the whole cymbal swelling after the stick, darkening as it rings
    set_metal(v, mix(mix(Q31(0.15), Q31(0.70), f), Q31(0.30), q31(bell)), L(267), ride_ratios, lmix(L(5200), L(3000), f), lmix(L(7000), L(15000), f), Q31(0.25), lmix(L(0.03), L(0.006), f), lmix(L(0.8), L(1.6), f));
    metal_tone(v, L(5200), D(0.4), Q31(0.4), L(1.0), L(0.9));
    set_room(v, Q31(0.15), L(0.45), L(5000), Q31(0.7));
}

__attribute__((cold)) inline void setup_crash(Voice& v, const Pick& p) {
    const i32 f = p.force;
    // a soft bar is a swell from the stick's tip; a hard one a crash on the edge
    strike(v, L(0.0008), U / 8, mix(Q31(0.15), Q31(0.90), f), L(0.0006), L(2000), L(15000));
    v.direct_pulse = 0;
    v.direct_click = Q31(0.5);
    set_metal(v, Q31(0.95), L(289), crash_ratios, lmix(L(3400), L(2200), f), lmix(L(9000), L(16500), f), Q31(0.45), lmix(L(0.012), L(0.001), f), lmix(L(0.9), L(1.5), f));
    metal_tone(v, lmix(L(3600), L(4600), f), D(0.6), Q31(0.3), L(1.5), L(0.7));
    // the cymbal's body: its low modes, struck hard, gone quickly
    set_mode(v, 0, L(410), L(0.14), Q31(0.22), L(0.0008), false);
    set_mode(v, 1, L(640), L(0.11), Q31(0.18), L(0.0008), false);
    kick_under(v, 2, p.bleed, L(0.0008));
    set_room(v, Q31(0.20), L(0.5), L(5000), Q31(0.7));
}

__attribute__((cold)) inline void setup_tom(Voice& v, const Pick& p) {
    const i32 f = p.force;
    const i32 lc = lmix(L(0.0035), L(0.0007), f);
    strike(v, lc, U / 8, mix(Q31(0.12), Q31(0.95), f), L(0.0007), L(1500), L(9000));
    v.direct_pulse = Q31(0.10);
    v.direct_click = Q31(0.5);
    v.glide = lmix(ST(1.0), ST(5.0), f);
    set_mode(v, 0, L(118), L(0.22), Q31(1.0), lc, true);
    set_mode(v, 1, L(118 * 1.59), L(0.12), Q31(0.50), lc, true);
    set_mode(v, 2, L(118 * 2.14), L(0.09), Q31(0.40), lc, true);
    set_mode(v, 3, L(118 * 2.65), L(0.065), Q31(0.24), lc, true);
    set_mode(v, 4, L(450), L(0.04), Q31(0.18), lc, false);
    // the stick's wood on the head
    set_layer(v, 1, mix(Q31(0.08), Q31(0.50), f), L(1100), D(1.3), 0, Q31(0.8), 0, L(0.0003), L(0.015));
    // bar 8: the snare's wires answering it
    set_layer(v, 0, times(Q31(0.40), p.bleed), L(4500), D(1.1), 0, Q31(0.5), Q31(0.7), L(0.003), L(0.12));
    v.layer[0].follow = Q31(0.8);
    set_room(v, Q31(0.28), L(0.5), L(4000), Q31(0.8));
}

// A hand drum: early bars the palm's bass tone, then the open tone, the
// last bars the slap.
__attribute__((cold)) inline void hand_drum(Voice& v, const Pick& p, i32 lf0, i32 ltau, i32 lbass) {
    const i32 f = p.force;
    const i32 bass = lbass ? sq(65536 - span<0, Q16(0.35)>(f)) : 0;
    const i32 slap = span<Q16(0.65), Q16(1.0)>(f);
    // (the hand's contact as short, against the drum's pitch, on the smaller drum)
    const i32 lc = lmix(L(0.0040), L(0.0009), f) - (lf0 - L(212));
    // the hand's skin on the head: a soft thud, a slap's crack
    strike(v, lc, U / 8, mix(Q31(0.10), Q31(1.0), slap), lmix(L(0.0015), L(0.003), slap), lmix(L(500), L(1100), slap), L(7000));
    v.direct_pulse = Q31(0.12);
    v.direct_click = Q31(0.7);
    v.glide = ST(0.8);
    // the slap's fingers stay on the head and damp it
    const i32 damp = mul16(L(2.2), slap) + mul16(L(2.0), bass);
    set_mode(v, 0, lf0, ltau - damp, mix(Q31(1.0), Q31(0.55), slap), lc, true);
    set_mode(v, 1, lf0 + L(1.52), ltau - L(2.2) - damp, Q31(0.45), lc, true);
    set_mode(v, 2, lf0 + L(2.17), ltau - L(3.2) - damp, Q31(0.28), lc, true);
    if (lbass) set_mode(v, 3, lbass, L(0.07), times(Q31(0.9), bass), lc, false);
    // the shell's wood
    set_mode(v, 4, lf0 + L(3.4), L(0.03), Q31(0.18), lc, false);
    set_mode(v, 5, lf0 + L(7.1), L(0.020), times(Q31(0.45), slap), lc, false);
    set_mode(v, 6, lf0 + L(11.3), L(0.016), times(Q31(0.35), slap), lc, false);
    set_layer(v, 1, mix(Q31(0.10), Q31(0.55), slap), L(2000), D(1.3), 0, Q31(0.8), Q31(0.2), L(0.0002), lmix(L(0.006), L(0.012), slap));
    set_room(v, Q31(0.22), L(0.45), L(5000), Q31(0.7));
}
__attribute__((cold)) inline void setup_conga(Voice& v, const Pick& p) { hand_drum(v, p, L(212), L(0.17), L(110)); }
__attribute__((cold)) inline void setup_bongo(Voice& v, const Pick& p) { hand_drum(v, p, L(430), L(0.09), 0); }

__attribute__((cold)) inline void setup_tamb(Voice& v, const Pick& p) {
    const i32 f = p.force;
    strike(v, L(0.002), U / 8, mix(Q31(0.05), Q31(0.25), f), L(0.001), L(800), L(6000));
    v.direct_pulse = Q31(0.05);
    v.direct_click = Q31(0.3);
    set_metal(v, Q31(0.85), L(1270), tamb_ratios, lmix(L(5500), L(4200), f), L(15500), Q31(0.30), L(0.0002), lmix(L(0.025), L(0.045), f));
    metal_tone(v, L(6200), D(0.35), Q31(0.4), 0, 0);
    v.metal.env.lvl = -24 * octave;
    // the jingles: each pair strikes and bounces, a burst at a time
    static const unsigned char gap[8] = {0, 9, 17, 27, 35, 46, 55, 68};
    const u32 n = 3 + static_cast<u32>(mul16(5, f));
    const i32 spread = lmix(L(1.5), L(0.8), f);
    for (u32 k = 0; k < n && k < 8; ++k)
        add_jingle(v, log2u(gap[k] ? gap[k] : 1) - L(1000.0) + spread + (gap[k] ? 0 : -8 * octave), -static_cast<i32>(k) * lmix(Q16(0.45), Q16(0.3), f));
    // the frame and the head
    set_mode(v, 0, L(290), L(0.035), mix(Q31(0.02), Q31(0.40), sq(f)), L(0.002), false);
    set_mode(v, 1, L(530), L(0.025), Q31(0.10), L(0.002), false);
    set_room(v, Q31(0.22), L(0.45), L(5000), Q31(0.7));
}

__attribute__((cold)) inline void setup_cowbell(Voice& v, const Pick& p) {
    const i32 f = p.force;
    const i32 lc = lmix(L(0.0012), L(0.0004), f);
    strike(v, lc, U / 8, mix(Q31(0.20), Q31(0.60), f), L(0.0005), L(2000), L(12000));
    v.direct_pulse = Q31(0.05);
    v.direct_click = Q31(0.5);
    // a soft bar damps it in the hand; a hard one lets it ring
    const i32 ld = lmix(L(0.45), L(1.0), f);
    set_mode(v, 0, L(548), L(0.20) + ld, Q31(1.0), lc, false);
    set_mode(v, 1, L(822), L(0.16) + ld, Q31(0.70), lc, false);
    set_mode(v, 2, L(1372), L(0.10) + ld, Q31(0.35), lc, false);
    set_mode(v, 3, L(1820), L(0.08) + ld, Q31(0.25), lc, false);
    set_mode(v, 4, L(2364), L(0.06) + ld, Q31(0.18), lc, false);
    set_mode(v, 5, L(3075), L(0.045) + ld, Q31(0.12), lc, false);
    set_room(v, Q31(0.18), L(0.4), L(5000), Q31(0.7));
}

__attribute__((cold)) inline void setup_clap(Voice& v, const Pick& p) {
    const i32 f = p.force;
    strike(v, L(0.001), U / 8, 0, L(0.001), L(800), L(6000));
    v.direct_pulse = 0;
    v.direct_click = 0;
    // several people clapping: each pair of hands its own burst and its own band
    set_layer(v, 0, Q31(1.0), lmix(L(700), L(1400), f), D(1.1), Q31(0.1), Q31(0.8), Q31(0.3), L(0.0008), lmix(L(0.014), L(0.007), f));
    v.layer[0].env.lvl = -24 * octave;
    static const unsigned char at[6] = {0, 7, 12, 19, 25, 31};
    static const signed char shift[6] = {0, 3, -2, 5, -4, 1};
    static const signed char drop[6] = {0, 3, 2, 5, 4, 7};
    const u32 people = 1 + static_cast<u32>(mul16(5, f) + (f > 60000 ? 1 : 0));
    const i32 spread = lmix(L(1.4), L(0.8), f);
    for (u32 k = 0; k < people && k < 6; ++k)
        add_burst(v, 0, at[k] ? log2u(at[k]) - L(1000.0) + spread : -12 * octave, -static_cast<i32>(drop[k]) * (octave / 10), ST(1) * shift[k]);
    // the claps' smear in the room
    // (one person clapping softly is mostly the room's smear)
    set_layer(v, 1, mix(Q31(0.8), Q31(0.35), f), lmix(L(1200), L(2000), f), D(1.3), 0, Q31(0.8), 0, L(0.008), lmix(L(0.12), L(0.08), f));
    // bar 8: the tambourine's overdub on the same beat
    if (p.bleed) {
        set_metal(v, times(Q31(0.5), p.bleed), L(1270), tamb_ratios, L(4500), L(15500), Q31(0.35), L(0.0002), L(0.035));
        v.metal.env.lvl = -24 * octave;
        static const unsigned char jg[5] = {1, 10, 19, 30, 41};
        for (u32 k = 0; k < 5; ++k) add_jingle(v, log2u(jg[k]) - L(1000.0), -static_cast<i32>(k) * Q16(0.4));
    }
    set_room(v, Q31(0.50), L(0.65), L(5000), Q31(0.95));
}

__attribute__((cold)) inline void setup_shaker(Voice& v, const Pick& p) {
    const i32 f = p.force;
    strike(v, L(0.001), U / 8, 0, L(0.001), L(800), L(6000));
    v.direct_pulse = 0;
    v.direct_click = 0;
    // the seeds: sparse hits through the gourd's resonance; they swell as the
    // shake begins and land together on the far wall
    set_layer(v, 0, Q31(1.0), lmix(L(2800), L(5600), f), D(1.0), 0, Q31(0.6), mix(Q31(0.3), Q31(0.8), f), lmix(L(0.035), L(0.012), f), lmix(L(0.022), L(0.04), f));
    v.layer[0].grain = static_cast<u32>(mix(Q31(0.20), Q31(0.45), f)) * 2u;
    v.layer[0].env.hold = 12;
    add_burst(v, 0, lmix(L(0.055), L(0.040), f), -lmix(Q16(0.7), Q16(0.0), f), 0);
    // the last bars: the maracas shaken back as well, a second landing
    if (p.bleed) add_burst(v, 0, L(0.105), -lmix(Q16(1.5), Q16(0.3), p.bleed), 0);
    set_layer(v, 1, Q31(0.20), L(6500), D(1.2), 0, Q31(0.3), Q31(0.9), L(0.02), L(0.04));
    set_room(v, Q31(0.20), L(0.4), L(5000), Q31(0.7));
}

__attribute__((cold)) inline void setup_bass(Voice& v, const Pick& p) {
    const i32 f = p.force;
    // the bassline: the note the bass player plays in each bar of the break
    static const signed char note[8] = {0, 3, 5, 7, 10, 12, 15, 0};
    const i32 l0 = L(55.0) + ST(1) * note[clamp(p.bar, 1, 8) - 1];
    const i32 lc = lmix(L(0.0042), L(0.0022), f);
    strike(v, lc, U / 8, mix(Q31(0.12), Q31(0.30), f), L(0.003), L(250), L(2200));
    v.direct_pulse = 0;
    v.direct_click = Q31(0.5);
    v.glide = ST(0.15);
    // a flatwound string, foam-muted: partials n f0 sqrt(1 + B n^2), plucked
    // a fifth of the way along (weights sin(n pi 0.22) / n), the upper ones
    // dying fast under the mute
    static const i32 ratio[6] = {L(1.0 * 1.000075), L(2.0 * 1.0003), L(3.0 * 1.000675), L(4.0 * 1.0012), L(5.0 * 1.001874), L(6.0 * 1.0027)};
    static const i32 weight[6] = {Q31(0.99), Q31(0.77), Q31(0.46), Q31(0.15), Q31(0.10), Q31(0.22)};
    static const i32 tau[6] = {L(0.75), L(0.39), L(0.23), L(0.15), L(0.11), L(0.085)};
    for (u32 k = 0; k < 6; ++k) set_mode(v, k, l0 + ratio[k], tau[k], multiply(weight[k], mix(Q31(0.8), Q31(1.0), f)), lc, true);
    // bar 8: the kick under it
    if (p.bleed) kick_under(v, 6, p.bleed, L(0.004));
    set_room(v, Q31(0.04), L(0.3), L(3000), Q31(0.5));
}

__attribute__((cold)) inline void setup_model(Voice& v, u32 m, const Pick& p) {
    switch (m) {
    case rec::KICK: setup_kick(v, p); break;
    case rec::SNARE: setup_snare(v, p); break;
    case rec::RIM: setup_rim(v, p); break;
    case rec::HAT: setup_hat(v, p); break;
    case rec::OPEN: setup_open(v, p); break;
    case rec::RIDE: setup_ride(v, p); break;
    case rec::CRASH: setup_crash(v, p); break;
    case rec::TOM: setup_tom(v, p); break;
    case rec::CONGA: setup_conga(v, p); break;
    case rec::BONGO: setup_bongo(v, p); break;
    case rec::TAMB: setup_tamb(v, p); break;
    case rec::COWBELL: setup_cowbell(v, p); break;
    case rec::CLAP: setup_clap(v, p); break;
    case rec::SHAKER: setup_shaker(v, p); break;
    default: setup_bass(v, p); break;
    }
}
}
