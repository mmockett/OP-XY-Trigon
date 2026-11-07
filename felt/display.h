#pragma once
// FELT's screen: the prepared strings as an isometric plate (art.h,
// panel.svg.fragment), drawn for the selected key's model and controls.
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "art.h"
#include "shape.h"

namespace felt {
using Frame = opxy::draw::Frame<4, 64, art::node_count, 16, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 initialized, track, serial;
    opxy::i32 age;        // Q10 seconds since the selected key last played
    opxy::u32 spare;      // keeps the declared stateBytes
};
}
