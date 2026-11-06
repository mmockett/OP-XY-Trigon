// DUST's sweep for tools/ub.sh: renders each job line from stdin with the
// real engine (tools/ubframes.cpp draws the screen through a hit the same way) and prints a hash of everything it produced, so builds by different
// compilers and flags can be compared bit for bit. Not part of the machine.
//   ./ubsweep < jobs     each line: model c0..c7 tune tone
#include "../engine.cpp"
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
        static dust::Voice voice;
        static dust::Shared shared;
        std::memset(&voice, 0, sizeof voice);
        shared.notes = job;
        dust::initialize(voice, shared, 1);
        dust::start(voice, shared, 1);
        opxy::i32 params[8];
        for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[2] = {tune, tone};
        opxy::u32 pitch[64] = {};
        opxy::i32 out[64];
        unsigned b = 0;
        for (; b < 44100 * 4 / 64 && !(b && dust::finished(voice, shared)); ++b) {
            dust::drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
            dust::drum_pad(voice, shared, 1, pads);
            dust::render(voice, shared, 1, out, params, pitch);
            mix(out, sizeof out);
        }
        std::printf("%u %08x %u\n", job, hash, b);
        ++job;
    }
    return 0;
}
