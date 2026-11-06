// FELT's sweep for tools/ub.sh: renders each scenario it reads (a model,
// eight controls and the pad's TUNE and SOFT) through the real engine for
// two seconds, and the real screen through a hit at each scenario's settings,
// and prints a hash of every sample and every frame. ub.sh builds it with
// several compilers and sanitizers; the hashes must agree bit for bit. Not
// part of the machine.
//   ./sweep < scenarios      (each line: model c0..c7 tune soft)
#include "../engine.cpp"
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
opxy::u32 fnv(opxy::u32 h, const void* data, unsigned n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
}

int main() {
    static felt::Voice voice;
    static felt::Shared shared;
    static felt::DisplayState state;
    static felt::Frame frame;
    char line[256];
    unsigned count = 0;
    while (std::fgets(line, sizeof line, stdin)) {
        int m, c[8], tune, soft;
        if (std::sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &tune, &soft) < 11) continue;
        // the sound
        std::memset(&voice, 0, sizeof voice);
        shared.notes = 0;
        felt::initialize(voice, shared, 1);
        felt::start(voice, shared, 1);
        opxy::i32 params[8];
        for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[2] = {tune, soft};
        opxy::u32 pitch[64] = {};
        opxy::i32 out[64];
        opxy::u32 h = 2166136261u;
        unsigned blocks = 0;
        for (; blocks < 44100 * 2 / 64; ++blocks) {
            felt::drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
            felt::drum_pad(voice, shared, 1, pads);
            felt::render(voice, shared, 1, out, params, pitch);
            h = fnv(h, out, sizeof out);
            if (felt::finished(voice, shared)) break;
        }
        // the screen: at rest, then a hit followed for two seconds at 60 frames a second
        opxy::draw::Input input;
        std::memset(&input, 0, sizeof input);
        input.track = 1;
        input.model = static_cast<opxy::u32>(m);
        for (int i = 0; i < 8; ++i) input.slots[i] = params[i];
        for (int i = 0; i < 4; ++i) { input.shown[i] = static_cast<opxy::u8>(i); input.parameters[i] = params[i]; }
        input.pad[0] = static_cast<opxy::i8>(tune);
        input.pad[1] = static_cast<opxy::i8>(soft);
        input.elapsed = 17;
        input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
        std::memset(&frame, 0, sizeof frame);
        felt::display_reset(state);
        opxy::u32 hd = 2166136261u;
        for (int f = 0; f < 120; ++f) {
            if (f == 1) input.note_serial = 1;
            if (f == 60) input.shift = 1;
            felt::display(state, input, frame);
            hd = fnv(hd, &frame, sizeof frame);
        }
        std::printf("%u %08x %08x %u\n", count++, h, hd, blocks);
    }
    return 0;
}
