#pragma once
// FELT's kit: its twelve models, their eight controls, and the 24 keys'
// default sounds (keys 53..76, laid out as stock's drum kits are).
#include <opxy/types.hpp>

namespace felt {
namespace kit {
const opxy::u32 keys = 24;
const opxy::u32 models = 12;
struct Model {
    const char* name;
    const char* labels[8];
    unsigned char defaults[8];   // 0..127
};
const Model model[models] = {
    {"THUD", {"TUNE", "DECAY", "HAMMER", "RUBBER", "GLIDE", "KNOCK", "PEDAL", "AGE"}, {52, 60, 50, 70, 64, 50, 0, 0}},
    {"RATTLE", {"TUNE", "DECAY", "HAMMER", "PAPER", "SPOT", "RING", "PEDAL", "AGE"}, {60, 50, 70, 90, 60, 40, 0, 0}},
    {"TOM", {"TUNE", "DECAY", "HAMMER", "RUBBER", "GLIDE", "KNOCK", "PEDAL", "AGE"}, {64, 60, 60, 50, 25, 40, 0, 0}},
    {"LID", {"TUNE", "DECAY", "BOUNCE", "SPREAD", "SNAP", "HOLLOW", "PEDAL", "AGE"}, {64, 40, 64, 50, 64, 40, 0, 0}},
    {"TACK", {"TUNE", "DECAY", "TACK", "MUTE", "KNOCK", "SPOT", "PEDAL", "AGE"}, {70, 64, 80, 50, 50, 40, 0, 0}},
    {"BELL", {"TUNE", "DECAY", "HAMMER", "MASS", "SPOT", "LOOSE", "PEDAL", "AGE"}, {60, 50, 90, 60, 50, 0, 0, 0}},
    {"BRUSH", {"TUNE", "DECAY", "STROKE", "METAL", "BRISTLE", "SPOT", "PEDAL", "AGE"}, {64, 20, 30, 50, 40, 60, 0, 0}},
    {"PLANK", {"TUNE", "DECAY", "WIDTH", "HAMMER", "BLOOM", "BOLTS", "PEDAL", "AGE"}, {50, 60, 64, 60, 40, 30, 50, 0}},
    {"CHAIN", {"TUNE", "DECAY", "BEADS", "SHAKE", "METAL", "SWELL", "PEDAL", "AGE"}, {64, 30, 70, 50, 0, 40, 0, 0}},
    {"BOLT", {"TUNE", "DECAY", "HAMMER", "MASS", "SPOT", "LOOSE", "PEDAL", "AGE"}, {40, 80, 40, 70, 50, 0, 0, 0}},
    {"PLUCK", {"TUNE", "DECAY", "NAIL", "SPOT", "MUTE", "BUZZ", "PEDAL", "AGE"}, {40, 60, 70, 40, 25, 0, 0, 0}},
    {"BOW", {"TUNE", "DECAY", "PRESS", "DRAW", "BEND", "SPOT", "PEDAL", "AGE"}, {64, 60, 60, 60, 64, 50, 40, 0}},
};
// Each key's model and its eight controls.
const unsigned char key_model[keys] = {0, 1, 2, 2, 2, 3, 4, 5, 6, 6, 5, 7, 8, 9, 10, 11, 0, 1, 6, 2, 4, 9, 10, 3};
const unsigned char key_values[keys][8] = {
    {52, 60, 50, 70, 64, 50, 0, 0},      // 53 BD: THUD
    {60, 50, 70, 90, 60, 40, 0, 0},      // 54 SD: RATTLE
    {84, 56, 60, 50, 25, 40, 0, 0},      // 55 HT: TOM high
    {64, 60, 60, 50, 25, 40, 0, 0},      // 56 MT: TOM mid
    {44, 64, 60, 50, 25, 40, 0, 0},      // 57 LT: TOM low
    {64, 40, 64, 50, 64, 40, 0, 0},      // 58 CP: LID
    {70, 64, 80, 50, 50, 40, 0, 0},      // 59 RS: TACK
    {60, 50, 90, 60, 50, 0, 0, 0},       // 60 CB: BELL
    {64, 20, 30, 50, 40, 60, 0, 0},      // 61 CH: BRUSH closed
    {64, 80, 40, 50, 40, 60, 0, 0},      // 62 OH: BRUSH open
    {80, 100, 100, 40, 30, 0, 0, 0},     // 63 RC: BELL, a bell ride
    {50, 60, 64, 60, 40, 30, 50, 0},     // 64 CC: PLANK
    {64, 30, 70, 50, 0, 40, 0, 0},       // 65: CHAIN
    {40, 80, 40, 70, 50, 0, 0, 0},       // 66: BOLT, a low gong
    {40, 60, 70, 40, 25, 0, 0, 0},       // 67: PLUCK, bass
    {64, 60, 60, 60, 64, 50, 40, 0},     // 68: BOW
    {20, 90, 40, 50, 70, 40, 0, 0},      // 69: THUD, long and deep
    {70, 25, 80, 100, 70, 30, 0, 0},     // 70: RATTLE, tight
    {64, 10, 10, 50, 40, 60, 0, 0},      // 71: BRUSH, pedal
    {24, 70, 60, 50, 25, 40, 0, 0},      // 72: TOM, floor
    {90, 50, 30, 80, 70, 60, 0, 0},      // 73: TACK, a woodblock
    {90, 70, 70, 80, 60, 0, 0, 0},       // 74: BOLT, high, gamelan
    {90, 50, 70, 40, 50, 0, 0, 0},       // 75: PLUCK, high
    {50, 30, 0, 50, 100, 30, 0, 0},      // 76: LID, one slap
};
// The keys that start in choke group 1 (the brushes: closed, open, pedal).
const unsigned char key_choke[keys] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
}
}
