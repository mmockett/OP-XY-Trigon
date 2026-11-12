// A sweep of FLICKER's screen for tools/ub.sh: each scenario line on stdin,
//   model c0 c1 c2 c3 c4 c5 c6 c7 tune slit
// draws the screen at rest, then through a hit at 60 frames a second for two
// seconds, with Shift held for its last frames, and prints an FNV-1a hash of
// every frame, so builds by different compilers and flags can be compared bit
// for bit. Not part of the machine.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
flicker::DisplayState state;
flicker::Frame frame;
opxy::u32 hash(opxy::u32 h, const void* p, unsigned n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (unsigned i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}
}

int main() {
    using namespace opxy::draw;
    char line[256];
    while (std::fgets(line, sizeof line, stdin)) {
        int m, c[8], tune, slit;
        if (std::sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &tune, &slit) < 11) continue;
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        Input input;
        std::memset(&input, 0, sizeof input);
        input.track = 1;
        input.model = static_cast<opxy::u32>(m);
        for (int i = 0; i < 8; ++i) input.slots[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        for (int i = 0; i < 4; ++i) {
            input.shown[i] = static_cast<opxy::u8>(i);
            input.parameters[i] = input.slots[i];
        }
        input.pad[0] = static_cast<opxy::i8>(tune);
        input.pad[1] = static_cast<opxy::i8>(slit);
        input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = envelope_unavailable;
        input.elapsed = 17;
        flicker::display_reset(state);
        opxy::u32 h = 2166136261u;
        bool ok = true;
        for (int f = 0; f < 130; ++f) {
            if (f == 5) { input.note_serial += 1; input.held_notes = 1; }
            if (f == 12) { input.held_notes = 0; input.note_off_serial += 1; }
            if (f == 60) {   // the second row, then Shift
                for (int i = 0; i < 4; ++i) { input.shown[i] = static_cast<opxy::u8>(4 + i); input.parameters[i] = input.slots[4 + i]; }
                input.line = 1;
            }
            if (f >= 110) { input.shift = 1; input.pan = -35; input.level = -7; input.pad[2] = 34; input.pad[3] = 101; }
            flicker::display(state, input, frame);
            ok = ok && valid(frame);
            h = hash(h, &frame, sizeof frame);
        }
        std::printf("%08x %d\n", h, ok ? 1 : 0);
    }
    return 0;
}
