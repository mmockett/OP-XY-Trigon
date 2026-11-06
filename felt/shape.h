#pragma once
// FELT's control ranges, shared by the engine and the screen, so the picture
// moves by the same numbers the sound does (the screen cannot hear the audio):
// each model's pitch and damper range as log2 ends (Q16), and the times a
// gesture takes.
#include <opxy/types.hpp>
#include "units.h"

namespace felt {
// The models, in the order kit.h names them.
struct M { enum : opxy::u32 { THUD, RATTLE, TOM, LID, TACK, BELL, BRUSH, PLANK, CHAIN, BOLT, PLUCK, BOW }; };
const opxy::u32 model_count = 12;
namespace shape {
using opxy::i32;
struct Range { i32 a, b; };   // log2 of a range's ends, Q16
// TUNE: the struck string's fundamental (Hz); LID the slap's band, BRUSH the
// lowest screwed top string, CHAIN the beads' ring, PLANK its lowest key.
const Range pitch[12] = {
    {L(32.0), L(110.0)},  {L(130.0), L(350.0)}, {L(65.0), L(420.0)},  {L(450.0), L(2600.0)},
    {L(250.0), L(1600.0)}, {L(300.0), L(2500.0)}, {L(2400.0), L(6400.0)}, {L(65.0), L(330.0)},
    {L(1800.0), L(7200.0)}, {L(55.0), L(500.0)},  {L(41.2), L(330.0)},  {L(55.0), L(880.0)}};
// DECAY: when the key rises and the damper lands (s); LID its slap's tail,
// BRUSH, CHAIN their ring, BOW the ring once the thread lifts.
const Range damper[12] = {
    {L(0.06), L(1.0)},  {L(0.05), L(0.8)},  {L(0.05), L(1.3)},  {L(0.04), L(0.9)},
    {L(0.03), L(0.45)}, {L(0.08), L(3.0)},  {L(0.025), L(1.0)}, {L(0.3), L(3.0)},
    {L(0.03), L(1.2)},  {L(0.15), L(4.5)},  {L(0.1), L(2.0)},   {L(0.1), L(4.0)}};
// LID: the first bounce's gap (SPREAD), s; CHAIN: the shake (SHAKE), s; BOW:
// the draw (DRAW), s; PLANK: the bloom (BLOOM), s.
const Range gap = {L(0.006), L(0.045)};
const Range shake = {L(0.02), L(0.5)};
const Range draw = {L(0.15), L(4.0)};
const Range bloom = {L(0.004), L(0.6)};
// BRUSH: the stroke's length (STROKE), s.
const Range stroke = {L(0.0012), L(0.075)};
// LID: each bounce's gap and strength over the last's, Q31 fractions.
const i32 bounce_gap = 1760936591;    // 0.82
const i32 bounce_height = 1717986918; // 0.8
// A unit t (Q16, 0..65536) along a range, as log2.
static inline i32 along(const Range& r, i32 t) {
    const i32 d = (r.b - r.a) / 256;   // ranges span at most 8 octaves
    return r.a + d * (t < 0 ? 0 : t > 65536 ? 65536 : t) / 256;
}
}
}
