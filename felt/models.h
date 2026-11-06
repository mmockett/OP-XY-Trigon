#pragma once
// FELT's twelve models: how each one prepares the string, what plays it, and
// what its eight controls move. setup_model runs at the note's start;
// block_model renders a block of the voice's sound (before its level).
//
// A control arrives as a Q16 unit c[i], 0..65536. AGE is always slot 7 and
// PEDAL slot 6.
#include "voice.h"

namespace felt {
// ------------------------------------------------------------ shared pieces
const i32 cent = 55;   // log2 Q16 of a cent (65536 / 1200)
inline i32 lerp16(i32 a, i32 b, i32 t) { return a + mul16(b - a, clamp(t, 0, 65536)); }   // |b - a| < 2^24
inline i32 lunit(i32 t) { return clamp(t, 0, 65535) * 32768; }   // a Q16 unit as Q31
// Samples for a time given as log2 seconds (up to 40 s).
inline u32 samples(i32 lsec) { return (static_cast<u32>(exp16(clamp(lsec, -16 * octave, L(40.0)))) >> 4) * 44100u >> 12; }

// The key's settings every model shares: its pitch offset (TUNE and AGE's
// wander), its felt (SOFT, AGE) and its pedal.
struct Common {
    i32 lt;        // TUNE and AGE's wander, log2 Q16
    i32 age, soft, pedal;   // Q16 units
    i32 ltc_add;   // the contact's change, log2 s: SOFT longer, AGE's hard grooves shorter
    i32 detune;    // the unisons' half spread, log2 Q16
    i32 second;    // the second unison's share, Q31
    i32 tilt_add;  // SOFT darkens
};
static __attribute__((noinline)) Common common(Voice& v, const i32* c, i32 tune) {
    Common k;
    k.age = c[7];
    k.pedal = c[6];
    k.soft = v.soft * 4369;   // 0..15 as a Q16 unit
    const i32 wander = static_cast<i32>(lcg(v.seed) >> 16) - 32768;   // -1..1, Q15
    k.lt = tune * 5461 + mul16(mul16(wander * 2, k.age), 12 * cent);   // up to 12 cents either way
    k.ltc_add = mul16(k.soft, Q16(1.2)) - mul16(k.age, Q16(0.8));    // SOFT's fresh felt is softer; AGE's grooved felt harder
    k.detune = cent / 4 + mul16(k.age, 22 * cent);
    k.second = Q31(0.85) - mul(lunit(k.soft), Q31(0.75));
    k.tilt_add = mul16(k.soft, Q16(0.6));
    v.grain = mul(lunit(k.age), Q31(0.8));
    return k;
}
// A string's lowest partials, set up and struck at the note's start; the
// rest follow, a few a block (string_rest), each struck as it would be had it
// started with the others: the note's set-up spread over its first blocks.
const u32 split = 5;
inline i32 string_first(Voice& v, const StringSpec& s) {
    v.spec = s;
    v.spec_next = s.partials > split ? split + 1 : 0;
    return add_string(v, s, 1, split);
}
inline void strike_string(Voice& v, i32 ltc, i32 lscale) {
    v.spec_ltc = ltc;
    v.spec_lscale = lscale;
    strike(v, ltc, lscale);
}
inline void string_rest(Voice& v) {
    const u32 first = v.n, last = v.spec_next + split - 1;
    add_string(v, v.spec, v.spec_next, last);
    if (v.model == M::THUD || v.model == M::TOM)
        for (u32 k = first; k < v.n; ++k) v.glides |= 1u << k;
    strike(v, v.spec_ltc, v.spec_lscale, v.t);
    v.spec_next = last < v.spec.partials ? last + 1 : 0;
}
// The pedal: the sympathetic strings an octave, a twelfth and two octaves
// over lf0, and one neighbouring key, ringing for 1.5..4 s; and every damper
// lifted, so the struck string's own damper barely lands.
static __attribute__((noinline)) void pedal_strings(Voice& v, i32 lf0, i32 pedal) {
    v.npedal = 0;
    v.pedal_g = 0;
    if (pedal <= 0) return;
    // A bell's or a gong's partials meet the strings off their pitch and
    // barely move them: the prepared note's own unison answers there, in place
    // of the neighbouring key. A brush's noise meets them everywhere, faintly.
    static const i32 ratio[2][4] = {{L(2.0), L(3.0), L(4.0), L(2.0 * 1.0594630943592953)}, {L(2.0), L(3.0), L(4.0), 0}};
    // they ring a long while at their own pitch, so they answer loudly: coupled
    // gently, a halo a few dB under the note rather than a sound of its own
    static const i32 coupling[12] = {Q31(0.0008), Q31(0.0008), Q31(0.0008), Q31(0.0008), Q31(0.0008), Q31(0.0002),
                                     Q31(0.0035), Q31(0.0008), Q31(0.0035), Q31(0.0002), Q31(0.0008), Q31(0.0008)};
    const u32 m = v.model;
    const i32* r = ratio[m == M::BELL || m == M::BOLT];
    const i32 sigma = rate_of(lerp16(L(0.8), L(1.8), pedal));
    for (u32 k = 0; k < 4; ++k) {
        i32 lf = lf0 + r[k];
        while (lf > L(5000.0)) lf -= octave;
        add_pedal(v, lf, sigma);
    }
    v.pedal_g = mul(lunit(pedal), coupling[m]);
}
inline i32 damper_eased(i32 damper, i32 pedal) { return mul(damper, one - mul(lunit(pedal), Q31(0.7))); }
// The damper lands at DECAY's time (slot 1).
inline void damper_at(Voice& v, u32 m, i32 c1) { v.damp_at = samples(shape::along(shape::damper[m], c1)); }

// ------------------------------------------------------------ THUD, TOM
// A bass (THUD) or middle (TOM) string with rubber wedged close to its end,
// so its upper partials die at once and its fundamental thumps; a hard blow
// stretches it, so its pitch starts high and falls (GLIDE); the action's
// wood knocks under it (KNOCK).
static __attribute__((noinline)) void setup_thud(Voice& v, u32 m, const i32* c, const Common& k) {
    const bool tom = m == M::TOM;
    const i32 lf0 = shape::along(shape::pitch[m], c[0]) + k.lt;
    StringSpec s;
    s.lf0 = lf0;
    s.B = tom ? 5033 : 10066;   // 0.0003, 0.0006 (Q24)
    s.partials = 8;
    s.doubled = tom ? 3 : 2;
    s.detune = k.detune;
    s.beta = Q16(1.0 / 9.0);
    s.tilt = (tom ? Q16(0.75) : Q16(0.9)) + k.tilt_add - mul16(c[2], Q16(0.5));   // a harder felt, brighter partials
    s.sigma1 = tom ? Q16(1.0 / 1.6) : Q16(1.0 / 2.4);
    s.sigma_exp = Q16(1.0);
    s.rubber = c[3] * (tom ? 1300 : 3500);
    s.rubber_x = tom ? Q16(0.018) : Q16(0.009);
    s.mu = 0;
    s.mass_x = 0;
    s.damper = damper_eased(tom ? Q16(34.0) : Q16(26.0), k.pedal);
    s.second = k.second;
    s.scatter = mul16(k.age, 6 * cent);
    const i32 total = string_first(v, s);
    const i32 lscale = level_scale(total, Q31(0.95));
    v.glides = (1u << v.n) - 1;
    // GLIDE: up to an octave over, falling as the string's swing does (twice
    // its loss: the stretch goes as the square of the swing)
    const i32 w1 = mul(sinpi(static_cast<u32>(s.rubber_x)), sinpi(static_cast<u32>(s.rubber_x)));
    const i32 sigma1 = s.sigma1 + mul(s.rubber, w1) * 2;
    const i32 rate = clamp(sigma1 * 2 + Q16(1.0 / 0.05) + mul16(c[2], Q16(16.0)), Q16(1.0 / 0.1), Q16(1.0 / 0.016));   // a harder blow lets go faster
    v.bend = (exp16(c[4] * 2) - 65536) * 5461;   // Q30 / 3: 2^GLIDE - 1, GLIDE up to two octaves
    v.bend_k = decay_over(rate, frames);
    bend_modes(v);
    add_knock(v, mul(lunit(c[5]), tom ? Q31(0.7) : Q31(0.9)), (tom ? mul16(c[0], Q16(0.5)) : 0) + mul16(c[2], Q16(1.0)));   // a harder hammer knocks brighter
    i32 ltc = lerp16(L(0.006), L(0.0004), c[2]) + k.ltc_add;
    if (tom) ltc -= Q16(0.3);
    strike_string(v, ltc, lscale);
    damper_at(v, m, c[1]);
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ RATTLE
// A tenor string with paper woven through its unisons: wherever the string
// swings harder than the paper weighs, the paper buzzes against it (noise,
// gated by the string's own motion), in a band set by where it sits (SPOT).
static __attribute__((noinline)) void setup_rattle(Voice& v, const i32* c, const Common& k) {
    const i32 lf0 = shape::along(shape::pitch[M::RATTLE], c[0]) + k.lt;
    const i32 paper = c[3], spot = c[4];
    StringSpec s;
    s.lf0 = lf0;
    s.B = 3355;   // 0.0002
    s.partials = 8;
    s.doubled = 3;
    s.detune = k.detune;
    s.beta = Q16(1.0 / 13.0);
    s.tilt = Q16(1.1) + k.tilt_add - mul16(c[2], Q16(0.5));
    s.sigma1 = Q16(1.0 / 1.3);
    s.sigma_exp = Q16(0.9);
    s.rubber = paper * 9;             // the paper's own drag
    s.rubber_x = Q16(0.06) + mul16(spot, Q16(0.38));
    s.mu = mul16(paper, Q16(0.04));   // and its weight
    s.mass_x = s.rubber_x;
    s.damper = damper_eased(Q16(42.0), k.pedal);
    s.second = k.second;
    s.scatter = mul16(k.age, 6 * cent);
    const i32 total = string_first(v, s);
    const i32 lscale = level_scale(total, Q31(0.95));
    // the buzz: it starts once the string swings past bthr; the paper pressed
    // harder (PAPER up) buzzes sooner and longer
    v.bthr = mul(Q31(0.5) - mul(lunit(paper), Q31(0.48)), 1 << 24);
    v.blevel = mul(lunit(paper), Q31(0.55));
    v.bf = svf_f(L(1700.0) + mul16(spot, Q16(1.6)));
    v.bq = Q31(0.95);
    v.bhp.s = 0;
    // the paper slapped by the blow: a short crack in the same band
    v.wlevel = Q31(0.002);
    v.wk = decay_over(Q16(1.0 / 0.004), 1);
    v.k[0] = Q31(0.12) + mul(lunit(c[5]), Q31(0.88));   // RING
    v.k[1] = onepole_k(L(500.0));
    strike_string(v, lerp16(L(0.004), L(0.00025), c[2]) + k.ltc_add, lscale);
    damper_at(v, M::RATTLE, c[1]);
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ LID
// The fallboard dropped on the key slip: a wooden slap that bounces (BOUNCE
// slaps, the first gap SPREAD, each gap and slap a little smaller), then the
// case's ring dies (DECAY). Each slap is a burst of noise in the board's band
// (TUNE) and a knock of the board's own modes; HOLLOW opens the case under it.
static __attribute__((noinline)) void setup_lid(Voice& v, const i32* c, const Common& k) {
    const i32 lb = shape::along(shape::pitch[M::LID], c[0]) + k.lt;
    const i32 snap = c[4], hollow = c[5];
    v.high = 0;
    // the board's modes, driven by the slaps
    static const i32 ratio[3] = {L(0.34), L(0.71), L(1.27)};
    static const i32 tau[3] = {L(0.026), L(0.016), L(0.008)};
    static const i32 amp[3] = {Q31(0.08), Q31(0.07), Q31(0.05)};
    for (u32 j = 0; j < 3; ++j) {
        const i32 sg = rate_of(tau[j] - mul16(snap, Q16(0.5)));
        add_mode(v, lb + ratio[j], amp[j], sg, sg);
    }
    // the case's cavity, HOLLOW
    if (hollow > 0) {
        const i32 sg = rate_of(lerp16(L(0.02), L(0.11), hollow));
        add_mode(v, L(185.0) + mul16(c[0], Q16(0.6)), mul(lunit(hollow), Q31(0.12)), sg, sg);
        add_mode(v, L(410.0) + mul16(c[0], Q16(0.6)), mul(lunit(hollow), Q31(0.07)), sg * 2, sg * 2);
    }
    v.bent = 0;
    // the slaps
    const i32 bounces = 1 + ((c[2] * 5 + 32768) >> 16);
    v.left = static_cast<u32>(bounces);
    v.at = 0;
    v.samp = Q31(0.9);
    v.sk = shape::bounce_height - mul(lunit(k.age), Q31(0.15));
    v.sgap = samples(shape::along(shape::gap, c[3])) << 8;
    v.sgk = shape::bounce_gap;
    v.waiting = 1;
    // the noise: a burst a slap (SNAP shortens and brightens it), then the tail
    v.wf = svf_f(lb);
    v.wq = Q31(0.55) + mul(lunit(hollow), Q31(0.25));
    v.wk = decay_over(rate_of(lerp16(L(0.008), L(0.0008), snap)), 1);
    v.k[0] = decay_over(rate_of(shape::along(shape::damper[M::LID], c[1])), 1);   // the tail's loss a sample
    v.k[1] = Q31(0.22) + mul(lunit(hollow), Q31(0.12));                           // the tail's level
    v.k[3] = Q31(0.94) - mul(lunit(snap), Q31(0.14));                             // the burst's swell: 0.4 ms, a hard snap's 0.1 ms
    v.k[4] = 0;
    v.whpk = onepole_k(lerp16(L(200.0), L(1100.0), snap) - mul16(k.age, Q16(0.5)));
    v.k[2] = onepole_k(L(9000.0) - mul16(k.age, Q16(1.5)));
    v.k[6] = L(110.0) + mul16(c[0], Q16(1.0));   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ TACK
// A hammer with a drawing pin in its felt (the tack piano) on a string
// muted by felt: a click and a short, woody note.
static __attribute__((noinline)) void setup_tack(Voice& v, const i32* c, const Common& k) {
    const i32 lf0 = shape::along(shape::pitch[M::TACK], c[0]) + k.lt;
    const i32 tack = c[2], mute = c[3];
    StringSpec s;
    s.lf0 = lf0;
    s.B = 1678;   // 0.0001
    s.partials = 8;
    s.doubled = 3;
    s.detune = k.detune;
    s.beta = Q16(1.0 / 8.5);
    s.tilt = Q16(0.45) + k.tilt_add;
    s.sigma1 = Q16(1.0 / 1.4) + mul16(mute, Q16(4.0));
    s.sigma_exp = Q16(0.8);
    s.rubber = mute * 50;
    s.rubber_x = Q16(0.12) + mul16(c[5], Q16(0.36));   // SPOT: where the mute sits
    s.mu = mul16(mute, Q16(0.08));
    s.mass_x = s.rubber_x;
    s.damper = damper_eased(Q16(70.0), k.pedal);
    s.second = k.second;
    s.scatter = mul16(k.age, 6 * cent);
    const i32 total = string_first(v, s);
    const i32 lscale = level_scale(total, Q31(0.9));
    v.bent = 0;
    // the pin's own ring: a click
    if (tack > 0) {
        static const i32 hz[2] = {L(3900.0), L(5700.0)};
        for (u32 j = 0; j < 2; ++j) {
            const i32 sg = rate_of(L(0.006) - Q16(0.4) * static_cast<i32>(j));
            const i32 i = add_mode(v, hz[j], 0, sg, sg);
            if (i >= 0) v.mode[i].b = mul(mul(lunit(tack), Q31(0.45)), one - mul(lunit(k.soft), Q31(0.7))) / 48;   // SOFT: the shifted hammer meets the string beside its pin
        }
    }
    add_knock(v, mul(lunit(c[4]), Q31(0.5)), mul16(c[0], Q16(0.7)) - mul16(k.soft, Q16(1.0)), 2);   // and knocks duller
    strike_string(v, lerp16(L(0.6), L(0.08), tack) - lf0 + k.ltc_add, lscale);   // the felt's contact a share of the note's period, so a plain felt still sounds it
    damper_at(v, M::TACK, c[1]);
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ BELL, BOLT
// A screw (BELL, treble strings) or a bolt and nut (BOLT, bass and tenor)
// between the strings: its mass at SPOT bends the partials near their
// antinodes into a bell's or a gong's inharmonic set, it rings its own modes
// too, and loose (LOOSE) it chatters against the string.
static __attribute__((noinline)) void setup_screw(Voice& v, u32 m, const i32* c, const Common& k) {
    const bool bolt = m == M::BOLT;
    const i32 lf0 = shape::along(shape::pitch[m], c[0]) + k.lt;
    const i32 mass = c[3], loose = c[5];
    StringSpec s;
    s.lf0 = lf0;
    s.B = bolt ? 3355 : 840;
    s.partials = bolt ? 9 : 7;
    s.doubled = bolt ? 3 : 2;
    s.detune = k.detune + (bolt ? 2 * cent : cent / 2);
    s.beta = Q16(1.0 / 7.0);
    s.tilt = (bolt ? Q16(0.75) + k.tilt_add : Q16(0.6)) + k.tilt_add - mul16(c[2], Q16(0.45));   // SOFT darkens the bolt's long string twice as far
    s.sigma1 = bolt ? Q16(1.0 / 4.5) : Q16(1.0 / 2.6);
    s.sigma_exp = bolt ? Q16(0.9) : Q16(0.7);
    s.rubber = 0;
    s.rubber_x = 0;
    s.mu = bolt ? Q16(0.1) + mul16(mass, Q16(3.9)) : Q16(0.05) + mul16(mass, Q16(2.45));
    s.mass_x = Q16(0.06) + mul16(c[4], Q16(0.38));
    {
        // tuned so the prepared note sounds at TUNE: the mass pulls the
        // fundamental flat by sqrt(1 + 2 mu sin^2(pi x)), so the string is
        // drawn up by as much (the partials keep their ratios)
        const i32 w = mul(sinpi(static_cast<u32>(s.mass_x)), sinpi(static_cast<u32>(s.mass_x)));
        s.lf0 += log16(65536 + mul(s.mu, w) * 2) / 2;
    }
    s.damper = damper_eased(bolt ? Q16(14.0) : Q16(50.0), k.pedal);
    s.second = k.second;
    s.scatter = mul16(k.age, 6 * cent);
    const i32 total = string_first(v, s);
    const i32 lscale = level_scale(total, Q31(0.85));
    // the felt's contact: the bolt's in seconds; the bell's a share of its
    // note's period, so a soft felt still reaches its first partials at any TUNE
    const i32 ltc = (bolt ? lerp16(L(0.004), L(0.0005), c[2]) : lerp16(L(1.2), L(0.2), c[2]) - s.lf0) + k.ltc_add;
    strike_string(v, ltc, lscale);
    v.bent = 0;
    // the screw's or bolt's own ring, struck through the string: set up in
    // the third block (screw_modes)
    v.k[5] = mass;
    v.late = 1;
    // LOOSE: the nut backed off, chattering whenever the string swings past it
    {
        const i32 slack = one - lunit(loose);
        v.bthr = mul(mul(mul(slack, slack), Q31(0.8)), 1 << 25);
    }
    v.blevel = mul(lunit(loose), Q31(0.35));
    v.bf = svf_f(bolt ? L(900.0) : L(2300.0));
    v.bq = Q31(0.35);
    v.k[1] = onepole_k(L(300.0));
    damper_at(v, m, c[1]);
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// The screw's or bolt's own ring (MASS), struck two blocks late as it would be
// had it been struck at the note's start.
static __attribute__((noinline)) void screw_modes(Voice& v) {
    static const i32 hz[2][3] = {{L(3400.0), L(5100.0), L(6600.0)}, {L(1050.0), L(1720.0), L(2850.0)}};
    const u32 bolt = v.model == M::BOLT;
    const i32 mass = v.k[5];
    const i32 shift = -mul16(mass, Q16(0.6));
    const i32 soft = one - static_cast<i32>(v.soft) * (Q31(0.7) / 15);   // SOFT: the una corda's felt barely rings the metal
    const i32 sg = rate_of(bolt ? L(0.35) : L(0.16));
    for (u32 j = 0; j < 3; ++j)
        add_mode(v, hz[bolt][j] + shift, mul(mul(lunit(mass), Q31(0.22)) + Q31(0.03), soft), sg + Q16(4.0) * static_cast<i32>(j), sg + Q16(40.0));
    strike(v, v.spec_ltc, 0, v.t);
}

// ------------------------------------------------------------ BRUSH
// A stiff brush swept along the top strings, screws wedged in them: a dense
// set of high, inharmonic partials and the brush's own hiss, high-passed.
// DECAY is the damper: closed to open; STROKE a tick to a sweep; METAL from
// the wire's hiss to the strings' clang; BRISTLE fine to coarse; SPOT where
// along the strings it sweeps.
static __attribute__((noinline)) void setup_brush(Voice& v, const i32* c, const Common& k) {
    v.high = 1;
    const i32 lb = shape::along(shape::pitch[M::BRUSH], c[0]) + k.lt;
    const i32 metal = c[3], spot = c[5];
    const i32 ld = shape::along(shape::damper[M::BRUSH], c[1]);
    static const i32 ratio[8] = {L(1.0), L(1.153), L(1.297), L(1.481), L(1.627), L(1.842), L(2.07), L(2.35)};
    const i32 x = Q16(0.08) + mul16(spot, Q16(0.36));
    for (u32 j = 0; j < 8; ++j) {
        const i32 w = Q31(0.25) + mul(sinpi(static_cast<u32>(j + 2) * static_cast<u32>(x)), Q31(0.75));
        const i32 sg = rate_of(ld - Q16(0.12) * static_cast<i32>(j));
        add_mode(v, lb + ratio[j], mul(mul(w, lunit(metal)), Q31(0.012)), sg, sg);
    }
    v.bent = 0;
    // the stroke: the brush's noise rising over the stroke, then the damper's ring
    const i32 lstroke = shape::along(shape::stroke, c[2]);
    v.nrise = static_cast<i32>(samples(lstroke));
    v.nlevel = 0;
    v.ntarget = Q31(0.9);
    v.nk = decay_over(rate_of(ld - Q16(0.4)), 1);
    v.k[0] = mul(lunit(c[4]) - mul(lunit(c[4]), lunit(c[4])) / 2 + lunit(c[4]) / 2, Q31(0.92));   // BRISTLE: the share of the brush's noise that is silent (2b - b^2)
    v.k[1] = Q31(0.55) - mul(lunit(metal), Q31(0.4));   // the hiss's share
    v.k[2] = one;
    v.nhpk = onepole_k(lb - Q16(1.4) + mul16(spot, Q16(1.6)));   // SPOT: low on the strings, darker; high, brighter
    v.hpk = onepole_k(lb - Q16(0.9));
    v.waiting = 1;
    v.k[6] = lb - Q16(1.6);   // the pedal's strings (the top strings' own, under the brush), tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ PLANK
// A plank pressed on a run of keys (WIDTH: two to twenty-four of them),
// dozens of strings at once: a wash that blooms (BLOOM) as the plank settles
// and the strings set each other ringing; BOLTS wedged among them make it
// metal.
static __attribute__((noinline)) void setup_cluster(Voice& v, const i32* c, const Common& k) {
    const i32 lf0 = shape::along(shape::pitch[M::PLANK], c[0]) + k.lt;
    const i32 width = c[2], bolts = c[5];
    const i32 span = Q16(2.0 / 12.0) + mul16(width, Q16(22.0 / 12.0));   // octaves from lowest to highest key
    const i32 ld = shape::along(shape::damper[M::PLANK], c[1]);
    static const unsigned char partial[2] = {1, 3};
    for (u32 j = 0; j < 6; ++j) {
        // six keys spread over the span, a little uneven
        const i32 lk = lf0 + span * static_cast<i32>(j) / 5 + ((static_cast<i32>(j * 37 % 11) - 5) * (3 * cent + mul16(k.age, 4 * cent)));
        for (u32 p = 0; p < 2; ++p) {
            const u32 n = partial[p];
            i32 lf = lk + tables::lnum[n];
            const i32 w = mul(sinpi(n * static_cast<u32>(Q16(0.17))), sinpi(n * static_cast<u32>(Q16(0.17))));
            lf -= mul16(bolts, log16(65536 + mul(Q16(1.8), w) * 2) / 2 + Q16(0.21) * static_cast<i32>(j & 1));
            const i32 sg = rate_of(L(2.0) - mul16(lf - lf0, Q16(0.9)));
            const i32 sd = sg + damper_eased(rate_of(ld - L(2.0)), k.pedal);
            add_mode(v, lf, p ? Q31(0.15) + mul(lunit(c[3]), Q31(0.4)) : Q31(0.6), sg, sd);   // HAMMER: a harder blow, more of the third partial
        }
    }
    v.bent = 0;
    v.spec_ltc = lerp16(L(0.006), L(0.0005), c[3]) + k.ltc_add;
    strike(v, v.spec_ltc);
    v.k[5] = bolts;   // the bolts' own ring, in the third block (cluster_bolts)
    v.late = 2;
    // the bloom: the strings swell from a quarter of their blow over BLOOM's
    // time as the plank settles and they set each other ringing (a growth
    // each block; then they ring down)
    const i32 lbloom = shape::along(shape::bloom, c[4]);
    const u32 blocks = samples(lbloom) / frames + 1;
    v.k[3] = static_cast<i32>(blocks);
    v.k[4] = clamp(exp16(2 * octave / static_cast<i32>(blocks < 2 ? 2 : blocks)) - 65536, 0, 65535) * 32768;   // 4x over the bloom: a block's growth - 1, Q31
    for (u32 j = 0; j < v.n; ++j) v.mode[j].l = v.mode[j].l / 4;
    // the plank's sizzle: noise rising over the bloom, then dying with the strings
    v.nrise = static_cast<i32>(samples(lbloom));
    v.nlevel = 0;
    v.ntarget = Q31(0.5);
    v.nk = decay_over(rate_of(ld - L(5.0)), 1);
    v.k[1] = Q31(0.22);   // the sizzle's share
    v.k[2] = 0;           // none of it drives the strings
    v.nhpk = onepole_k(L(3500.0));
    v.hpk = onepole_k(L(60.0));
    v.waiting = 1;
    v.damp_at = samples(ld);
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// The bolts wedged among the plank's strings (BOLTS), struck two blocks late as
// they would be had they been struck with the strings, a quarter as loud
// as they bloom.
static __attribute__((noinline)) void cluster_bolts(Voice& v) {
    const u32 first = v.n;
    const i32 bolts = v.k[5], sg = rate_of(L(0.6));
    add_mode(v, L(1330.0), mul(lunit(bolts), Q31(0.4)) + 1, sg, sg);
    add_mode(v, L(2210.0), mul(lunit(bolts), Q31(0.3)) + 1, sg, sg);
    strike(v, v.spec_ltc, 0, v.t);
    for (u32 j = first; j < v.n; ++j) {
        v.mode[j].l = v.mode[j].l / 4;
        v.mode[j].b = v.mode[j].b / 4;
    }
}

// ------------------------------------------------------------ CHAIN
// A chain of glass beads laid over the strings and shaken: each bead's knock
// rings the beads (and with METAL, the jingles of a tambourine's discs).
static __attribute__((noinline)) void setup_beads(Voice& v, const i32* c, const Common& k) {
    v.high = 1;
    const i32 lb = shape::along(shape::pitch[M::CHAIN], c[0]) + k.lt;
    const i32 metal = c[4];
    const i32 ld = shape::along(shape::damper[M::CHAIN], c[1]);
    static const i32 bead[4] = {L(1.0), L(1.37), L(1.79), L(2.31)};
    const i32 lbead = lerp16(L(0.0022), L(0.03), c[1]);   // DECAY rings the glass longer too
    for (u32 j = 0; j < 4; ++j) {
        const i32 sg = rate_of(lbead - Q16(0.3) * static_cast<i32>(j));
        // AGE: chipped beads, each its own pitch
        const i32 spread = mul16(k.age, Q16(0.45)) * (static_cast<i32>(j) * 2 - 3) / 3;
        add_mode(v, lb + bead[j] + spread, Q31(0.12), sg, sg);
    }
    static const i32 jingle[4] = {L(1.52), L(1.98), L(2.61), L(3.07)};
    for (u32 j = 0; j < 4; ++j) {
        const i32 sg = rate_of(ld - Q16(0.25) * static_cast<i32>(j));
        add_mode(v, lb - L(1.4) + jingle[j], mul(lunit(metal), Q31(0.06)), sg, sg);
    }
    v.bent = 0;
    // the shake: knocks at BEADS a second, over SHAKE's time, front-loaded or swelling (SWELL)
    v.nrise = static_cast<i32>(samples(shape::along(shape::shake, c[3])));
    v.k[0] = static_cast<i32>(pow2(lerp16(L(80.0), L(2600.0), c[2]) - lsr + 32 * octave));   // a knock's chance a sample, of 2^32
    v.k[1] = c[5];   // SWELL
    v.k[2] = decay_over(rate_of(lerp16(L(0.001), L(0.004), c[1]) + mul16(k.age, Q16(0.8))), 1);   // a knock's own hiss
    v.k[3] = Q31(0.3) - mul(lunit(metal), Q31(0.12));
    v.nhpk = onepole_k(lb - Q16(0.4));
    v.hpk = onepole_k(L(1200.0));
    v.waiting = 1;
    v.at = 0;
    v.k[6] = lb - Q16(1.8);   // the pedal's strings (the top strings the beads lie on), tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ PLUCK
// A fingernail plucks a bass string: a round pizzicato; MUTE lays the palm
// on it; BUZZ lets it buzz against a loose fitting.
static __attribute__((noinline)) void setup_pluck(Voice& v, const i32* c, const Common& k) {
    const i32 lf0 = shape::along(shape::pitch[M::PLUCK], c[0]) + k.lt;
    const i32 nail = c[2], mute = c[4], buzz = c[5];
    StringSpec s;
    s.lf0 = lf0;
    s.B = 6711;   // 0.0004
    s.partials = 14;
    s.doubled = 1;
    s.detune = k.detune;
    s.beta = Q16(0.04) + mul16(c[3], Q16(0.42));
    s.tilt = lerp16(Q16(1.75), Q16(0.5), nail) + k.tilt_add * 3;   // SOFT: the quill's softer side
    s.sigma1 = Q16(1.0 / 4.0) + mul16(mute, Q16(4.0));
    s.sigma_exp = Q16(1.1);
    s.rubber = mute * 520;
    s.rubber_x = Q16(0.022);
    s.mu = 0;
    s.mass_x = 0;
    s.damper = damper_eased(Q16(24.0), k.pedal);
    s.second = k.second;
    s.scatter = mul16(k.age, 6 * cent);
    const i32 total = string_first(v, s);
    const i32 lscale = level_scale(total, Q31(0.95));
    v.bent = 0;
    {
        const i32 slack = one - lunit(buzz);
        v.bthr = mul(mul(mul(slack, slack), Q31(0.8)), 1 << 25);
    }
    v.blevel = mul(lunit(buzz), Q31(0.25));
    v.bf = svf_f(L(1100.0) + mul16(c[0], Q16(1.0)));
    v.bq = Q31(0.45);
    v.k[1] = onepole_k(L(250.0));
    strike_string(v, lerp16(L(0.0035), L(0.00025), nail) + k.ltc_add * 2, lscale);   // the quill's soft side stays on the string longer
    damper_at(v, M::PLUCK, c[1]);
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ BOW
// A rosined thread drawn round a string (the bowed piano): the string's
// partials driven by the thread's stick and slip (a sawtooth at the string's
// own pitch) and its rosin's grit, swelling over the draw (DRAW), the pitch
// bent along it (BEND), then left to ring (DECAY).
static __attribute__((noinline)) void setup_bow(Voice& v, const i32* c, const Common& k) {
    const i32 lf0 = shape::along(shape::pitch[M::BOW], c[0]) + k.lt;
    const i32 press = c[2];
    const i32 x = Q16(0.04) + mul16(c[5], Q16(0.28));
    for (u32 n = 1; n <= 12; ++n) {
        const i32 lf = lf0 + tables::lnum[n];
        const i32 a = mul(sinpi(n * static_cast<u32>(x)), Q31(0.000006));
        const i32 sg = exp16(log16(Q16(1.0 / 1.4)) + mul16(Q16(0.3), lf - lf0));
        const i32 sd = sg + rate_of(shape::along(shape::damper[M::BOW], c[1]));
        if (n <= 2) {
            if (add_mode(v, lf - k.detune, a, sg, sd) < 0) break;
            if (add_mode(v, lf + k.detune, mul(a, k.second), sg, sd) < 0) break;
        } else if (add_mode(v, lf, a, sg, sd) < 0) {
            break;
        }
    }
    v.bent = v.n;
    v.k[0] = lf0;
    v.k[3] = k.detune;
    v.k[1] = (c[4] - 32768) * 2;   // BEND, -1..1 octave, Q16
    v.bdraw = samples(shape::along(shape::draw, c[3]));
    v.binc = increment(lf0);
    v.bphase = 0;
    v.benv = 0;
    v.bpress = press;
    v.blp = onepole_k(lf0 + Q16(0.2) + mul16(press, Q16(5.0)));
    v.k[2] = mul(mul(lunit(press), lunit(press)), Q31(0.9)) + Q31(0.02);   // the rosin's grit
    v.waiting = 1;
    v.damp_at = v.bdraw;
    v.k[6] = lf0;   // the pedal's strings, tuned in the fifth block
    v.k[7] = k.pedal;
}

// ------------------------------------------------------------ dispatch
inline void setup_model(Voice& v, u32 m, const i32* c, i32 tune) {
    const Common k = common(v, c, tune);
    // the soundboard's soft top, lower with SOFT: the una corda's rounder note
    v.topk = onepole_k((m == M::CHAIN ? L(10000.0) : L(5200.0)) - mul16(k.soft, Q16(1.8)));
    switch (m) {
    case M::THUD: case M::TOM: setup_thud(v, m, c, k); break;
    case M::RATTLE: setup_rattle(v, c, k); break;
    case M::LID: setup_lid(v, c, k); break;
    case M::TACK: setup_tack(v, c, k); break;
    case M::BELL: case M::BOLT: setup_screw(v, m, c, k); break;
    case M::BRUSH: setup_brush(v, c, k); break;
    case M::PLANK: setup_cluster(v, c, k); break;
    case M::CHAIN: setup_beads(v, c, k); break;
    case M::PLUCK: setup_pluck(v, c, k); break;
    default: setup_bow(v, c, k); break;
    }
}

// ------------------------------------------------------------ the stages
// Each per-sample stage is its own function, so tools/cpu.py can count its
// loop in the compiled code and add up a block from what runs.
// The buzz: noise wherever the string's swing s passes the threshold, in its
// band; and RATTLE's crack, the paper slapped by the blow (w, falling fast).
static __attribute__((noinline)) void stage_buzz(Voice& v, const i32* s, i32* out) {
    const i32 thr = v.bthr, level = v.blevel, f = v.bf, q = v.bq, hk = v.k[1], wk = v.wk;
    i32 w = v.wlevel;
    for (unsigned i = 0; i < frames; ++i) {
        i32 e = absolute(s[i]) - thr;
        e = e > 0 ? e : 0;
        const i32 z = mul(noise(v.seed), e + w);
        w = mul(w, wk);
        const i32 y = v.bhp.high(v.bsv.band(z, f, q), hk);
        out[i] = add(out[i], mul(y, level) * 16);
    }
    v.wlevel = w;
}
// RATTLE: the string's note under the paper (RING).
static __attribute__((noinline)) void stage_ring(Voice& v, const i32* s, i32* out) {
    const i32 ring = v.k[0];
    for (unsigned i = 0; i < frames; ++i) out[i] = mul(s[i], ring);
}
// LID: each slap's burst of noise in the board's band, then the case's tail.
static __attribute__((noinline)) void stage_slaps(Voice& v, const u32* hit_at, const i32* hit_amp, u32 hits, i32* out) {
    // each burst swells over a fraction of a millisecond (w less a faster w2),
    // as the board's face meets the slip, then dies
    i32 w = v.wlevel, w2 = v.k[4], tail = v.nlevel;
    const i32 fast = v.k[3];
    u32 h = 0;
    for (unsigned i = 0; i < frames; ++i) {
        if (h < hits && hit_at[h] == i) {
            w = w2 = hit_amp[h++];
            if (!v.left && h == hits) tail = mul(w, v.k[1]);
        }
        const i32 z = mul(noise(v.seed), add(w - w2, tail)) / 4;
        const i32 y = v.whp.high(v.wsv.band(z, v.wf, v.wq), v.whpk);
        out[i] = v.top.low(add(out[i], y), v.k[2]);
        w = mul(w, v.wk);
        w2 = mul(w2, fast);
        tail = mul(tail, v.k[0]);
    }
    v.wlevel = w;
    v.k[4] = w2;
    v.nlevel = tail;
}
// BRUSH, PLANK: the brush's (or plank's) noise, rising over its stroke,
// then dying; a share of it drives the strings (x).
static __attribute__((noinline)) void stage_sweep(Voice& v, i32* x, i32* hiss) {
    i32 level = v.nlevel;
    const i32 sparse = v.model == M::BRUSH ? v.k[0] : 0, drive = v.k[2];
    const i32 rise_step = v.nrise > 0 ? (v.ntarget - level) / v.nrise : 0;
    for (unsigned i = 0; i < frames; ++i) {
        if (v.nrise > 0) { level += rise_step; --v.nrise; } else level = mul(level, v.nk);
        i32 n = noise(v.seed);
        if (sparse && absolute(n) < sparse) n = 0;
        const i32 z = v.nhp.high(mul(n, level) / 8, v.nhpk);
        x[i] = mul(z, drive);
        hiss[i] = z;
    }
    v.nlevel = level;
}
// ... and its hiss into the sound, high-passed twice.
static __attribute__((noinline)) void stage_hiss(Voice& v, const i32* hiss, i32* out) {
    const i32 share = v.k[1];
    for (unsigned i = 0; i < frames; ++i) {
        const i32 y = add(out[i], mul(hiss[i], share) / 4);
        out[i] = v.hp2.high(v.hp1.high(y, v.hpk), v.hpk);
    }
}
// CHAIN: a bead lands at random, chance in 2^32 a sample; its knock rings the
// glass (x) and hisses.
static __attribute__((noinline)) void stage_beads(Voice& v, u32 chance, i32* x, i32* out) {
    i32 w = v.wlevel, target = v.wk;
    for (unsigned i = 0; i < frames; ++i) {
        const u32 r = lcg(v.seed);
        x[i] = 0;
        if (r < chance) {
            const i32 a = static_cast<i32>((r >> 8) & 0xffff) * 4096 + Q31(0.2);
            x[i] = mul(a, Q31(0.25));
            target = a;
        }
        w += (target - w) / 4;   // a knock's hiss rises over a few samples: glass, not a spark
        const i32 z = v.nhp.high(mul(noise(v.seed), w), v.nhpk);
        out[i] = mul(z, v.k[3]);
        target = mul(target, v.k[2]);
    }
    v.wlevel = w;
    v.wk = target;
}
// BOW: the thread's stick and slip (a smoothed sawtooth at the string's pitch)
// and its rosin's grit, swelling from e0 by step a sample.
static __attribute__((noinline)) void stage_bow(Voice& v, i32 e, i32 step, i32* x) {
    u32 ph = v.bphase;
    const i32 grit = v.k[2], k = v.blp;
    for (unsigned i = 0; i < frames; ++i) {
        e += step;
        ph += v.binc;
        const i32 saw = static_cast<i32>(ph) / 2;
        const i32 drive = v.nhp.low(v.nlp.low(saw, k), k) + mul(noise(v.seed) / 2, grit);
        x[i] = mul(drive, e);
    }
    v.bphase = ph;
}
// AGE: the grooved felt's scratch at the blow.
static __attribute__((noinline)) void stage_grain(Voice& v, i32* out) {
    i32 w = v.grain;
    const i32 k = Q31(0.9943);   // a 4 ms fall
    for (unsigned i = 0; i < frames; ++i) {
        out[i] = add(out[i], mul(noise(v.seed), w) / 32);
        w = mul(w, k);
    }
    v.grain = v.t >= 5 * frames ? 0 : w;
}
// The soundboard's soft top; CHAIN's high-pass and a gentler top, twice.
static __attribute__((noinline)) void stage_top(Voice& v, i32* out) {
    for (unsigned i = 0; i < frames; ++i) out[i] = v.top.low(out[i], v.topk);
}
// RATTLE: the same top twice, so the paper's buzz is warm, not fizzing.
static __attribute__((noinline)) void stage_top2(Voice& v, i32* out) {
    for (unsigned i = 0; i < frames; ++i) out[i] = v.hp2.low(v.top.low(out[i], v.topk), v.topk);
}
static __attribute__((noinline)) void stage_highpass(Voice& v, i32* out) {
    for (unsigned i = 0; i < frames; ++i) out[i] = v.hp2.low(v.top.low(v.hp1.high(out[i], v.hpk), v.topk), v.topk);
}

// ------------------------------------------------------------ a block
inline void block_model(Voice& v, i32* out) {
    i32 x[frames];
    for (unsigned i = 0; i < frames; ++i) out[i] = 0;
    const u32 m = v.model;
    if (v.spec_next && v.t) string_rest(v);
    if (v.late && v.t >= 2 * frames) {
        if (v.late == 1) screw_modes(v); else cluster_bolts(v);
        v.late = 0;
    }
    switch (m) {
    case M::RATTLE: case M::BELL: case M::BOLT: case M::PLUCK: {
        i32 s[frames];
        for (unsigned i = 0; i < frames; ++i) s[i] = 0;
        run_modes(v, nullptr, s);
        if (m == M::RATTLE) {
            stage_ring(v, s, out);
        } else {
            for (unsigned i = 0; i < frames; ++i) out[i] = s[i];
        }
        if (v.blevel) stage_buzz(v, s, out);
        v.waiting = 0;
        break;
    }
    case M::LID: {
        // the slaps due in this block: a knock into the board's modes, a burst of noise
        u32 hit_at[6];
        i32 hit_amp[6];
        u32 hits = 0;
        for (unsigned i = 0; i < frames; ++i) x[i] = 0;
        while (v.left && v.at < v.t + frames && hits < 6) {
            hit_at[hits] = v.at - v.t;
            hit_amp[hits++] = v.samp;
            x[v.at - v.t] = add(x[v.at - v.t], mul(v.samp, 1 << 28));
            --v.left;
            const u32 gap = v.sgap >> 8;
            v.at += gap > 1 ? gap : 1;
            v.sgap = static_cast<u32>(mul(static_cast<i32>(v.sgap >> 1), v.sgk)) * 2u;
            v.samp = mul(v.samp, v.sk);
        }
        if (!v.left) v.waiting = 0;
        run_modes(v, hits ? x : nullptr, out);
        stage_slaps(v, hit_at, hit_amp, hits, out);
        break;
    }
    case M::BRUSH: case M::PLANK: {
        i32 hiss[frames];
        stage_sweep(v, x, hiss);
        if (v.nrise <= 0 && v.nlevel < 4096) v.waiting = 0;
        if (m == M::PLANK) {
            run_modes(v, nullptr, out);
            if (v.k[3] > 0) {   // the bloom's growth
                --v.k[3];
                const i32 grow = v.k[4];
                for (u32 j = 0; j < v.n; ++j) {
                    v.mode[j].l += mul(v.mode[j].l, grow);
                    v.mode[j].b += mul(v.mode[j].b, grow);
                }
            }
        } else {
            run_modes(v, x, out);
        }
        stage_hiss(v, hiss, out);
        break;
    }
    case M::CHAIN: {
        // the shake's envelope: from front-loaded (falling) to swelling (rising)
        const u32 shake = static_cast<u32>(v.nrise);
        u32 chance = 0;
        if (v.t < shake) {
            const i32 pos = static_cast<i32>(((v.t >> 4) << 12) / ((shake >> 4) + 1));   // 0..4096 through the shake
            const i32 fall = 4096 - pos, rise = pos;
            const i32 env = clamp(fall + (((rise - fall) * (v.k[1] >> 4)) >> 12), 0, 4095);
            chance = static_cast<u32>(mul(v.k[0], env * 524288));
        } else {
            v.waiting = 0;
        }
        stage_beads(v, chance, x, out);
        run_modes(v, chance ? x : nullptr, out);
        stage_highpass(v, out);
        break;
    }
    case M::BOW: {
        // the draw: the thread's pull swelling as (t / draw)^2, then let go
        const u32 d = v.bdraw;
        i32 env_end;
        if (v.t < d) {
            const i32 pos = static_cast<i32>(((v.t + frames) >> 4 << 12) / ((d >> 4) + 1));   // Q12
            const i32 p = clamp(pos, 0, 4096);
            env_end = p >= 4096 ? one : p * p * 128;   // Q31
            // the bend along the draw
            if (v.k[1]) {
                const i32 lf0 = v.k[0] + lerp16(0, v.k[1], p * 16);
                v.binc = increment(lf0);
                for (u32 j = 0; j < v.bent; ++j) {
                    const u32 n = j < 4 ? 1 + j / 2 : j - 1;
                    const i32 dt = j < 4 ? (j & 1 ? v.k[3] : -v.k[3]) : 0;
                    v.mode[j].f = sin_inc(increment(clamp(lf0 + tables::lnum[n] + dt, 0, L(7000.0)))) * 2;
                }
            }
        } else {
            env_end = 0;
            v.waiting = 0;
        }
        stage_bow(v, v.benv, (env_end - v.benv) / static_cast<i32>(frames), x);
        v.benv = env_end;
        run_modes(v, x, out);
        break;
    }
    default: {   // THUD, TOM, TACK
        run_modes(v, nullptr, out);
        if (v.bend) {
            v.bend = mul(v.bend, v.bend_k);
            if (v.bend < 4096) v.bend = 0;
            bend_modes(v);
        }
        v.waiting = 0;
        break;
    }
    }
    if (v.grain && v.t >= frames) stage_grain(v, out);   // from the second block, for three
    // the pedal's strings answer what the voice sounds (set up in the fifth
    // block, to spread the note's start)
    if (v.t == 4 * frames && v.k[7] > 0) pedal_strings(v, v.k[6], v.k[7]);
    run_pedal(v, out, out);
    if (m == M::RATTLE) stage_top2(v, out);
    else if (m != M::BRUSH && m != M::CHAIN && m != M::LID) stage_top(v, out);
    // the damper lands
    if (!v.damped && v.damp_at && v.t + frames >= v.damp_at) land_damper(v, v.n);
}
}
