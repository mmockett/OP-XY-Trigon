// DUST's screen sweep for tools/ub.sh: draws the real screen through a hit
// for each job line from stdin (tools/ubsweep.cpp renders the engine) and prints a hash of everything it produced, so builds by different
// compilers and flags can be compared bit for bit. Not part of the machine.
//   ./ubframes < jobs     each line: model c0..c7 tune tone
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
unsigned hash = 2166136261u;
void mix(const void* p, unsigned n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (unsigned i = 0; i < n; ++i) hash = (hash ^ b[i]) * 16777619u;
}
}

int main() {
    char line[512];
    unsigned job = 0;
    while (std::fgets(line, sizeof line, stdin)) {
        int m, c[8], tune, tone;
        if (std::sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &tune, &tone) < 11) continue;
        hash = 2166136261u;
        static dust::DisplayState state;
        static dust::Frame frame;
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        opxy::draw::Input input;
        std::memset(&input, 0, sizeof input);
        input.track = 1;
        input.model = static_cast<opxy::u32>(m);
        for (int i = 0; i < 8; ++i) input.slots[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        input.pad[0] = static_cast<opxy::i8>(tune);
        input.pad[1] = static_cast<opxy::i8>(tone);
        input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
        input.elapsed = 17;
        dust::display_reset(state);
        for (int f = 0; f < 400; ++f) {
            if (f == 5) { input.note_serial = 1; input.held_notes = 1; }
            if (f == 9) { input.held_notes = 0; input.note_off_serial = 1; }
            if (f == 300) { input.shift = 1; input.line = 1; input.shown[0] = 4; }
            dust::display(state, input, frame);
            mix(&frame, sizeof frame);
        }
        std::printf("%u %08x %d\n", job, hash, opxy::draw::valid(frame) ? 1 : 0);
        ++job;
    }
    return 0;
}
