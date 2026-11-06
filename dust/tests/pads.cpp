// DUST's pad controls in the voice (the SDK's padControls, drum_pad):
// at 0 a key sounds exactly as without them; TUNE moves a key's pitch by
// semitones; TONE brightens or darkens the key and never passes the SDK's
// ceiling; values arrive clamped; their captions read as the
// header shows them. And a finished voice renders silence until its next note.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {
int failures = 0;
dust::Voice voice;
dust::Shared shared;
const unsigned frames_ = 32768;
opxy::i32 plain[frames_], with[frames_];

unsigned play(opxy::u32 key, const opxy::i32* pads, opxy::i32* out, double& peak) {
    opxy::u8 model, raw[8];
    dust::drum_defaults(key, &model, raw);
    opxy::i32 values[8];
    for (int c = 0; c < 8; ++c) values[c] = raw[c] == 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c]) << 24;
    std::memset(&voice, 0, sizeof voice);
    shared = dust::Shared();
    dust::initialize(voice, shared, 1);
    dust::start(voice, shared, 1);
    opxy::u32 pitch[64] = {};
    peak = 0;
    unsigned blocks = 0;
    for (; blocks < 44100 * 20 / 64; ++blocks) {
        dust::drum_select(voice, shared, 1, model);
        if (pads) dust::drum_pad(voice, shared, 1, pads);
        opxy::i32 block[64];
        dust::render(voice, shared, 1, block, values, pitch);
        for (unsigned i = 0; i < 64; ++i) {
            const double a = std::fabs(block[i] / 2147483648.0);
            if (a > peak) peak = a;
            const unsigned at = blocks * 64 + i;
            if (out && at < frames_) out[at] = block[i];
        }
        if (dust::finished(voice, shared)) { ++blocks; break; }
    }
    return blocks;
}
// The frequency where a sound's first 8192 samples are strongest, by a DFT
// over 40..800 Hz in 1 Hz steps.
double strongest(const opxy::i32* x) {
    double best = 0, at = 0;
    for (double f = 40; f < 800; f += 1) {
        double re = 0, im = 0;
        for (unsigned i = 0; i < 8192; i += 2) {
            const double w = 2 * 3.141592653589793 * f * i / 44100;
            re += x[i] * std::cos(w);
            im += x[i] * std::sin(w);
        }
        const double p = re * re + im * im;
        if (p > best) { best = p; at = f; }
    }
    return at;
}
// How far a sound's top stands over its bottom in its first 2048 samples
// (dB): 3 to 12 kHz against 150 to 800 Hz, by a Hann-windowed FFT.
void fft(double* re, double* im, unsigned n) {
    for (unsigned i = 1, j = 0; i < n; ++i) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (unsigned len = 2; len <= n; len <<= 1) {
        const double a = -2 * 3.141592653589793 / len;
        for (unsigned i = 0; i < n; i += len)
            for (unsigned k = 0; k < len / 2; ++k) {
                const double c = std::cos(a * k), s = std::sin(a * k);
                const double xr = re[i + k + len / 2] * c - im[i + k + len / 2] * s, xi = re[i + k + len / 2] * s + im[i + k + len / 2] * c;
                re[i + k + len / 2] = re[i + k] - xr;
                im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
    }
}
double tilt(const opxy::i32* x) {
    static double re[2048], im[2048];
    for (unsigned i = 0; i < 2048; ++i) {
        re[i] = x[i] / 2147483648.0 * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / 2048));
        im[i] = 0;
    }
    fft(re, im, 2048);
    double lo = 0, hi = 0;
    for (unsigned k = 1; k < 1024; ++k) {
        const double hz = k * 44100.0 / 2048, p = re[k] * re[k] + im[k] * im[k];
        if (hz >= 150 && hz < 800) lo += p;
        if (hz >= 3000 && hz < 12000) hi += p;
    }
    return 10 * std::log10((hi + 1e-30) / (lo + 1e-30));
}
void expect(const char* what, const char* shown, const char* wanted) {
    if (std::strcmp(shown, wanted)) { std::printf("FAIL %s reads \"%s\", not \"%s\"\n", what, shown, wanted); ++failures; }
}
}

int main() {
    const opxy::i32 neutral[2] = {0, 0};
    for (opxy::u32 key = 53; key < 77; ++key) {
        double a, b;
        const unsigned n = play(key, nullptr, plain, a), m = play(key, neutral, with, b);
        if (n != m || a != b || std::memcmp(plain, with, sizeof plain)) { std::printf("FAIL key %u differs with its pad controls at 0\n", key); ++failures; }
    }
    // TUNE: +12 doubles a tom's pitch, -12 halves it (key 56, the mid tom)
    {
        double peak;
        play(56, neutral, plain, peak);
        const double f0 = strongest(plain);
        const opxy::i32 up[2] = {12, 0}, down[2] = {-12, 0};
        play(56, up, with, peak);
        const double f1 = strongest(with);
        play(56, down, with, peak);
        const double f2 = strongest(with);
        if (std::fabs(f1 / f0 - 2) > 0.08 || std::fabs(f0 / f2 - 2) > 0.08) {
            std::printf("FAIL TUNE +-12 moves the tom from %.0f Hz to %.0f and %.0f Hz\n", f0, f1, f2); ++failures;
        }
    }
    // TONE: a treble shelf, +7 dB brighter and -8 dB darker: the top stands
    // at least 3 dB higher and lower against the bottom on every key, under
    // the ceiling, and every key still ends
    for (opxy::u32 key = 53; key < 77; ++key) {
        double p0, p1, p2;
        play(key, neutral, plain, p0);
        const double t0 = tilt(plain);
        const opxy::i32 hot[2] = {0, 7}, dark[2] = {0, -8};
        const unsigned blocks = play(key, hot, with, p1);
        const double t1 = tilt(with);
        play(key, dark, with, p2);
        const double t2 = tilt(with);
        if (p1 > 0.5 || p2 > 0.5) { std::printf("FAIL key %u passes the ceiling with TONE\n", key); ++failures; }
        if (blocks >= 44100 * 20 / 64) { std::printf("FAIL key %u never ends at TONE +7\n", key); ++failures; }
        if (!(t1 > t0 + 3 && t2 < t0 - 3)) { std::printf("FAIL key %u: TONE tilts it only from %.1f dB to %.1f and %.1f\n", key, t0, t1, t2); ++failures; }
    }
    // values arrive clamped to their fields
    const opxy::i32 wild[2] = {99, 99}, under[2] = {-99, -9};
    dust::drum_pad(voice, shared, 1, wild);
    if (voice.tune != 15 || voice.tone != 7) { std::printf("FAIL pad values unclamped\n"); ++failures; }
    dust::drum_pad(voice, shared, 1, under);
    if (voice.tune != -16 || voice.tone != -8) { std::printf("FAIL pad values unclamped\n"); ++failures; }
    // captions
    char text[16];
    dust::pad_control_text(0, 0, text); expect("TUNE 0", text, "0");
    dust::pad_control_text(0, 5, text); expect("TUNE 5", text, "+5");
    dust::pad_control_text(0, -16, text); expect("TUNE -16", text, "-16");
    dust::pad_control_text(1, 0, text); expect("TONE 0", text, "0dB");
    dust::pad_control_text(1, 7, text); expect("TONE 7", text, "+7dB");
    dust::pad_control_text(1, -8, text); expect("TONE -8", text, "-8dB");
    if (dust::pad_control_text(2, 0, text)) { std::printf("FAIL a third pad control has a caption\n"); ++failures; }
    // a finished voice renders silence, and its next note sounds again
    {
        double peak;
        play(53, neutral, nullptr, peak);
        opxy::i32 block[64], values[8] = {};
        opxy::u32 pitch[64] = {};
        dust::render(voice, shared, 1, block, values, pitch);
        for (unsigned i = 0; i < 64; ++i) if (block[i]) { std::printf("FAIL a finished voice sounds\n"); ++failures; break; }
        dust::start(voice, shared, 1);
        dust::render(voice, shared, 1, block, values, pitch);
        dust::render(voice, shared, 1, block, values, pitch);
        if (dust::finished(voice, shared)) { std::printf("FAIL a new note on a finished voice is silent\n"); ++failures; }
    }
    return failures ? 1 : 0;
}
