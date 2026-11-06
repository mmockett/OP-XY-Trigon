// The host half of tools/cpu.py: renders each job line from stdin with the
// real engine and prints, for every block, which parts it ran (Voice::work)
// and how many modes and noise layers. Not part of the machine.
//   ./blocks < jobs     each line: model c0..c7 tune tone
#include "../engine.cpp"
#include <cstdio>
#include <cstring>

int main() {
    static dust::Voice voice;
    static dust::Shared shared;
    char line[512];
    unsigned job = 0;
    while (std::fgets(line, sizeof line, stdin)) {
        int m, c[8], tune, tone;
        if (std::sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &tune, &tone) < 11) continue;
        std::memset(&voice, 0, sizeof voice);
        shared.notes = job;
        dust::initialize(voice, shared, 1);
        dust::start(voice, shared, 1);
        opxy::i32 params[8];
        for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[2] = {tune, tone};
        opxy::u32 pitch[64] = {};
        opxy::i32 out[64];
        for (unsigned b = 0; b < 44100 * 6 / 64 && !(b && dust::finished(voice, shared)); ++b) {
            dust::drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
            dust::drum_pad(voice, shared, 1, pads);
            dust::render(voice, shared, 1, out, params, pitch);
            std::printf("%u %d %u %u %u %u %u\n", job, m, voice.work, voice.struck_modes, voice.ringing_modes, voice.noise_layers, voice.grain_layers);
        }
        ++job;
    }
    return 0;
}
