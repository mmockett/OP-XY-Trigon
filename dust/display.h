#pragma once
// DUST's screen: a portable record player with a soul 45 on it (art.h,
// panel.svg.fragment), drawn from the selected key's controls and played by
// its hits.
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "art.h"
#include "record.h"

namespace dust {
using Frame = opxy::draw::Frame<4, 64, art::node_count, 16, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 initialized, track, serial;
    opxy::i32 age;        // Q10 seconds since the selected key's last hit
};
}
