#pragma once
// The record player's eight controls as physical values, shared by the
// engine and the screen, so what the screen shows is what the voice plays.
// Controls arrive as raw 0..127; values come back as log2 (Q16) or whole
// units. No tables: both translation units include it.
#include "lit.h"

namespace dust {
namespace rec {
using opxy::i32;
using opxy::u32;
enum Model : u32 { KICK, SNARE, RIM, HAT, OPEN, RIDE, CRASH, TOM, CONGA, BONGO, TAMB, COWBELL, CLAP, SHAKER, BASS, MODELS };
enum Slot : u32 { SPEED, PICK, LIFT, WEIGHT, CUE, WEAR, WARP, DRAG };

inline i32 raw(i32 v) { return v < 0 ? 0 : v > 127 ? 127 : v; }

// SPEED: 33 1/3 at 0, 45 at 64, 78 at 127, evenly in log between them.
// log2 of the platter's rate over 45 rpm, Q16.
inline i32 speed_log(i32 r) {
    r = raw(r);
    if (r <= 64) return -((L(45.0 / 33.3333333) * (64 - r)) / 64);
    return (L(78.0 / 45.0) * (r - 64)) / 63;
}
// PICK: the bar of the break the needle drops on, 1..8.
inline i32 bar(i32 r) { return raw(r) / 16 + 1; }
// how hard the bar is played, Q16 (0 the softest ghost, 65536 the hardest)
inline i32 force(i32 r) { return (raw(r) * 65536) / 127; }
// the band bleeding in, from bar 7 to the last, Q16
inline i32 bleed(i32 r) {
    const i32 b = (raw(r) - 100) * 65536 / 27;
    return b < 0 ? 0 : b > 65536 ? 65536 : b;
}
// LIFT: the hit's length, each model's own span (log2 s, Q16).
const i32 lift_lo[MODELS] = {L(0.06), L(0.05), L(0.03), L(0.02), L(0.08), L(0.15), L(0.2), L(0.07), L(0.05), L(0.04), L(0.05), L(0.04), L(0.05), L(0.04), L(0.08)};
const i32 lift_hi[MODELS] = {L(0.6), L(0.42), L(0.2), L(0.3), L(1.4), L(2.6), L(2.8), L(1.0), L(0.8), L(0.5), L(0.24), L(0.6), L(0.34), L(0.2), L(1.4)};
inline i32 lift_log(u32 m, i32 r) {
    m = m < MODELS ? m : 0;
    return lift_lo[m] + ((lift_hi[m] - lift_lo[m]) * raw(r)) / 127;
}
// WEIGHT: the tracking force, 0.5 to 5 g evenly in log; 2 g (76) tracks clean.
inline i32 weight_log(i32 r) { return L(0.5) + (L(10.0) * raw(r)) / 127; }
const i32 weight_neutral = 76;
// CUE: early (the needle lands before the hit, up to 25 ms, in whole
// blocks of 64 samples) or late (into the hit: the hit comes up out of the
// groove over that long, up to each model's own reach, 45 ms on a hat to
// 0.3 s on a crash, for a cymbal a swell).
inline u32 cue_blocks(i32 r) { r = raw(r); return r >= 64 ? 0u : static_cast<u32>(((64 - r) * 17 + 32) / 64); }
// the early lead in tenths of a millisecond, as the blocks make it
inline i32 cue_early_tenths(i32 r) { return static_cast<i32>((cue_blocks(r) * 64000u + 2205u) / 4410u); }
// the late landing in tenths of a millisecond
const short late_most[MODELS] = {720, 500, 450, 450, 1680, 3000, 3000, 1200, 960, 600, 450, 720, 450, 450, 1680};   // tenths of a ms
inline i32 cue_late_tenths(u32 m, i32 r) { r = raw(r); return r <= 64 ? 0 : ((r - 64) * late_most[m < MODELS ? m : 0] + 31) / 63; }
// WARP: the wow's depth, up to 150 cents either way (log2 Q16)
// (heard soon after the disc leaves flat: the depth rises quickly at first)
inline i32 warp_share(i32 r) { const i32 d = force(r) >> 1; return 3 * d - ((d * d) >> 15); }   // Q16, 0..65536
inline i32 warp_log(i32 r) { return (warp_share(r) * (L(2.0) * 150 / 1200)) >> 16; }
inline i32 warp_cents(i32 r) { return (warp_share(r) * 150 + 32768) >> 16; }
// DRAG: how far the brush would slow the platter, up to 12 semitones (an
// octave), most of it within a quarter second
// (heard soon after it touches: the sag rises quickly at first)
inline i32 drag_share(i32 r) { const i32 d = force(r) >> 1; return 3 * d - ((d * d) >> 15); }   // Q16, 0..65536
inline i32 drag_log(i32 r) { return drag_share(r); }   // up to an octave (65536)
inline i32 drag_tenths(i32 r) { return (drag_share(r) * 120 + 32768) >> 16; }
}
}
