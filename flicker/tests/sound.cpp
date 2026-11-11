// FLICKER's sound checks, run by sdk.py test: every key's default sound
// sounds, stays under the SDK's half-scale ceiling and ends; every model with
// each control at five settings stays bounded and ends; each control moves
// the sound across its whole travel; what the track menu's scramble rolls
// stays playable; the kit's names fit. The pad controls are tests/pads.cpp's.
// The full measurement of each control (spectrum and envelope, step by step)
// is tools/sweep.py; this is its guard.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {
int failures = 0;
flicker::Voice voice;
flicker::Shared shared;
const unsigned most = 44100 * 3;   // three seconds at most a check
float buffer[2][44100 * 3];

void fail(const char* what, unsigned m, int a, int b) {
    std::printf("FAIL %s (model %s, %d, %d)\n", what, m < 12 ? flicker::kit::model[m].name : "?", a, b);
    ++failures;
}

// One note of model m with raw controls c and pad values; returns the frames
// rendered, the peak (of full scale) and whether it ended.
unsigned play(unsigned m, const int* c, const int* pads, float* out, double& peak, bool& ended, unsigned limit = most) {
    std::memset(&voice, 0, sizeof voice);
    shared = flicker::Shared();
    flicker::initialize(voice, shared, 1);
    flicker::start(voice, shared, 1);
    opxy::i32 params[8];
    for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
    opxy::u32 pitch[64] = {};
    opxy::i32 block[64];
    peak = 0;
    ended = false;
    unsigned n = 0;
    while (n + 64 <= limit) {
        flicker::drum_select(voice, shared, 1, m);
        if (pads) flicker::drum_pad(voice, shared, 1, pads);
        flicker::render(voice, shared, 1, block, params, pitch);
        for (unsigned i = 0; i < 64; ++i) {
            const double v = block[i] / 2147483648.0;
            if (std::fabs(v) > peak) peak = std::fabs(v);
            if (out) out[n + i] = static_cast<float>(v);
        }
        n += 64;
        if (flicker::finished(voice, shared)) { ended = true; break; }
    }
    if (out) for (unsigned i = n; i < limit; ++i) out[i] = 0;
    return n;
}

// A sound's features: its spectrum in 24 bands from 30 Hz to 16 kHz (dB) and
// its strongest partial's pitch (semitones), in 1024-sample frames 512 apart
// over its first 1.5 s; and the same in 256-sample frames 128 apart over its
// first 290 ms from just before it starts, its attack, where a click, a
// slap, a snap or the projector's flicker lives.
const unsigned frames_ = 129, bands = 24, attack_frames = 100;
struct Features {
    double band[frames_][bands]; double pitch[frames_]; double level[frames_];
    double abands[attack_frames][bands]; double alevel[attack_frames];
};
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
void features(const float* x, unsigned n, Features& f) {
    static double re[1024], im[1024];
    double edge[bands + 1];
    for (unsigned b = 0; b <= bands; ++b) edge[b] = 30 * std::pow(16000.0 / 30, b / double(bands));
    for (unsigned fr = 0; fr < frames_; ++fr) {
        for (unsigned i = 0; i < 1024; ++i) {
            const unsigned at = fr * 512 + i;
            re[i] = (at < n ? x[at] : 0) * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / 1024));
            im[i] = 0;
        }
        fft(re, im, 1024);
        double e[bands] = {}, best = 0;
        unsigned top = 1;
        for (unsigned k = 1; k < 512; ++k) {
            const double p = re[k] * re[k] + im[k] * im[k], hz = k * 44100.0 / 1024;
            for (unsigned b = 0; b < bands; ++b) if (hz >= edge[b] && hz < edge[b + 1]) e[b] += p;
            if (p > best) { best = p; top = k; }
        }
        double total = 0;
        for (unsigned b = 0; b < bands; ++b) { f.band[fr][b] = 10 * std::log10(e[b] + 1e-14); total += e[b]; }
        f.level[fr] = 10 * std::log10(total + 1e-14);
        f.pitch[fr] = 12 * std::log2(top * 44100.0 / 1024 / 440);
    }
    for (unsigned fr = 0; fr < attack_frames; ++fr) {
        for (unsigned i = 0; i < 256; ++i) {
            const int at = static_cast<int>(fr * 128 + i) - 160;   // the first frame centred on the onset
            re[i] = (at >= 0 && at < static_cast<int>(n) ? x[at] : 0) * (0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / 256));
            im[i] = 0;
        }
        fft(re, im, 256);
        double e[bands] = {}, total = 0;
        for (unsigned k = 1; k < 128; ++k) {
            const double p = re[k] * re[k] + im[k] * im[k], hz = k * 44100.0 / 256;
            for (unsigned b = 0; b < bands; ++b) if (hz >= edge[b] && hz < edge[b + 1]) e[b] += p;
        }
        for (unsigned b = 0; b < bands; ++b) { f.abands[fr][b] = 10 * std::log10(e[b] + 1e-14); total += e[b]; }
        f.alevel[fr] = 10 * std::log10(total + 1e-14);
    }
}
// How far apart two sounds are: the mean change of their band levels where
// either sounds (within 45 dB of its loudest), plus the mean change of their
// pitch (semitones, at most 6) where both sound; over the whole sound or over
// the three frames of its attack that move most, whichever moves more (the
// attack's change counted at a quarter).
double apart(const Features& f, const Features& g) {
    double top = -1e9, loud = -1e9;
    for (unsigned i = 0; i < frames_; ++i) {
        loud = std::fmax(loud, std::fmax(f.level[i], g.level[i]));
        for (unsigned b = 0; b < bands; ++b) top = std::fmax(top, std::fmax(f.band[i][b], g.band[i][b]));
    }
    double sum = 0, pitch = 0;
    int count = 0, both = 0;
    for (unsigned i = 0; i < frames_; ++i) {
        for (unsigned b = 0; b < bands; ++b) {
            if (f.band[i][b] < top - 45 && g.band[i][b] < top - 45) continue;
            sum += std::fabs(std::fmax(f.band[i][b], top - 45) - std::fmax(g.band[i][b], top - 45));
            ++count;
        }
        if (f.level[i] > loud - 30 && g.level[i] > loud - 30) { pitch += std::fabs(f.pitch[i] - g.pitch[i]); ++both; }
    }
    const double whole = (count ? sum / count : 0) + (both ? std::fmin(6.0, 2 * pitch / both) : 0);
    // the attack: the three frames of it that move most, each by its mean band change
    double atop = -1e9, worst[3] = {0, 0, 0};
    for (unsigned i = 0; i < attack_frames; ++i)
        for (unsigned b = 0; b < bands; ++b) atop = std::fmax(atop, std::fmax(f.abands[i][b], g.abands[i][b]));
    for (unsigned i = 0; i < attack_frames; ++i) {
        double asum = 0;
        int acount = 0;
        for (unsigned b = 0; b < bands; ++b) {
            if (f.abands[i][b] < atop - 40 && g.abands[i][b] < atop - 40) continue;
            asum += std::fabs(std::fmax(f.abands[i][b], atop - 40) - std::fmax(g.abands[i][b], atop - 40));
            ++acount;
        }
        double m = acount ? asum / acount : 0;
        for (int k = 0; k < 3; ++k) if (m > worst[k]) std::swap(m, worst[k]);
    }
    const double attack = 0.25 * (worst[0] + worst[1] + worst[2]) / 3;
    return std::fmax(whole, attack);
}
}

int main() {
    // The kit's names and controls: every model names all eight, in at most
    // eight characters; every key holds a model.
    for (unsigned m = 0; m < flicker::kit::models; ++m) {
        if (flicker::parameter_count(m) != 8) fail("not eight controls", m, 0, 0);
        for (unsigned s = 0; s < 8; ++s) {
            const char* l = flicker::parameter_name(m, s);
            if (!l[0] || std::strlen(l) > 8) fail("a control without a name, or too long a one", m, static_cast<int>(s), 0);
        }
    }
    for (unsigned key = 53; key < 77; ++key) {
        opxy::u8 model, raw[8];
        flicker::drum_defaults(key, &model, raw);
        if (model >= flicker::kit::models) fail("a key without a model", model, static_cast<int>(key), 0);
        for (int i = 0; i < 8; ++i) if (raw[i] > 127) fail("a key's control out of range", model, static_cast<int>(key), i);
        // Its sound: loud enough, under the ceiling, and it ends.
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = raw[i];
        double peak;
        bool ended;
        play(model, c, nullptr, nullptr, peak, ended, 44100 * 8);
        if (peak > 0.5) fail("a key passes the half-scale ceiling", model, static_cast<int>(key), static_cast<int>(peak * 1000));
        // levels are set by loudness (tools/calibrate.py)
        if (peak < 0.07) fail("a key too quiet", model, static_cast<int>(key), static_cast<int>(peak * 1000));
        if (!ended) fail("a key never ends", model, static_cast<int>(key), 0);
    }
    // Every model, each control at 0, 32, 64, 96 and 127: bounded, sounding,
    // ending; and each step moves the sound.
    static const int steps[5] = {0, 32, 64, 96, 127};
    static Features f[5];
    unsigned weak = 0;
    for (unsigned m = 0; m < flicker::kit::models; ++m) {
        for (unsigned s = 0; s < 8; ++s) {
            for (int k = 0; k < 5; ++k) {
                int c[8];
                for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[m].defaults[i];
                c[s] = steps[k];
                double peak;
                bool ended;
                const unsigned n = play(m, c, nullptr, buffer[0], peak, ended, 44100 * 2);
                if (peak > 0.5) fail("passes the ceiling", m, static_cast<int>(s), steps[k]);
                if (peak < 0.025) fail("too quiet", m, static_cast<int>(s), steps[k]);   // a dense wash peaks low at its loudness
                features(buffer[0], n, f[k]);
            }
            for (int k = 0; k < 4; ++k) {
                const double d = apart(f[k], f[k + 1]);
                // a quarter of the travel moves the sound at least 1.2 dB on average
                if (d < 1.2) {
                    std::printf("weak: %s %s from %d to %d moves %.2f dB\n", flicker::kit::model[m].name,
                                flicker::kit::model[m].labels[s], steps[k], steps[k + 1], d);
                    ++weak;
                }
            }
            // and from end to end, clearly
            const double ends = apart(f[0], f[4]);
            if (ends < 3.0) fail("a control barely changes the sound end to end (tenths of a dB)", m, static_cast<int>(s), static_cast<int>(ends * 10));
        }
    }
    if (weak > 8) fail("too many weak steps", 0, static_cast<int>(weak), 0);
    // Every model ends at its heaviest settings too.
    for (unsigned m = 0; m < flicker::kit::models; ++m) {
        for (int v = 0; v <= 127; v += 127) {
            int c[8];
            for (int i = 0; i < 8; ++i) c[i] = v;
            double peak;
            bool ended;
            play(m, c, nullptr, nullptr, peak, ended, 44100 * 15);
            if (peak > 0.5) fail("passes the ceiling with every control at an end", m, v, 0);
            if (!ended) fail("never ends with every control at an end", m, v, 0);
        }
    }
    // The track menu's scramble: every model with its controls at the low
    // ends of their scramble ranges, at the high ends, and drawn at random
    // from them as the SDK draws them, sounds, stays under the ceiling and
    // ends within 14 s.
    unsigned seed = 0x2545f491u;
    for (unsigned m = 0; m < flicker::kit::models; ++m) {
        for (int roll = 0; roll < 26; ++roll) {
            int c[8];
            for (unsigned s = 0; s < 8; ++s) {
                opxy::u8 low = 0, high = 127;
                flicker::scramble_range(m, s, &low, &high);
                if (low > high || high > 127) fail("a scramble range out of order", m, static_cast<int>(s), 0);
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                const unsigned span = high - low + 1u;
                c[s] = roll == 0 ? low : roll == 1 ? high : low + static_cast<int>(((seed >> 16) * span) >> 16);
            }
            double peak;
            bool ended;
            play(m, c, nullptr, nullptr, peak, ended, 44100 * 14);
            if (peak > 0.5) fail("a scramble passes the ceiling", m, roll, static_cast<int>(peak * 1000));
            if (peak < 0.01) fail("a scramble too quiet", m, roll, static_cast<int>(peak * 1000));
            if (!ended) fail("a scramble never ends", m, roll, 0);
        }
    }
    std::printf("%u weak steps of %u\n", weak, flicker::kit::models * 8 * 4);
    return failures ? 1 : 0;
}
