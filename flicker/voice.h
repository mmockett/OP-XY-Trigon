#pragma once
// One voice of FLICKER: a shape drawn on a film's optical soundtrack, read by
// a projector's sound head. Every model draws its own shape (models.h); every
// model's drawing then passes the same projector:
//
//   the drawing, its ink bled soft or laid on past the track's edges and
//   cut off there (INK)
//   -> dirt on the film (DUST) -> the light through the slit (SLIT, a
//   low-pass; FLUTTER's flicker) -> the photocell -> the valve amplifier
//   (a warm, even bend) -> the coupling capacitor (a high-pass at 25 Hz)
//
// The drawing is held in track edges: E (2^28) is a shape that just reaches
// both edges of the track. The film's speed (FLUTTER) scales every pitch.
#include "dsp.h"

namespace flicker {
// An attack that rises linearly and a decay, multiplied, stepped once a block
// and ramped through it.
struct Env {
    i32 a, ainc;   // the attack, Q31, and its rise a block
    i32 d, dk;     // the decay, Q31, and its factor a block
    i32 g;         // their product at the block's start
    void set(i32 level, i32 rise, i32 factor) {
        a = rise >= one ? one : rise;
        ainc = rise;
        d = level;
        dk = factor;
        g = multiply(a, d);
    }
    // The block's start, and its step a sample.
    i32 next(i32& start) {
        start = g;
        a = one - a <= ainc ? one : a + ainc;
        d = multiply(d, dk);
        if (d < (one >> 9)) d = multiply(d, dk);   // 54 dB down the drawing's taper ends twice as fast
        g = multiply(a, d);
        return (g - start) / 64;
    }
};

// INK's cut (ink_cut, below): the drawing's reach, Q31 of 8; the knee and the
// span past it, in E / 8; the parabola's curve, 1 / (32 span), Q31.
struct Cut { i32 ink, knee, span, curve; };

struct Voice {
    u32 owner, model, next, pending, done, quiet, waiting;
    i32 tune, slit;           // the key's pad controls (drum_pad)
    u32 seed, t;              // noise; samples since the note began
    i32 gain, lim;            // the model's output gain (Q31 of 64); the transient limiter's gain
    i32 run;                  // the drawing's height now, Q31: the film's dirt plays under it
    // the projector
    Cut cut;                  // INK: the drawing's reach and its edge
    i32 bleed, bleed_s;       // a thin ink's soft edge: a one-pole's coefficient (0: crisp) and state
    i32 slit_k, lens_k, cap_k;
    i32 slit_s, lens_s, cap_s;
    i32 valve;                // the valve's even bend, Q31 of a quarter (each drawing its own)
    i32 fm;                   // the film's speed, Q30 (FLUTTER)
    i32 am, am_k;             // the light's 24 Hz flicker now, Q31, and its depth
    i32 fl_depth, wow_depth;
    u32 fl_ph, fl_inc, wow_ph, wow_inc;
    // DUST: specks, a hair and grain on the film, the same each pass of a key's frame
    i32 dust;                 // Q31
    u32 dseed;
    i32 pop, pop_k, pop_wait, crackle, hair_at, hair_len;
    u32 hair_ph, hair_inc;
    // the model's own
    u32 ph[10], inc[10];
    i32 k[24];
    i32 s[16];
    Env env[3];
    Svf sv[3];
    OnePole op[3];
};

// INK: the ink's weight. A thin, watery ink bleeds into the film and its
// edges go soft (a low-pass, the drawing's bleed); a full ink sits crisp;
// laid on heavily the drawing reaches past the track's edges by the voice's
// ink and is cut off there. The cut is soft: linear up to the knee, then a
// parabola that meets the edge flat (the slit, after it, rounds the rest);
// the heavier the ink, the harder its edge: the knee rises from 0.6 of the
// edge toward it. x in track edges.
// (no branches: the bend is worked out over the knee and chosen by selects)
inline i32 ink_cut(i32 x, const Cut& c) {
    const i32 y = multiply(x, c.ink);   // x times ink / 8: the edge at 2^25
    const i32 a = magnitude(y);
    const i32 d = a - c.knee;
    const i32 dd = clamp(d, 0, c.span);
    const i32 bent = c.knee + dd - multiply(multiply(dd * 32, dd * 32), c.curve);
    i32 r = d >= c.span ? E / 8 : bent;
    r = d > 0 ? r : a;
    const i32 s = y >> 31;
    return ((r * 8) ^ s) - s;
}
}
