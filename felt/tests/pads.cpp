// FELT's pad controls in the voice (the SDK's padControls, drum_pad):
// at 0 a key sounds exactly as without them; TUNE moves a key's pitch by
// semitones; SOFT, the una corda, makes a struck key rounder and darker and
// holds its loudness, and never passes the SDK's ceiling; values arrive
// clamped; their captions read as the header shows them. And a finished voice
// renders silence until its next note.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
int failures = 0;
felt::Voice voice;
felt::Shared shared;
const unsigned frames_ = 32768;
opxy::i32 plain[frames_], with[frames_];

unsigned play(opxy::u32 key, const opxy::i32* pads, opxy::i32* out, double& peak) {
    opxy::u8 model, raw[8];
    felt::drum_defaults(key, &model, raw);
    opxy::i32 values[8];
    for (int c = 0; c < 8; ++c) values[c] = raw[c] == 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c]) << 24;
    std::memset(&voice, 0, sizeof voice);
    shared = felt::Shared();
    felt::initialize(voice, shared, 1);
    felt::start(voice, shared, 1);
    opxy::u32 pitch[64] = {};
    peak = 0;
    unsigned blocks = 0;
    for (; blocks < 44100 * 20 / 64; ++blocks) {
        felt::drum_select(voice, shared, 1, model);
        if (pads) felt::drum_pad(voice, shared, 1, pads);
        opxy::i32 block[64];
        felt::render(voice, shared, 1, block, values, pitch);
        for (unsigned i = 0; i < 64; ++i) {
            const double a = std::fabs(block[i] / 2147483648.0);
            if (a > peak) peak = a;
            const unsigned at = blocks * 64 + i;
            if (out && at < frames_) out[at] = block[i];
        }
        if (felt::finished(voice, shared)) { ++blocks; break; }
    }
    return blocks;
}
// A sound's power spectrum over its first 8192 samples: a Hann-windowed DFT
// in 10 Hz steps from 20 Hz to 12 kHz, fine enough that a bass note's
// fundamental lands on the grid rather than leaking across it.
const unsigned bins = 1198;
void spectrum(const opxy::i32* x, double* p) {
    static double w[8192];
    for (unsigned i = 0; i < 8192; ++i) w[i] = x[i] * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / 8192));
    for (unsigned k = 0; k < bins; ++k) {
        const double f = 20 + 10.0 * k;
        double re = 0, im = 0;
        const double c = std::cos(2 * 3.141592653589793 * f / 44100), s = std::sin(2 * 3.141592653589793 * f / 44100);
        double pr = 1, pi = 0;
        for (unsigned i = 0; i < 8192; ++i) {
            re += w[i] * pr;
            im += w[i] * pi;
            const double nr = pr * c - pi * s;
            pi = pr * s + pi * c;
            pr = nr;
        }
        p[k] = re * re + im * im;
    }
}
// Its centre (Hz), and the share of its power above 1.5 times its strongest
// frequency: what a note keeps over its fundamental.
void brightness(const opxy::i32* x, double& centre, double& upper) {
    static double p[bins];
    spectrum(x, p);
    double num = 0, den = 0, best = 0;
    unsigned at = 0;
    for (unsigned k = 0; k < bins; ++k) {
        num += p[k] * (20 + 10.0 * k);
        den += p[k];
        if (p[k] > best) { best = p[k]; at = k; }
    }
    double over = 0;
    for (unsigned k = 0; k < bins; ++k) if (20 + 10.0 * k > 1.5 * (20 + 10.0 * at)) over += p[k];
    centre = den > 0 ? num / den : 0;
    upper = den > 0 ? over / den : 0;
}
double rms(const opxy::i32* x, unsigned n) {
    double e = 0;
    for (unsigned i = 0; i < n; ++i) { const double v = x[i] / 2147483648.0; e += v * v; }
    return std::sqrt(e / n);
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
    // SOFT: on every key under the ceiling, and every key still ends; on the
    // struck keys darker at 15 (its spectrum's centre lower, or less over its
    // fundamental) with its
    // loudness held within 3 dB
    for (opxy::u32 key = 53; key < 77; ++key) {
        double p0, p1;
        play(key, neutral, plain, p0);
        const opxy::i32 soft[2] = {0, 15};
        const unsigned blocks = play(key, soft, with, p1);
        if (p1 > 0.5) { std::printf("FAIL key %u passes the ceiling at SOFT 15\n", key); ++failures; }
        if (blocks >= 44100 * 20 / 64) { std::printf("FAIL key %u never ends at SOFT 15\n", key); ++failures; }
        opxy::u8 model, raw[8];
        felt::drum_defaults(key, &model, raw);
        const bool struck = model == felt::M::THUD || model == felt::M::RATTLE || model == felt::M::TOM || model == felt::M::TACK ||
                            model == felt::M::BELL || model == felt::M::BOLT || model == felt::M::PLANK || model == felt::M::PLUCK;
        if (!struck) continue;
        double c0, c1, u0, u1;
        brightness(plain, c0, u0);
        brightness(with, c1, u1);
        const double l0 = rms(plain, 17640), l1 = rms(with, 17640);
        const double change = 20 * std::log10(l1 / l0);
        // darker: its centre 8% lower, or (a note that is mostly its
        // fundamental) what it holds over that 6 dB down
        const bool darker = c1 < 0.92 * c0 || u1 < 0.25 * u0;
        if (!darker) { std::printf("FAIL key %u: SOFT 15 barely darkens it (%.0f Hz to %.0f Hz)\n", key, c0, c1); ++failures; }
        if (std::getenv("FELT_VERBOSE")) std::printf("key %u: SOFT centroid %.0f -> %.0f Hz, over the fundamental %.1f dB, level %+.1f dB\n", key, c0, c1, 10 * std::log10((u1 + 1e-12) / (u0 + 1e-12)), change);
        if (change > 3 || change < -3) { std::printf("FAIL key %u: SOFT 15 changes its level %.1f dB\n", key, change); ++failures; }
    }
    // values arrive clamped to their fields
    const opxy::i32 wild[2] = {99, 99}, under[2] = {-99, -9};
    felt::drum_pad(voice, shared, 1, wild);
    if (voice.tune != 15 || voice.soft != 15) { std::printf("FAIL pad values unclamped\n"); ++failures; }
    felt::drum_pad(voice, shared, 1, under);
    if (voice.tune != -16 || voice.soft != 0) { std::printf("FAIL pad values unclamped\n"); ++failures; }
    // captions
    char text[16];
    felt::pad_control_text(0, 0, text); expect("TUNE 0", text, "0");
    felt::pad_control_text(0, 5, text); expect("TUNE 5", text, "+5");
    felt::pad_control_text(0, -16, text); expect("TUNE -16", text, "-16");
    felt::pad_control_text(1, 0, text); expect("SOFT 0", text, "OFF");
    felt::pad_control_text(1, 15, text); expect("SOFT 15", text, "15");
    if (felt::pad_control_text(2, 0, text)) { std::printf("FAIL a third pad control has a caption\n"); ++failures; }
    // a finished voice renders silence, and its next note sounds again
    {
        double peak;
        play(53, neutral, nullptr, peak);
        opxy::i32 block[64], values[8] = {};
        opxy::u32 pitch[64] = {};
        felt::render(voice, shared, 1, block, values, pitch);
        for (unsigned i = 0; i < 64; ++i) if (block[i]) { std::printf("FAIL a finished voice sounds\n"); ++failures; break; }
        felt::start(voice, shared, 1);
        felt::render(voice, shared, 1, block, values, pitch);
        felt::render(voice, shared, 1, block, values, pitch);
        if (felt::finished(voice, shared)) { std::printf("FAIL a new note on a finished voice is silent\n"); ++failures; }
    }
    return failures ? 1 : 0;
}
