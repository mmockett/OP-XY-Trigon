#pragma once
// DUST's kit: fifteen hits from one soul 45, the record player's eight
// controls on every one, and the 24 keys' default sounds (keys 53..76, laid
// out as stock's drum kits are).
#include <opxy/types.hpp>

namespace dust {
namespace kit {
const opxy::u32 keys = 24;
const opxy::u32 models = 15;
struct Model {
    const char* name;
    const char* labels[8];
    unsigned char defaults[8];   // 0..127
};
#define DUST_LABELS {"SPEED", "PICK", "LIFT", "WEIGHT", "CUE", "WEAR", "WARP", "DRAG"}
const Model model[models] = {
    {"KICK", DUST_LABELS, {64, 72, 96, 76, 64, 0, 0, 0}},
    {"SNARE", DUST_LABELS, {64, 72, 96, 76, 64, 0, 0, 0}},
    {"RIM", DUST_LABELS, {64, 40, 96, 76, 64, 0, 0, 0}},
    {"HAT", DUST_LABELS, {64, 64, 96, 76, 64, 0, 0, 0}},
    {"OPEN", DUST_LABELS, {64, 64, 96, 76, 64, 0, 0, 0}},
    {"RIDE", DUST_LABELS, {64, 56, 96, 76, 64, 0, 0, 0}},
    {"CRASH", DUST_LABELS, {64, 72, 96, 76, 64, 0, 0, 0}},
    {"TOM", DUST_LABELS, {64, 72, 96, 76, 64, 0, 0, 0}},
    {"CONGA", DUST_LABELS, {64, 64, 96, 76, 64, 0, 0, 0}},
    {"BONGO", DUST_LABELS, {64, 64, 96, 76, 64, 0, 0, 0}},
    {"TAMB", DUST_LABELS, {64, 64, 96, 76, 64, 0, 0, 0}},
    {"COWBELL", DUST_LABELS, {64, 72, 96, 76, 64, 0, 0, 0}},
    {"CLAP", DUST_LABELS, {64, 72, 96, 76, 64, 0, 0, 0}},
    {"SHAKER", DUST_LABELS, {64, 64, 96, 76, 64, 0, 0, 0}},
    {"BASS", DUST_LABELS, {64, 8, 96, 76, 64, 0, 0, 0}},
};
#undef DUST_LABELS
// Each key's model and its eight controls.
const unsigned char key_model[keys] = {0, 1, 7, 7, 7, 12, 2, 11, 3, 4, 5, 6, 10, 8, 9, 13, 0, 1, 3, 8, 2, 14, 14, 5};
const unsigned char key_values[keys][8] = {
    {64, 72, 96, 76, 64, 0, 0, 0},     // 53 BD: KICK
    {64, 72, 96, 76, 64, 0, 0, 0},     // 54 SD: SNARE
    {90, 72, 96, 76, 64, 0, 0, 0},     // 55 HT: TOM high, the platter faster
    {64, 72, 96, 76, 64, 0, 0, 0},     // 56 MT: TOM mid
    {15, 72, 96, 76, 64, 0, 0, 0},     // 57 LT: TOM low, the platter slower
    {64, 72, 96, 76, 64, 0, 0, 0},     // 58 CP: CLAP
    {64, 40, 96, 76, 64, 0, 0, 0},     // 59 RS: RIM, cross-stick
    {64, 72, 96, 76, 64, 0, 0, 0},     // 60 CB: COWBELL
    {64, 64, 96, 76, 64, 0, 0, 0},     // 61 CH: HAT
    {64, 64, 96, 76, 64, 0, 0, 0},     // 62 OH: OPEN
    {64, 56, 96, 76, 64, 0, 0, 0},     // 63 RC: RIDE
    {64, 72, 96, 76, 64, 0, 0, 0},     // 64 CC: CRASH
    {64, 64, 96, 76, 64, 0, 0, 0},     // 65: TAMB
    {76, 64, 96, 76, 64, 0, 0, 0},     // 66: CONGA high
    {64, 64, 96, 76, 64, 0, 0, 0},     // 67: BONGO
    {64, 64, 96, 76, 64, 0, 0, 0},     // 68: SHAKER
    {0, 72, 100, 76, 64, 0, 0, 0},     // 69: KICK at 33, heavier
    {64, 12, 96, 76, 64, 0, 0, 0},     // 70: SNARE, a ghost from the first bar
    {64, 24, 40, 76, 64, 0, 0, 0},     // 71: HAT, the pedal, short
    {40, 64, 96, 76, 64, 0, 0, 0},     // 72: CONGA low
    {64, 100, 96, 90, 64, 0, 0, 0},    // 73: RIM, a rimshot from bar 7, the arm a little heavy
    {64, 8, 96, 76, 64, 0, 0, 0},      // 74: BASS
    {64, 88, 96, 76, 64, 0, 0, 0},     // 75: BASS, an octave up (bar 6)
    {64, 116, 96, 76, 64, 0, 0, 0},    // 76: RIDE, the bell (the last bar)
};
// The keys that start in choke group 1 (the hats: closed, open, pedal).
const unsigned char key_choke[keys] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
}
}
