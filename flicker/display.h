#pragma once
// FLICKER's screen: a strip of 16 mm sound film lying on an isometric slat
// under a small gate (art.h, panel.svg.fragment), its soundtrack drawn with
// the selected key's sound.
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "art.h"
#include "units.h"

namespace flicker {
using Frame = opxy::draw::Frame<4, 64, art::node_count, 16, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 initialized, track, serial;
    opxy::i32 age;        // Q10 seconds since the selected key's last note
    opxy::i32 from, to;   // the film's travel, Q10 px: where the pull began and where it ends
    opxy::i32 span;       // the pull's length, Q10 s
    opxy::u32 drawn;      // the drawing's key: what the paths hold
};
}
