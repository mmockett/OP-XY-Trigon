#pragma once
// FLICKER's kit: its twelve models, their eight controls, and the 24 keys'
// default sounds (keys 53..76, laid out as stock's drum kits are).
#include <opxy/types.hpp>

namespace flicker {
namespace kit {
const opxy::u32 keys = 24;
const opxy::u32 models = 12;
struct Model {
    const char* name;
    const char* labels[8];
    unsigned char defaults[8];   // 0..127
};
// Row 2 of every model ends INK, FLUTTER, DUST: how the drawing is inked and
// how the projector plays it.
const Model model[models] = {
    {"SWEEP", {"PITCH", "DECAY", "BEND", "SHAPE", "CLICK", "INK", "FLUTTER", "DUST"}, {46, 52, 70, 30, 40, 24, 0, 0}},
    {"SCRATCH", {"PITCH", "DECAY", "SCRATCH", "SNAP", "GRAIN", "INK", "FLUTTER", "DUST"}, {60, 56, 72, 50, 40, 24, 0, 0}},
    {"RING", {"PITCH", "DECAY", "BEND", "SHAPE", "SLAP", "INK", "FLUTTER", "DUST"}, {64, 50, 40, 24, 40, 24, 0, 0}},
    {"STROKES", {"PITCH", "DECAY", "STROKES", "SPREAD", "SCATTER", "INK", "FLUTTER", "DUST"}, {60, 50, 76, 50, 30, 24, 0, 0}},
    {"DOT", {"PITCH", "DECAY", "SIZE", "ECHO", "GAP", "INK", "FLUTTER", "DUST"}, {80, 40, 40, 32, 50, 24, 0, 0}},
    {"CARD", {"PITCH", "DECAY", "RATIO", "WIDTH", "BEND", "INK", "FLUTTER", "DUST"}, {64, 50, 61, 60, 20, 24, 0, 0}},
    {"HATCH", {"PITCH", "DECAY", "METAL", "EDGE", "SPREAD", "INK", "FLUTTER", "DUST"}, {64, 26, 60, 64, 50, 24, 0, 0}},
    {"ORNAMENT", {"PITCH", "DECAY", "RINGS", "BELL", "SWELL", "INK", "FLUTTER", "DUST"}, {64, 70, 50, 40, 0, 24, 0, 0}},
    {"STIPPLE", {"PITCH", "DECAY", "DENSITY", "SWELL", "JINGLE", "INK", "FLUTTER", "DUST"}, {64, 40, 92, 40, 0, 24, 0, 0}},
    {"BOOP", {"PITCH", "DECAY", "SLIDE", "SHAPE", "VIBRATO", "INK", "FLUTTER", "DUST"}, {64, 50, 72, 30, 0, 24, 0, 0}},
    {"WAVE", {"PITCH", "DECAY", "SHAPE", "SWEEP", "SUB", "INK", "FLUTTER", "DUST"}, {50, 56, 90, 60, 30, 24, 0, 0}},
    {"CLAW", {"PITCH", "DECAY", "RATE", "SWELL", "GRIT", "INK", "FLUTTER", "DUST"}, {60, 60, 64, 40, 40, 24, 0, 0}},
};
// Each key's model and its eight controls.
const unsigned char key_model[keys] = {0, 1, 2, 2, 2, 3, 4, 5, 6, 6, 7, 7, 8, 9, 10, 11, 0, 1, 6, 2, 4, 5, 9, 3};
const unsigned char key_values[keys][8] = {
    {46, 52, 70, 30, 40, 24, 0, 0},       // 53 BD: SWEEP
    {60, 56, 72, 50, 40, 24, 0, 0},       // 54 SD: SCRATCH
    {78, 46, 40, 24, 40, 24, 0, 0},       // 55 HT: RING high
    {64, 50, 40, 24, 40, 24, 0, 0},       // 56 MT: RING mid
    {50, 54, 40, 24, 40, 24, 0, 0},       // 57 LT: RING low
    {60, 50, 76, 50, 30, 24, 0, 0},       // 58 CP: STROKES
    {90, 40, 30, 0, 50, 24, 0, 0},        // 59 RS: DOT, a rim
    {64, 50, 61, 60, 20, 24, 0, 0},       // 60 CB: CARD cowbell
    {64, 26, 60, 64, 50, 24, 0, 0},       // 61 CH: HATCH closed
    {64, 86, 60, 64, 50, 24, 0, 0},       // 62 OH: HATCH open
    {70, 70, 40, 44, 0, 24, 0, 0},        // 63 RC: ORNAMENT ride
    {56, 86, 90, 10, 20, 24, 0, 0},       // 64 CC: ORNAMENT crash
    {64, 40, 92, 40, 0, 24, 0, 0},        // 65: STIPPLE shaker
    {64, 50, 72, 30, 0, 24, 0, 0},        // 66: BOOP
    {50, 56, 90, 60, 30, 24, 0, 0},       // 67: WAVE bass
    {60, 60, 64, 40, 40, 24, 0, 0},       // 68: CLAW
    {30, 84, 80, 10, 30, 24, 0, 0},       // 69: SWEEP long and deep
    {72, 34, 80, 40, 30, 24, 0, 0},       // 70: SCRATCH tight
    {64, 16, 60, 72, 50, 24, 0, 0},       // 71: HATCH pedal
    {70, 44, 20, 50, 80, 24, 0, 0},       // 72: RING conga
    {82, 48, 24, 0, 50, 24, 0, 0},        // 73: DOT clave
    {90, 40, 40, 64, 0, 24, 0, 0},        // 74: CARD beep
    {96, 40, 60, 20, 0, 24, 0, 0},        // 75: BOOP high
    {64, 62, 0, 50, 30, 24, 0, 0},        // 76: STROKES one snap
};
// The keys that start in choke group 1 (the hats: closed, open, pedal).
const unsigned char key_choke[keys] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
}
}
