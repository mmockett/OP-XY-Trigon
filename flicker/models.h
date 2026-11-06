#pragma once
// FLICKER's twelve drawings. Each model sets itself up from the key's eight
// controls (Q16 units, 0..65536) and its TUNE (log2, Q16) when a note starts,
// and then draws a block of its shape at a time, in track edges (E is a shape
// that just reaches the track's edges; a default hit peaks near 0.75 E).
// v.fm is the film's speed (FLUTTER), which every pitch follows; each model
// leaves v.run, its height now, for the dirt on the film to play under.
#include "voice.h"
#include "ranges.h"

namespace flicker {
struct M { enum : u32 { SWEEP, SCRATCH, RING, STROKES, DOT, CARD, HATCH, ORNAMENT, STIPPLE, BOOP, WAVE, CLAW }; };
const i32 semitone = 5461;   // a semitone in log2, Q16

// ------------------------------------------------------------ shared pieces
// SHAPE: sine (0) to triangle (a half) to a drawn square (1): how far the
// sine bends to a triangle (Q31) and how tall the triangle is drawn before
// it is cut flat (Q8: 256 as tall as the track, up to 8 times). A steeper
// square is held to edges of at least two samples (period: samples, log2 Q16).
inline void shape_of(i32 shape, i32 lperiod, i32& bend, i32& height) {
    bend = shape >= 32768 ? one : clamp(shape, 0, 32767) * 65536;
    i32 h = shape > 32768 ? 256 + mul16(1792, (shape - 32768) * 2) : 256;
    // edges of at least 2 samples: height at most period / 4
    const i32 most = static_cast<i32>(pow2(lperiod - 2 * octave + 8 * octave));
    if (h > most) h = most > 256 ? most : 256;
    bend = shape >= 32768 ? one : bend;
    height = clamp(h, 256, 2047);
}
// log2 of a period in samples for a frequency in log2 Hz.
inline i32 lperiod(i32 lhz) { return lsr - lhz; }
// A raised-cosine bump, Q31 (0 .. one .. 0 over one turn of the phase).
inline i32 bump(u32 phase) { return 0x3fffffff - (sine(phase + 0x40000000u) >> 1); }
// A filter's frequency coefficient at the film's speed (FLUTTER moves every
// pitch on the film, a ring's too).
inline i32 film(i32 f, i32 fm) {
    if (fm == (1 << 30)) return f;
    const i32 r = multiply(f, fm);
    return r > (one >> 1) ? one : r * 2;
}
// A crack: a burst of scratched noise that dies in a few milliseconds (a
// click's grit, a snare's snap, a conga's slap). Its level and its fall a
// sample are v.s[14] and v.s[15].
inline void crack(Voice& v, i32* x) {
    i32 a = v.s[14];
    if (!a) return;
    const i32 k = v.s[15];
    u32 seed = v.seed;
    for (unsigned i = 0; i < frames; ++i) {
        x[i] += multiply(noise(seed) >> 3, a);
        a = multiply(a, k);
    }
    v.s[14] = a < (one >> 12) ? 0 : a;   // gone 72 dB down
    v.seed = seed;
}
// SWEEP's and BOOP's drawing: the shape at a gliding pitch (inc by di a
// sample), under its envelope (e by de), a quarter of it into x.
template <bool Tall> inline void glide_draw(i32* x, u32& ph, u32 inc, i32 di, i32 bend, i32 height, i32 e, i32 de) {
    u32 p = ph;
    for (unsigned i = 0; i < frames; ++i) {
        p += inc;
        inc = static_cast<u32>(static_cast<i32>(inc) + di);
        x[i] = div2k(multiply(drawn_as<Tall>(p, bend, height), e), 2);
        e += de;
    }
    ph = p;
}
// A pitch glide's increment through a block: from inc0 toward inc1.
inline i32 glide_step(u32 from, u32 to) { return (static_cast<i32>(to >> 1) - static_cast<i32>(from >> 1)) / 32; }

// ------------------------------------------------------------ setup
// Each model's setup and each drawing's block is a function of its own,
// never inlined, so the Blackfin code of each can be counted apart
// (tools/static_cpu.py).
#define FLICKER_APART static __attribute__((noinline))
FLICKER_APART void setup_sweep(Voice& v, const i32* c, i32 tune) {
    // Sine waves inked further and further apart: a pitch that falls BEND
    // semitones to PITCH as the drawing tapers over DECAY; SHAPE draws
    // harder edges; CLICK an inked tick at the start.
    const i32 lf = lmix(range::sweep_pitch[0], range::sweep_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    v.k[1] = mul16(c[2], range::sweep_bend * semitone);         // BEND, log2
    v.k[2] = block_decay(lmix(L(0.022), L(0.09), c[1]));        // the glide's fall a block
    v.k[3] = one;                                               // the glide now
    shape_of(c[3], lperiod(lf + v.k[1] / 2), v.k[4], v.k[5]);
    // the click: a dot 1 to 0.3 ms long, as dark as CLICK, and the
    // scratch of the pen that drew it
    v.k[6] = mul16(c[4], 98304);   // its height, Q16 of the track's edge: up to one and a half
    v.inc[1] = static_cast<u32>(pow2(32 * octave - lsr - lmix(L(0.0012), L(0.0004), c[4])));
    v.s[14] = multiply(clamp(c[4], 0, 65535) * 32768, Q31(0.95));
    v.s[15] = decay(lmix(L(0.0015), L(0.008), c[4]));
    v.ph[1] = 0;
    v.inc[0] = increment(lf + v.k[1]);
    v.ph[0] = 0;
    v.env[0].set(Q31(0.75), one, block_decay(lmix(range::sweep_decay[0], range::sweep_decay[1], c[1])));
}
FLICKER_APART void setup_scratch(Voice& v, const i32* c, i32 tune) {
    // Two drawn tones bent by SNAP, and scratches through the emulsion:
    // GRAIN from fine to coarse; SCRATCH how many against the tones.
    const i32 lf = lmix(range::scratch_pitch[0], range::scratch_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    v.k[1] = mul16(c[3], range::scratch_snap * semitone);        // SNAP, log2
    v.k[2] = block_decay(L(0.014));
    v.k[3] = one;
    v.inc[0] = increment(lf + v.k[1]);
    v.inc[1] = increment(lf + L(1.47) + v.k[1]);
    const i32 sc = c[2];
    v.env[0].set(Q31(0.62) - multiply(Q31(0.3), sc * 32768), one, block_decay(lmix(L(0.04), L(0.35), c[1])));
    v.env[1].set(multiply(Q31(0.25) + multiply(Q31(0.75), sc * 32768), Q31(0.8)), one, block_decay(lmix(range::scratch_decay[0], range::scratch_decay[1], c[1])));
    // the scratches: held for 1 to 9 samples, then a band 6.5 kHz to 2.2 kHz
    v.k[4] = 1 + (c[4] * 8 + 32768) / 65536;
    const i32 lb = lmix(L(6500), L(2000), c[4]);
    v.k[5] = svf_f(increment(lb));
    v.k[6] = Q31(1.1);
    v.k[7] = 0;   // the held value
    v.k[8] = 0;   // samples held
    v.s[14] = multiply(c[3] * 32768, Q31(0.5)) + multiply(sq16(c[3]) * 32768, Q31(0.4));   // SNAP's crack
    v.s[15] = decay(L(0.003));
}
FLICKER_APART void setup_ring(Voice& v, const i32* c, i32 tune) {
    // Drawn sines bending a little to PITCH, a second ring over them, and
    // SLAP an inked edge at the attack: toms, congas.
    const i32 lf = lmix(range::ring_pitch[0], range::ring_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    v.k[1] = mul16(c[2], range::ring_bend * semitone);
    v.k[2] = block_decay(L(0.05));
    v.k[3] = one;
    shape_of(c[3], lperiod(lf + v.k[1]), v.k[4], v.k[5]);
    v.inc[0] = increment(lf + v.k[1]);
    v.inc[1] = increment(lf + L(1.52) + v.k[1]);
    const i32 lt = lmix(range::ring_decay[0], range::ring_decay[1], c[1]);
    v.env[0].set(Q31(0.7), one, block_decay(lt));
    v.env[1].set(Q31(0.22), one, block_decay(lt - octave));
    // the slap: a burst of 4 ms through a band near four times the note
    v.k[6] = multiply(c[4] * 32768, Q31(0.9));
    v.k[7] = decay(L(0.006));
    v.s[14] = multiply(c[4] * 32768, Q31(0.9));                  // the slap's crack
    v.s[15] = decay(lmix(L(0.002), L(0.009), c[4]));
    v.k[8] = svf_f(increment(clamp(lf + 2 * octave, L(500), L(6000))));
    v.k[9] = Q31(0.7);
}
FLICKER_APART void setup_strokes(Voice& v, const i32* c, i32 tune) {
    // STROKES quick brush strokes SPREAD apart (SCATTER unevens them),
    // then the smear they leave, DECAY long: a clap through a band at PITCH.
    const i32 lf = lmix(range::strokes_pitch[0], range::strokes_pitch[1], c[0]) + tune;
    v.k[0] = svf_f(increment(lf));
    v.k[1] = Q31(0.75);
    const i32 n = 65536 + c[2] * 5;                             // 1..6 strokes, Q16
    v.k[2] = n >> 16;                                           // whole strokes after the first
    v.k[3] = (n & 0xffff) * 32768;                              // the last one's share, Q31
    v.k[4] = samples(lmix(range::strokes_spread[0], range::strokes_spread[1], c[3]));            // the gap
    v.k[5] = c[4];                                              // scatter, Q16
    v.k[6] = 0;                                                 // the next stroke, samples
    v.k[7] = 0;                                                 // strokes struck
    v.k[8] = 0;                                                 // the burst now
    v.k[9] = decay(L(0.0045));
    const i32 tail_at = v.k[4] * (v.k[2] + 1);
    v.env[0].set(Q31(0.5), clamp(static_cast<i32>(pow2(L(64) + 31 * octave - log2q(static_cast<u32>(tail_at + 64)))), 1, one),
                 block_decay(lmix(range::strokes_decay[0], range::strokes_decay[1], c[1])));
    v.waiting = 1;
}
FLICKER_APART void setup_dot(Voice& v, const i32* c, i32 tune) {
    // One inked dot (SIZE across) and ECHO dots after it, GAP apart: each
    // rings the light at PITCH for DECAY, and the wood of the sound under it
    // at a lower ring (the larger the dot, the more of it). McLaren's dots:
    // a rim, a clave, a woodblock.
    const i32 lf = lmix(range::dot_pitch[0], range::dot_pitch[1], c[0]) + tune;
    const i32 lt = lmix(range::dot_decay[0], range::dot_decay[1], c[1]);
    v.k[0] = svf_f(increment(clamp(lf, L(60), L(7000))));
    v.k[1] = svf_q(lf, lt);
    const i32 lw = clamp(lf - L(2.35), L(60), L(7000));
    v.k[11] = svf_f(increment(lw));
    v.k[12] = svf_q(lw, lt - L(1.6));
    v.inc[0] = static_cast<u32>(pow2(32 * octave - lsr - lmix(range::dot_size[0], range::dot_size[1], c[2])));   // the dot's width
    v.ph[0] = 0xf0000000u;
    const i32 n = c[3] * 6;                                     // echoes, Q16
    v.k[3] = n >> 16;                                           // whole echoes
    v.k[4] = (n & 0xffff) * 32768;                              // the share of one more
    v.k[5] = samples(lmix(range::dot_gap[0], range::dot_gap[1], c[4]));            // the gap
    v.k[6] = 0;                                                 // the next dot
    v.k[7] = 0;                                                 // dots drawn
    v.k[8] = one;                                               // the next echo's ink
    v.k[9] = 0;                                                 // the dot being drawn's ink
    // the rings' levels: a narrow dot rings the light's pitch high; a wide
    // one the wood under it
    v.k[10] = Q31(0.95);
    v.k[13] = Q31(0.1) + multiply(c[2] * 32768, Q31(0.5));
    v.env[0].set(one, one, block_decay(lt));                    // only for the screen's run
    v.waiting = 1;
}
FLICKER_APART void setup_card(Voice& v, const i32* c, i32 tune) {
    // Striped cards: two pulse tones RATIO apart, WIDTH the stripes,
    // BEND a fall into the note: a cowbell, an agogo, beeps.
    const i32 lf = lmix(range::card_pitch[0], range::card_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    v.k[1] = lmix(range::card_ratio[0], range::card_ratio[1], c[2]);                        // RATIO
    v.k[2] = mul16(c[4], range::card_bend * semitone);
    v.k[3] = one;
    v.k[4] = block_decay(L(0.035));
    const i32 duty = lmix(Q16(0.05) * 0 + 3277, 32768, c[3]);    // 5% .. 50%
    v.k[5] = (65536 - 2 * duty) * 16384;                         // the cut, Q31 of the triangle's half
    v.k[6] = clamp(static_cast<i32>(pow2(lperiod(lf + v.k[1] + v.k[2]) - 2 * octave + 8 * octave)), 256, 2047);
    v.inc[0] = increment(lf + v.k[2]);
    v.inc[1] = increment(lf + v.k[1] + v.k[2]);
    v.k[7] = svf_f(increment(clamp(lf + L(2.1), L(200), L(6800))));
    v.k[8] = Q31(1.2);
    const i32 lt = lmix(range::card_decay[0], range::card_decay[1], c[1]);
    v.env[0].set(Q31(0.5), one, block_decay(lt));
    v.env[1].set(Q31(0.3), one, block_decay(lt - octave - octave / 2));   // the first strike, quicker
}
FLICKER_APART void setup_hatch(Voice& v, const i32* c, i32 tune) {
    // Ruled lines across the track: six near-periodic hatchings SPREAD in
    // spacing, and scratched noise; METAL from scratches to ringing lines,
    // EDGE their sharpness (a high-pass), DECAY closed to open.
    const i32 lf = lmix(range::hatch_pitch[0], range::hatch_pitch[1], c[0]) + tune;
    static const i32 at[6] = {0, Q16(0.43), Q16(0.71), Q16(0.97), Q16(1.21), Q16(1.47)};
    const i32 spread = lmix(Q16(0.35), Q16(1.25), c[4]);
    for (u32 i = 0; i < 6; ++i) v.inc[i] = increment(lf + mul16(at[i], spread));
    for (u32 i = 0; i < 6; ++i) v.ph[i] = static_cast<u32>(i) * 0x2b3c4d5eu;
    v.k[0] = c[2] * 32768;                                      // METAL, Q31
    const i32 le = lmix(range::hatch_edge[0], range::hatch_edge[1], c[3]);
    v.k[1] = svf_f(increment(le + mul16(c[0] - 32768, Q16(0.5))));
    // sharper lines ring at the cut: the filter's damping falls as EDGE rises
    v.k[2] = Q31(1.4) - multiply(c[3] * 32768, Q31(0.6));
    v.k[3] = Q31(0.35) + multiply(c[3] * 32768, Q31(0.4));     // the ring at the cut
    v.env[0].set(Q31(0.85), Q31(0.6), block_decay(lmix(range::hatch_decay[0], range::hatch_decay[1], c[1])));
}
FLICKER_APART void setup_ornament(Voice& v, const i32* c, i32 tune) {
    // Interlaced painted rings: pairs of ruled lines crossing each other
    // (light where exactly one passes: a ring modulation), RINGS of them and
    // the scratches between, high in the track's sizzle; BELL a struck ping
    // in their middle; SWELL a wash that blooms: a ride, a crash.
    const i32 lf = lmix(range::ornament_pitch[0], range::ornament_pitch[1], c[0]) + tune;
    static const i32 at[6] = {0, L(1.483), L(1.749), L(2.172), L(2.631), L(3.212)};
    for (u32 i = 0; i < 6; ++i) v.inc[i] = increment(lf + at[i] + octave);
    for (u32 i = 0; i < 6; ++i) v.ph[i] = static_cast<u32>(i) * 0x3c5a7e11u;
    // the bell: three sines, its ping
    const i32 lb = lf + L(1.6);
    v.inc[6] = increment(lb);
    v.inc[7] = increment(lb + L(2.41));
    v.inc[8] = increment(clamp(lb + L(3.93), 0, L(15000)));
    v.ph[6] = v.ph[7] = v.ph[8] = 0;
    const i32 r = c[2];
    v.k[0] = one >> 3;                                          // the first pair
    v.k[1] = multiply(clamp(r * 2, 0, 65535) * 32768, one >> 3);            // the second
    v.k[2] = multiply(clamp(r * 2 - 65536, 0, 65535) * 32768, one >> 3);    // the third
    v.k[3] = Q31(0.15) + multiply(r * 32768, Q31(0.6));         // the scratches between them
    v.k[4] = multiply(c[3] * 32768, Q31(0.9));                  // BELL
    v.k[5] = svf_f(increment(lmix(L(5200), L(6900), c[0])));
    v.k[6] = Q31(1.1);
    const i32 lt = lmix(range::ornament_decay[0], range::ornament_decay[1], c[1]);
    const i32 rise = c[4] < 512 ? one : static_cast<i32>(pow2(L(64) + 31 * octave - lsr - lmix(range::ornament_swell[0], range::ornament_swell[1], c[4])));
    v.env[0].set(Q31(0.6), clamp(rise, 1, one), block_decay(lt));
    v.env[1].set(Q31(0.5), one, block_decay(lt - L(3.0)));      // the bell, a third as long
}
FLICKER_APART void setup_stipple(Voice& v, const i32* c, i32 tune) {
    // Stippled dots, DENSITY of them a second, ringing a band at PITCH:
    // a shaker; JINGLE gathers them into clusters that ring small
    // cymbals: a tambourine. SWELL eases the shake in.
    const i32 lf = lmix(range::stipple_pitch[0], range::stipple_pitch[1], c[0]) + tune;
    v.k[0] = svf_f(increment(clamp(lf, L(800), L(7000))));
    v.k[1] = Q31(0.55);
    v.k[2] = static_cast<i32>(pow2(lmix(range::stipple_density[0], range::stipple_density[1], c[2]) - lsr + 31 * octave));   // a dot's chance a sample, Q31
    v.k[3] = decay(L(0.0007));
    v.k[4] = 0;                                                 // the dot now
    v.k[5] = multiply(c[4] * 32768, Q31(0.95));                 // JINGLE
    v.k[6] = svf_f(increment(clamp(lf - L(1.4), L(2000), L(6800))));
    v.k[7] = svf_q(lf - L(1.4), L(0.08));
    v.k[8] = svf_f(increment(clamp(lf - L(1.4) + L(1.37), L(2000), L(6900))));
    v.k[9] = svf_q(lf - L(1.4) + L(1.37), L(0.07));
    v.inc[0] = increment(L(11));                                // the clusters' shake
    v.ph[0] = 0;
    // SWELL: the shake eases in over up to 250 ms, as SWELL's square
    const i32 sw = sq16(c[3]);
    const i32 rise = sw < 64 ? one : static_cast<i32>(pow2(L(64) + 31 * octave - lsr - (log2q(static_cast<u32>(sw)) - 16 * octave + range::stipple_swell[1])));
    v.env[0].set(Q31(0.9), clamp(rise, 1, one), block_decay(lmix(range::stipple_decay[0], range::stipple_decay[1], c[1])));
}
FLICKER_APART void setup_boop(Voice& v, const i32* c, i32 tune) {
    // A tuned drawn tone that slides into its note from SLIDE semitones
    // away (below at 0, above at 127), SHAPE its edges, VIBRATO the
    // drawing's waver: McLaren's boops, melodic hits and zaps.
    const i32 lf = lmix(range::boop_pitch[0], range::boop_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    v.k[1] = mul16(c[2] - 32768, 2 * range::boop_slide * semitone);                // SLIDE, -24..+24 st
    v.k[2] = block_decay(L(0.045));
    v.k[3] = one;
    shape_of(c[3], lperiod(lf + (v.k[1] > 0 ? v.k[1] : 0)), v.k[4], v.k[5]);
    v.k[6] = mul16(c[4], 2 * semitone);                         // VIBRATO, up to two semitones
    v.k[7] = 0;                                                 // its depth now
    v.inc[1] = increment(L(6.0));
    v.ph[1] = 0;
    v.inc[0] = increment(lf + v.k[1]);
    v.ph[0] = 0;
    v.env[0].set(Q31(0.72), Q31(0.4), block_decay(lmix(range::boop_decay[0], range::boop_decay[1], c[1])));
}
FLICKER_APART void setup_wave(Voice& v, const i32* c, i32 tune) {
    // Oram's drawn bass: a tuned wave of SHAPE, its edges rounding over the
    // note by SWEEP, and SUB a drawn octave under it.
    const i32 lf = lmix(range::wave_pitch[0], range::wave_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    shape_of(c[2], lperiod(lf), v.k[1], v.k[2]);
    v.inc[0] = increment(lf);
    v.inc[1] = increment(lf - octave);
    v.ph[0] = v.ph[1] = 0;
    // the sweep: a resonant low-pass that starts open and closes, settling
    // (log2 over the note) at 1.6 kHz at SWEEP 0, falling quickly as SWEEP
    // rises (between 1 - (1 - SWEEP)^2 and ^3), to the note itself at 127
    const i32 rest = 65536 - c[3];
    const i32 r2 = sq16(rest);
    v.k[3] = mul16(clamp(L(1600) - lf, 0, 8 * octave), (r2 + mul16(r2, rest)) / 2);   // between (1 - SWEEP)^2 and ^3 of the way
    v.k[4] = one;                                               // its fall now
    v.k[5] = block_decay(L(0.14));
    v.k[6] = c[4] * 32768;                                      // SUB, Q31
    v.k[7] = Q31(0.6);
    const i32 lt = lmix(range::wave_decay[0], range::wave_decay[1], c[1]);
    v.env[0].set(Q31(0.62), Q31(0.3), block_decay(lt));
}
FLICKER_APART void setup_claw(Voice& v, const i32* c, i32 tune) {
    // The projector's claw clattering through frames, RATE a second: each
    // pull a tick (GRIT: clean to gritty) ringing at PITCH; SWELL from a
    // roll that dies away (0) through an even one to one that rises and
    // quickens (a riser); DECAY its length.
    const i32 lf = lmix(range::claw_pitch[0], range::claw_pitch[1], c[0]) + tune;
    v.k[0] = lf;
    v.k[1] = svf_f(increment(clamp(lf, L(100), L(7000))));
    v.k[2] = svf_q(lf, L(0.025));
    const i32 lrate = lmix(range::claw_rate[0], range::claw_rate[1], c[2]);
    const i32 len = lmix(range::claw_decay[0], range::claw_decay[1], c[1]);
    v.k[4] = samples(len);                                      // the roll's length
    const i32 lticks = clamp(len + lrate, octave, 9 * octave);  // log2 of the ticks in it
    const i32 sw = clamp(c[3] * 2 - 65536, -65536, 65536);      // -1..1, Q16
    const i32 a = sw < 0 ? -sw : sw;
    const i32 per = a ? static_cast<i32>(pow2(log2q(static_cast<u32>(a)) - lticks)) : 0;   // a / ticks, Q16
    v.k[3] = lsr - lrate;                                       // log2 of the gap, samples
    v.k[5] = sw >= 0 ? -mul16(per, Q16(1.3)) : mul16(per, Q16(1.0));   // the gap's change a tick, log2
    v.k[6] = sw > 0 ? -mul16(a, 3 * octave) : 0;                // the ink's log2 now
    v.k[7] = sw >= 0 ? mul16(per, 3 * octave) : -mul16(per, 3 * octave);   // its change a tick
    v.k[8] = 0;                                                 // the next tick, samples
    v.k[9] = 0;                                                 // the tick now
    v.k[10] = c[4] * 32768;                                     // GRIT, Q31
    v.k[11] = decay(lmix(L(0.0012), L(0.005), c[4]));
    v.env[0].set(one, one, one);
    v.waiting = 1;
}
inline void setup_model(Voice& v, u32 m, const i32* c, i32 tune) {
    switch (m) {
    case M::SWEEP: setup_sweep(v, c, tune); break;
    case M::SCRATCH: setup_scratch(v, c, tune); break;
    case M::RING: setup_ring(v, c, tune); break;
    case M::STROKES: setup_strokes(v, c, tune); break;
    case M::DOT: setup_dot(v, c, tune); break;
    case M::CARD: setup_card(v, c, tune); break;
    case M::HATCH: setup_hatch(v, c, tune); break;
    case M::ORNAMENT: setup_ornament(v, c, tune); break;
    case M::STIPPLE: setup_stipple(v, c, tune); break;
    case M::BOOP: setup_boop(v, c, tune); break;
    case M::WAVE: setup_wave(v, c, tune); break;
    default: setup_claw(v, c, tune); break;
    }
}

// ------------------------------------------------------------ a block
FLICKER_APART void draw_sweep(Voice& v, i32* x) {
    i32 e0 = 0;
    const i32 de = v.env[0].next(e0);
    const u32 to = scale_inc(increment(v.k[0] + multiply(v.k[1], v.k[3])), v.fm);
    v.k[3] = multiply(v.k[3], v.k[2]);
    u32 inc = v.inc[0], ph = v.ph[0];
    const i32 di = glide_step(inc, to);
    const i32 bend = v.k[4], height = v.k[5];
    if (height <= 256) glide_draw<false>(x, ph, inc, di, bend, height, e0, de);
    else glide_draw<true>(x, ph, inc, di, bend, height, e0, de);
    v.ph[0] = ph;
    v.inc[0] = to;
    if (v.k[6] && v.ph[1] < 0xf0000000u) {
        // the inked tick: a dot passing the slit
        u32 p = v.ph[1];
        const i32 h = v.k[6] * 4096;   // Q16 of E
        for (unsigned i = 0; i < frames && p < 0xf0000000u; ++i) {
            const u32 q = p;
            p += v.inc[1];
            if (p < q) { p = 0xf0000000u; break; }
            x[i] += multiply(bump(q), h);
        }
        v.ph[1] = p;
    }
    crack(v, x);
    v.run = v.env[0].g;
}
FLICKER_APART void draw_scratch(Voice& v, i32* x) {
    i32 e0 = 0;
    i32 e1;
    const i32 de0 = v.env[0].next(e0), de1 = v.env[1].next(e1);
    const i32 lift = multiply(v.k[1], v.k[3]);
    v.k[3] = multiply(v.k[3], v.k[2]);
    const u32 to0 = scale_inc(increment(v.k[0] + lift), v.fm), to1 = scale_inc(increment(v.k[0] + L(1.47) + lift), v.fm);
    u32 a = v.inc[0], b = v.inc[1], pa = v.ph[0], pb = v.ph[1];
    const i32 da = glide_step(a, to0), db = glide_step(b, to1);
    i32 held = v.k[7], count = v.k[8];
    const i32 hold = v.k[4], f = film(v.k[5], v.fm), q = v.k[6];
    Svf sv = v.sv[0];
    u32 seed = v.seed;
    for (unsigned i = 0; i < frames; ++i) {
        pa += a; pb += b;
        a = static_cast<u32>(static_cast<i32>(a) + da);
        b = static_cast<u32>(static_cast<i32>(b) + db);
        const i32 body = (sine(pa) >> 2) + (sine(pb) >> 3);   // Q31 / 4 + / 8
        if (--count <= 0) { held = noise(seed) >> 1; count = hold; }
        const i32 n = sv.band(held >> 1, f, q);
        x[i] = (multiply(body, e0) >> 1) + div2k(multiply(n, e1), 2);
        e0 += de0;
        e1 += de1;
    }
    v.sv[0] = sv;
    v.seed = seed;
    v.k[7] = held;
    v.k[8] = count;
    v.ph[0] = pa; v.ph[1] = pb;
    v.inc[0] = to0; v.inc[1] = to1;
    crack(v, x);
    v.run = v.env[1].g > v.env[0].g ? v.env[1].g : v.env[0].g;
}
// RING's two drawn rings: the shape at pa and a sine at pb, each gliding.
template <bool Tall> inline void ring_draw(i32* x, u32& pa, u32& pb, u32 a, u32 b, i32 da, i32 db, i32 bend, i32 height,
                                           i32 e0, i32 de0, i32 e1, i32 de1) {
    u32 p = pa, q = pb;
    for (unsigned i = 0; i < frames; ++i) {
        p += a; q += b;
        a = static_cast<u32>(static_cast<i32>(a) + da);
        b = static_cast<u32>(static_cast<i32>(b) + db);
        x[i] = div2k(multiply(drawn_as<Tall>(p, bend, height), e0), 2) + multiply(sine(q) >> 3, e1);
        e0 += de0;
        e1 += de1;
    }
    pa = p;
    pb = q;
}
FLICKER_APART void draw_ring(Voice& v, i32* x) {
    i32 e0 = 0;
    i32 e1;
    const i32 de0 = v.env[0].next(e0), de1 = v.env[1].next(e1);
    const i32 lift = multiply(v.k[1], v.k[3]);
    v.k[3] = multiply(v.k[3], v.k[2]);
    const u32 to0 = scale_inc(increment(v.k[0] + lift), v.fm), to1 = scale_inc(increment(v.k[0] + L(1.52) + lift), v.fm);
    u32 a = v.inc[0], b = v.inc[1], pa = v.ph[0], pb = v.ph[1];
    const i32 da = glide_step(a, to0), db = glide_step(b, to1);
    const i32 bend = v.k[4], height = v.k[5];
    i32 burst = v.k[6];
    const i32 bk = v.k[7], f = v.k[8], q = v.k[9];
    Svf sv = v.sv[0];
    u32 seed = v.seed;
    if (height <= 256) ring_draw<false>(x, pa, pb, a, b, da, db, bend, height, e0, de0, e1, de1);
    else ring_draw<true>(x, pa, pb, a, b, da, db, bend, height, e0, de0, e1, de1);
    // the slap's burst, while it lasts
    for (unsigned i = 0; i < frames && burst; ++i) {
        x[i] += multiply(sv.band(multiply(noise(seed) >> 3, burst), f, q), Q31(0.9));
        burst = multiply(burst, bk);
        if (burst < (one >> 12)) burst = 0;   // gone 72 dB down
    }
    v.k[6] = burst;
    v.sv[0] = sv;
    v.seed = seed;
    v.ph[0] = pa; v.ph[1] = pb;
    v.inc[0] = to0; v.inc[1] = to1;
    crack(v, x);
    v.run = v.env[0].g;
}
FLICKER_APART void draw_strokes(Voice& v, i32* x) {
    i32 e0 = 0;
    const i32 de = v.env[0].next(e0);
    const i32 f = film(v.k[0], v.fm), q = v.k[1], bk = v.k[9];
    i32 burst = v.k[8], next = v.k[6], struck = v.k[7], fs = v.k[10] ? v.k[10] : f;
    // (the voice's settings in locals: the block's stores could reach them)
    const i32 k2 = v.k[2], k3 = v.k[3], k4 = v.k[4], k5 = v.k[5];
    i32 k11 = v.k[11];
    Svf sv = v.sv[0];
    u32 seed = v.seed;
    i32 e = e0;
    for (unsigned i = 0; i < frames; ++i) {
        if (struck <= k2 && next <= 0) {
            // a stroke: louder or softer, sooner or later and higher or lower by SCATTER
            const i32 r = static_cast<i32>(rng(seed) >> 17);   // 0..32767
            const i32 level = struck == k2 && struck ? k3 : one;
            burst = multiply(level, one - multiply(mul16(k5, r * 2) * 16384, Q31(0.5)));
            ++struck;
            // scattered strokes come further apart and less evenly, the later
            // ones later still: a loose clap, a crowd
            next = k4 + mul16(mul16(k4, k5), r * 3 + struck * 32768);
            if (next < 32) next = 32;
            const i32 tone = mul16(k5, (static_cast<i32>(seed >> 17) - 16384) * 2);   // -1..1 by SCATTER, Q16
            fs = clamp(opxy::q31::add(f, multiply(f, tone * 13000)), 1, one);   // up to 0.8 either way, held under one
        }
        --next;
        const i32 n = noise(seed) >> 3;
        // a scattered smear is many hands: its level jumps every 3 ms
        if (!(i & 127u) && k5) k11 = one - multiply(mul16(k5, static_cast<i32>(seed >> 16)) * 32767, Q31(0.85));
        const i32 smear = k5 ? multiply(e, k11) : e;
        const i32 y = sv.band(multiply(n, burst) * 2 + multiply(n, smear), struck > k2 && next < -400 ? f : fs, q);
        x[i] = y;
        burst = multiply(burst, bk);
        e += de;
    }
    v.k[6] = next;
    v.k[7] = struck;
    v.k[11] = k11;
    v.k[8] = burst;
    v.k[10] = fs;
    v.sv[0] = sv;
    v.seed = seed;
    if (struck > v.k[2]) v.waiting = 0;
    const i32 smear = v.env[0].g >= one / 4 ? one : clamp(v.env[0].g, 0, one / 4) * 4;
    v.run = smear > burst ? smear : burst;
}
FLICKER_APART void draw_dot(Voice& v, i32* x) {
    const i32 f = film(v.k[0], v.fm), q = v.k[1], g = film(v.k[11], v.fm), gq = v.k[12];
    const i32 ring = v.k[10], wood = v.k[13];
    Svf a = v.sv[0], b = v.sv[1];
    u32 p = v.ph[0];
    i32 next = v.k[6], drawn_ = v.k[7], ink = v.k[9];
    const i32 last = v.k[3] + (v.k[4] ? 1 : 0);
    for (unsigned i = 0; i < frames; ++i) {
        if (next == 0 && drawn_ <= last) {
            // the next dot reaches the slit: the first in full, each echo
            // fainter, the last as far as ECHO reaches
            ink = drawn_ == 0 ? one : drawn_ > v.k[3] ? multiply(v.k[8], v.k[4]) : v.k[8];
            if (drawn_) v.k[8] = multiply(v.k[8], Q31(0.6));
            ++drawn_;
            p = 0;
            next = v.k[5];
        }
        if (next > 0) --next;
        i32 d = 0;
        if (p < 0xf0000000u) {
            const u32 q0 = p;
            p += v.inc[0];
            if (p < q0) p = 0xf0000000u;
            else d = multiply(bump(q0), ink);
        }
        const i32 r = a.band(d >> 5, f, q), w = b.band(d >> 3, g, gq);
        x[i] = multiply(multiply(r, ring) + multiply(w, wood) + (d >> 6), Q31(0.9)) * 2;
    }
    v.ph[0] = p;
    v.k[6] = next;
    v.k[7] = drawn_;
    v.k[9] = ink;
    v.sv[0] = a;
    v.sv[1] = b;
    if (drawn_ > last && p >= 0xf0000000u) v.waiting = 0;
    i32 s;
    v.env[0].next(s);
    v.run = v.waiting ? one : v.env[0].g;
}
FLICKER_APART void draw_card(Voice& v, i32* x) {
    i32 e0 = 0;
    i32 e1;
    const i32 de0 = v.env[0].next(e0), de1 = v.env[1].next(e1);
    const i32 lift = multiply(v.k[2], v.k[3]);
    v.k[3] = multiply(v.k[3], v.k[4]);
    const u32 to0 = scale_inc(increment(v.k[0] + lift), v.fm), to1 = scale_inc(increment(v.k[0] + v.k[1] + lift), v.fm);
    u32 a = v.inc[0], b = v.inc[1], pa = v.ph[0], pb = v.ph[1];
    const i32 da = glide_step(a, to0), db = glide_step(b, to1);
    const i32 level = v.k[5], height = v.k[6], f = film(v.k[7], v.fm), q = v.k[8];
    Svf sv = v.sv[0];
    for (unsigned i = 0; i < frames; ++i) {
        pa += a; pb += b;
        a = static_cast<u32>(static_cast<i32>(a) + da);
        b = static_cast<u32>(static_cast<i32>(b) + db);
        const i32 w = (pulse(pa, level, height) >> 1) + (pulse(pb, level, height) >> 1);   // Q30
        const i32 g = e0 + e1;
        const i32 y = multiply(w, g);
        const i32 band = sv.band(y >> 1, f, q);
        x[i] = (y >> 3) + (band >> 2);
        e0 += de0;
        e1 += de1;
    }
    v.sv[0] = sv;
    v.ph[0] = pa; v.ph[1] = pb;
    v.inc[0] = to0; v.inc[1] = to1;
    v.run = v.env[0].g + v.env[1].g;
}
FLICKER_APART void draw_hatch(Voice& v, i32* x) {
    i32 e0 = 0;
    const i32 de = v.env[0].next(e0);
    u32 p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2], p3 = v.ph[3], p4 = v.ph[4], p5 = v.ph[5];
    const u32 i0 = scale_inc(v.inc[0], v.fm), i1 = scale_inc(v.inc[1], v.fm), i2 = scale_inc(v.inc[2], v.fm);
    const u32 i3 = scale_inc(v.inc[3], v.fm), i4 = scale_inc(v.inc[4], v.fm), i5 = scale_inc(v.inc[5], v.fm);
    const i32 metal = v.k[0], f = film(v.k[1], v.fm), q = steady(f, v.k[2] >> 1) * 2, ring = v.k[3];
    Svf sv = v.sv[0];
    u32 seed = v.seed;
    i32 e = e0;
    // how many of the six lines are dark, three at a time (so the phases
    // stay in registers), then the lines, the noise and the filter
    u32 count[frames];
    for (unsigned i = 0; i < frames; ++i) {
        p0 += i0; p1 += i1; p2 += i2;
        count[i] = (p0 >> 31) + (p1 >> 31) + (p2 >> 31);
    }
    for (unsigned i = 0; i < frames; ++i) {
        p3 += i3; p4 += i4; p5 += i5;
        count[i] += (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
    }
    for (unsigned i = 0; i < frames; ++i) {
        const i32 lines = static_cast<i32>(count[i]) * 0x0aaaaaaa - 0x1fffffff;
        const i32 n = noise(seed) >> 2;
        const i32 m = n + multiply(lines - n, metal);
        const i32 h = sv.high(m >> 1, f, q);
        x[i] = multiply(h + multiply(sv.bp, ring), e);
        e += de;
    }
    v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; v.ph[3] = p3; v.ph[4] = p4; v.ph[5] = p5;
    v.sv[0] = sv;
    v.seed = seed;
    v.run = v.env[0].g;
}
// ORNAMENT's bell, its ping over the wash, from level g by dg a sample (its
// own function, so its loop keeps the hardware loop).
FLICKER_APART void ornament_bell(Voice& v, i32* x, i32 g, i32 dg) {
    u32 b0 = v.ph[6], b1 = v.ph[7], b2 = v.ph[8];
    const u32 j0 = scale_inc(v.inc[6], v.fm), j1 = scale_inc(v.inc[7], v.fm), j2 = scale_inc(v.inc[8], v.fm);
    for (unsigned i = 0; i < frames; ++i) {
        b0 += j0; b1 += j1; b2 += j2;
        x[i] += multiply((sine(b0) >> 3) + (sine(b1) >> 4) + (sine(b2) >> 5), g);
        g += dg;
    }
    v.ph[6] = b0; v.ph[7] = b1; v.ph[8] = b2;
}
FLICKER_APART void draw_ornament(Voice& v, i32* x) {
    i32 e0 = 0, e1 = 0;
    const i32 de0 = v.env[0].next(e0), de1 = v.env[1].next(e1);
    u32 p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2], p3 = v.ph[3], p4 = v.ph[4], p5 = v.ph[5];
    const u32 i0 = scale_inc(v.inc[0], v.fm), i1 = scale_inc(v.inc[1], v.fm), i2 = scale_inc(v.inc[2], v.fm);
    const u32 i3 = scale_inc(v.inc[3], v.fm), i4 = scale_inc(v.inc[4], v.fm), i5 = scale_inc(v.inc[5], v.fm);
    const i32 w0 = v.k[0], w1 = v.k[1], w2 = v.k[2], wn = v.k[3];
    const i32 f = film(v.k[5], v.fm), q = steady(f, v.k[6] >> 1) * 2;
    Svf sv = v.sv[0];
    u32 seed = v.seed;
    i32 e = e0;
    // the three ring pairs, a pair at a time: two ruled lines crossing, light
    // where exactly one passes (+w, else -w, chosen by a mask)
    i32 rings[frames];
    for (unsigned i = 0; i < frames; ++i) {
        p0 += i0; p3 += i3;
        const i32 m = ~(static_cast<i32>(p0 ^ p3) >> 31);
        rings[i] = (w0 ^ m) - m;
    }
    for (unsigned i = 0; i < frames; ++i) {
        p1 += i1; p4 += i4;
        const i32 m = ~(static_cast<i32>(p1 ^ p4) >> 31);
        rings[i] += (w1 ^ m) - m;
    }
    for (unsigned i = 0; i < frames; ++i) {
        p2 += i2; p5 += i5;
        const i32 m = ~(static_cast<i32>(p2 ^ p5) >> 31);
        rings[i] += (w2 ^ m) - m;
    }
    for (unsigned i = 0; i < frames; ++i) {
        const i32 n = multiply(noise(seed) >> 3, wn);
        const i32 h = sv.high((rings[i] + n) >> 1, f, q);
        x[i] = div2k(multiply(opxy::q31::add(h, sv.bp >> 2), e), 1);
        e += de0;
    }
    v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; v.ph[3] = p3; v.ph[4] = p4; v.ph[5] = p5;
    v.sv[0] = sv;
    v.seed = seed;
    const i32 bell = v.k[4];
    if (bell && e1 > 4096) ornament_bell(v, x, multiply(e1, bell), multiply(de1, bell));
    v.run = v.env[0].g;
}
FLICKER_APART void draw_stipple(Voice& v, i32* x) {
    i32 e0 = 0;
    const i32 de = v.env[0].next(e0);
    const i32 f = film(v.k[0], v.fm), q = v.k[1], chance = v.k[2], dk = v.k[3], jingle = v.k[5];
    const i32 jf = film(v.k[6], v.fm), jq = v.k[7], kf = film(v.k[8], v.fm), kq = v.k[9];
    // JINGLE gathers the dots into clusters: their chance swings at the shake's rate
    v.ph[0] += v.inc[0] * 64u;
    const i32 sw = sine(v.ph[0]);
    const i32 c = chance + multiply(multiply(chance, jingle), sw);
    i32 dot = v.k[4];
    Svf a = v.sv[0], b = v.sv[1], d = v.sv[2];
    u32 seed = v.seed;
    i32 e = e0;
    // the dots first (a new one by a select, not a branch), then their bands
    i32 dots[frames];
    for (unsigned i = 0; i < frames; ++i) {
        const u32 r = rng(seed);
        dot = static_cast<i32>(r >> 1) < c ? static_cast<i32>(r << 1) >> 1 : dot;
        dots[i] = dot;
        dot = multiply(dot, dk);
    }
    if (jingle) {
        for (unsigned i = 0; i < frames; ++i) {
            const i32 y = a.band(dots[i] >> 2, f, q);
            i32 out = multiply(y, one - jingle);
            // the jingles ring long: fed a quarter as hard, heard four times as loud
            const i32 jj = opxy::q31::add(b.band(dots[i] >> 7, jf, jq), d.band(dots[i] >> 7, kf, kq));
            out = opxy::q31::add(out, clamp(multiply(jj, jingle), -(1 << 29), (1 << 29) - 1) * 4);
            x[i] = clamp(multiply(out, e), -(1 << 29), (1 << 29) - 1) * 4;
            e += de;
        }
    } else {
        for (unsigned i = 0; i < frames; ++i) {
            const i32 y = a.band(dots[i] >> 2, f, q);
            x[i] = clamp(multiply(multiply(y, one), e), -(1 << 29), (1 << 29) - 1) * 4;
            e += de;
        }
    }
    v.k[4] = dot;
    v.sv[0] = a; v.sv[1] = b; v.sv[2] = d;
    v.seed = seed;
    v.run = v.env[0].g;
}
FLICKER_APART void draw_boop(Voice& v, i32* x) {
    i32 e0 = 0;
    const i32 de = v.env[0].next(e0);
    // the vibrato comes in over the first 150 ms
    v.k[7] += (v.k[6] - v.k[7]) / 8;
    v.ph[1] += v.inc[1] * 64u;
    const i32 vib = multiply(v.k[7], sine(v.ph[1]));
    const u32 to = scale_inc(increment(v.k[0] + multiply(v.k[1], v.k[3]) + vib), v.fm);
    v.k[3] = multiply(v.k[3], v.k[2]);
    u32 inc = v.inc[0], ph = v.ph[0];
    const i32 di = glide_step(inc, to);
    const i32 bend = v.k[4], height = v.k[5];
    if (height <= 256) glide_draw<false>(x, ph, inc, di, bend, height, e0, de);
    else glide_draw<true>(x, ph, inc, di, bend, height, e0, de);
    v.ph[0] = ph;
    v.inc[0] = to;
    v.run = v.env[0].g;
}
FLICKER_APART void draw_wave(Voice& v, i32* x) {
    i32 e0 = 0;
    const i32 de = v.env[0].next(e0);
    const u32 a = scale_inc(v.inc[0], v.fm), b = scale_inc(v.inc[1], v.fm);
    // the low-pass: from open, falling to where SWEEP settles it
    const i32 lc = v.k[0] + L(1.2) + v.k[3] + multiply(L(7000) - v.k[0] - v.k[3], v.k[4]);
    v.k[4] = multiply(v.k[4], v.k[5]);
    const i32 f = svf_f(increment(clamp(lc, L(40), L(7000))));
    const i32 q = v.k[7], bend = v.k[1], height = v.k[2], sub = v.k[6];
    const i32 main = one - multiply(sub, Q31(0.45));   // the octave under takes the wave's place
    Svf sv = v.sv[0];
    u32 pa = v.ph[0], pb = v.ph[1];
    i32 e = e0;
    for (unsigned i = 0; i < frames; ++i) {
        pa += a; pb += b;
        const i32 w = multiply(sv.low(drawn(pa, bend, height) >> 1, f, q), main) + multiply(sine(pb) >> 1, sub);
        x[i] = div2k(multiply(w, e), 1);
        e += de;
    }
    v.sv[0] = sv;
    v.ph[0] = pa; v.ph[1] = pb;
    v.run = v.env[0].g;
}
FLICKER_APART void draw_claw(Voice& v, i32* x) {
    const i32 f = film(v.k[1], v.fm), q = v.k[2], grit = v.k[10], bk = v.k[11];
    Svf sv = v.sv[0];
    u32 seed = v.seed;
    i32 next = v.k[8], burst = v.k[9];
    const i32 end = v.k[4] - static_cast<i32>(v.t);
    for (unsigned i = 0; i < frames; ++i) {
        if (next <= 0 && static_cast<i32>(i) < end) {
            // a pull of the claw: the next frame's tick, its gap and ink moved by SWELL
            burst = frac2(v.k[6]);
            next = samples(v.k[3] - lsr);
            next += multiply(next * 64, ((1 << 30) - v.fm) * 2) / 64;   // the claw runs with the film
            v.k[3] = clamp(v.k[3] + v.k[5], L(40), L(44100.0 / 3));
            v.k[6] = clamp(v.k[6] + v.k[7], -6 * octave, 0);
        }
        --next;
        const i32 n = noise(seed) >> 3;
        const i32 ex = multiply((one >> 3) + multiply(n - (one >> 3), grit), burst);
        // the ring fed a quarter as hard and heard four times as loud, so its
        // state has room at the highest, longest ring
        const i32 ring = clamp(sv.band(ex >> 3, f, q), -(1 << 29), (1 << 29) - 1) * 4;
        x[i] = clamp(multiply(opxy::q31::add(ring, div2k(multiply(n, multiply(burst, grit)), 2)), Q31(0.9)), -(1 << 30), (1 << 30) - 1) * 2;
        burst = multiply(burst, bk);
    }
    v.k[8] = next;
    v.k[9] = burst;
    v.sv[0] = sv;
    v.seed = seed;
    if (end <= 64) v.waiting = 0;
    v.run = v.waiting ? frac2(v.k[6]) : burst;
}
inline void block_model(Voice& v, i32* x) {
    switch (v.model) {
    case M::SWEEP: draw_sweep(v, x); break;
    case M::SCRATCH: draw_scratch(v, x); break;
    case M::RING: draw_ring(v, x); break;
    case M::STROKES: draw_strokes(v, x); break;
    case M::DOT: draw_dot(v, x); break;
    case M::CARD: draw_card(v, x); break;
    case M::HATCH: draw_hatch(v, x); break;
    case M::ORNAMENT: draw_ornament(v, x); break;
    case M::STIPPLE: draw_stipple(v, x); break;
    case M::BOOP: draw_boop(v, x); break;
    case M::WAVE: draw_wave(v, x); break;
    default: draw_claw(v, x); break;
    }
}
}
