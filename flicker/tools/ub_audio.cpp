// A sweep of FLICKER's engine for tools/ub.sh: each scenario line on stdin,
//   model c0 c1 c2 c3 c4 c5 c6 c7 tune slit
// is played for up to six seconds (until the voice finishes), and a line with
// an FNV-1a hash of every output sample and the length is printed, so builds
// by different compilers and flags can be compared bit for bit. Not part of
// the machine.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>

int main() {
    static flicker::Voice voice;
    static flicker::Shared shared;
    char line[256];
    unsigned n = 0;
    while (std::fgets(line, sizeof line, stdin)) {
        int m, c[8], tune, slit;
        if (std::sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &tune, &slit) < 11) continue;
        std::memset(&voice, 0, sizeof voice);
        shared.notes = n++;
        flicker::initialize(voice, shared, 1);
        flicker::start(voice, shared, 1);
        opxy::i32 params[8];
        for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[2] = {tune, slit};
        opxy::u32 pitch[64] = {};
        opxy::i32 out[64];
        opxy::u32 h = 2166136261u;
        unsigned b = 0;
        for (; b < 6 * 44100 / 64; ++b) {
            flicker::drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
            flicker::drum_pad(voice, shared, 1, pads);
            flicker::render(voice, shared, 1, out, params, pitch);
            for (int i = 0; i < 64; ++i) h = (h ^ static_cast<opxy::u32>(out[i])) * 16777619u;
            if (flicker::finished(voice, shared)) { ++b; break; }
        }
        std::printf("%08x %u\n", h, b * 64);
    }
    return 0;
}
