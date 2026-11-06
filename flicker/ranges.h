#pragma once
// The ends of FLICKER's controls, shared by the engine (models.h) and the
// screen's captions (display.cpp): log2 values in Q16 (Hz, seconds, ratios),
// semitones, counts. Compile-time constants only, so either translation unit
// may include it without duplicating data.
#include "units.h"

namespace flicker {
namespace range {
using opxy::i32;
// A drawing's decay is shown as the time it takes to fall 40 dB (4.6 time constants).
constexpr double shown = 4.605;
// SWEEP
constexpr i32 sweep_pitch[2] = {L(30), L(100)};             // Hz, the note it falls to
constexpr i32 sweep_decay[2] = {L(0.06), L(0.9)};           // s, the taper's time constant
constexpr i32 sweep_bend = 48;                              // semitones at most
// SCRATCH
constexpr i32 scratch_pitch[2] = {L(120), L(420)};          // Hz, the lower tone
constexpr i32 scratch_decay[2] = {L(0.05), L(0.6)};         // s, the scratches' time constant
constexpr i32 scratch_snap = 24;                            // semitones
// RING
constexpr i32 ring_pitch[2] = {L(55), L(660)};
constexpr i32 ring_decay[2] = {L(0.05), L(1.0)};
constexpr i32 ring_bend = 12;
// STROKES
constexpr i32 strokes_pitch[2] = {L(500), L(3000)};          // Hz, the band
constexpr i32 strokes_decay[2] = {L(0.04), L(0.7)};          // s, the smear
constexpr i32 strokes_spread[2] = {L(0.003), L(0.03)};       // s between strokes
// DOT
constexpr i32 dot_pitch[2] = {L(300), L(4500)};
constexpr i32 dot_decay[2] = {L(0.01), L(0.4)};
constexpr i32 dot_size[2] = {L(0.0001), L(0.0025)};          // s, the dot's length past the slit
constexpr i32 dot_gap[2] = {L(0.012), L(0.25)};
// CARD
constexpr i32 card_pitch[2] = {L(180), L(2200)};
constexpr i32 card_decay[2] = {L(0.03), L(0.9)};
constexpr i32 card_ratio[2] = {L(1.0), L(2.0)};
constexpr i32 card_bend = 24;
// HATCH
constexpr i32 hatch_pitch[2] = {L(180), L(900)};             // Hz, the widest lines' rate
constexpr i32 hatch_decay[2] = {L(0.01), L(1.2)};
constexpr i32 hatch_edge[2] = {L(2600), L(7000)};            // Hz, the high-pass
// ORNAMENT
constexpr i32 ornament_pitch[2] = {L(220), L(900)};
constexpr i32 ornament_decay[2] = {L(0.25), L(1.3)};
constexpr i32 ornament_swell[2] = {L(0.02), L(0.6)};          // s, the bloom (0 at the bottom)
// STIPPLE
constexpr i32 stipple_pitch[2] = {L(2500), L(9000)};
constexpr i32 stipple_decay[2] = {L(0.025), L(0.6)};
constexpr i32 stipple_density[2] = {L(300), L(12000)};       // dots a second
constexpr i32 stipple_swell[2] = {0, L(0.25)};              // s, the shake easing in, as SWELL squared
// BOOP
constexpr i32 boop_pitch[2] = {L(65.40639), L(2093.005)};    // C2 .. C7
constexpr i32 boop_decay[2] = {L(0.05), L(1.6)};
constexpr i32 boop_slide = 24;                               // semitones either way
// WAVE
constexpr i32 wave_pitch[2] = {L(27.5), L(220.0)};           // A0 .. A3
constexpr i32 wave_decay[2] = {L(0.08), L(1.6)};
// CLAW
constexpr i32 claw_pitch[2] = {L(250), L(3500)};
constexpr i32 claw_decay[2] = {L(0.15), L(3.0)};             // s, the roll's length
constexpr i32 claw_rate[2] = {L(6), L(60)};                  // frames a second
}
}
