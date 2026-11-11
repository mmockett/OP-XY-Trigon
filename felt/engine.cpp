#include "core.h"
#include "levels.h"

// FELT's engine: the kit's callbacks, and a voice's note from its strike to
// silence. The sound itself is in models.h and voice.h.
namespace felt {
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls) {
    const u32 k = key >= 53 && key < 53 + kit::keys ? key - 53 : 0;
    *model = kit::key_model[k];
    for (u32 c = 0; c < 8; ++c) controls[c] = kit::key_values[k][c];
}
const char* model_name(u32 model) { return kit::model[model < kit::models ? model : 0].name; }
const char* parameter_name(u32 model, u32 slot) { return slot < 8 ? kit::model[model < kit::models ? model : 0].labels[slot] : ""; }
u32 parameter_count(u32) { return 8; }

// The ranges the track menu's scramble draws each model's controls from
// (the SDK's scramble_range), so whatever it rolls stays playable: TUNE
// within about an octave around the model's default (no kick under 39 Hz),
// DECAY off its shortest, PEDAL's halo and AGE's wear to half their travel,
// a bolt's or bell's LOOSE and a pluck's BUZZ short of a constant rattle,
// and the few ends the level trims (levels.h) work hardest to lift (a soft
// HAMMER or a bare PAPER on the snare, a thin chain, a bow's near-silent
// SPOT, a pluck fully muted) left out. BOW draws for at most 2 s and BENDs
// at most half an octave. Every other control keeps its whole travel.
const unsigned char scramble_low[kit::models][8] = {
    {20, 16, 0, 0, 0, 0, 0, 0},       // THUD
    {28, 16, 16, 16, 0, 0, 0, 0},     // RATTLE
    {16, 16, 0, 0, 0, 0, 0, 0},       // TOM
    {16, 0, 0, 0, 0, 0, 0, 0},        // LID
    {16, 16, 16, 0, 0, 0, 0, 0},      // TACK
    {16, 16, 16, 0, 0, 0, 0, 0},      // BELL
    {16, 8, 0, 0, 0, 0, 0, 0},        // BRUSH
    {8, 0, 0, 16, 0, 0, 0, 0},        // PLANK
    {16, 8, 16, 8, 0, 0, 0, 0},       // CHAIN
    {8, 16, 0, 0, 0, 0, 0, 0},        // BOLT
    {8, 8, 0, 0, 0, 0, 0, 0},         // PLUCK
    {16, 0, 8, 16, 32, 16, 0, 0},     // BOW
};
const unsigned char scramble_high[kit::models][8] = {
    {88, 127, 127, 127, 127, 127, 64, 64},     // THUD: 39..75 Hz
    {96, 127, 127, 127, 127, 127, 64, 64},     // RATTLE: 162..275 Hz
    {112, 127, 127, 127, 127, 127, 64, 64},    // TOM: 82..337 Hz
    {112, 127, 127, 127, 127, 127, 64, 64},    // LID
    {120, 127, 127, 127, 127, 127, 64, 64},    // TACK
    {112, 127, 127, 127, 127, 72, 64, 64},     // BELL
    {112, 127, 127, 127, 112, 127, 64, 64},    // BRUSH
    {112, 127, 127, 127, 127, 127, 64, 64},    // PLANK
    {112, 127, 127, 127, 127, 127, 64, 64},    // CHAIN
    {96, 127, 127, 127, 127, 72, 64, 64},      // BOLT: 63..292 Hz
    {112, 127, 127, 127, 100, 72, 64, 64},     // PLUCK: 47..258 Hz
    {112, 127, 127, 100, 96, 127, 64, 64},     // BOW
};
void scramble_range(u32 model, u32 slot, opxy::u8* low, opxy::u8* high) {
    if (model >= kit::models || slot >= 8) return;
    *low = scramble_low[model][slot];
    *high = scramble_high[model][slot];
}

inline void initialize(Voice& voice, Shared&, u32 owner) {
    voice.owner = owner;
    voice.model = voice.next = 0;
    voice.pending = 0;
    voice.done = 1;
    voice.quiet = 0;
    voice.tune = voice.soft = 0;
}
inline void start(Voice& voice, Shared&, u32) { voice.pending = 1; }
inline void destroy(Voice&, Shared&, u32) {}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }

// The key's pad controls: TUNE, -16..15 semitones; SOFT, the una corda, 0..15.
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.tune = clamp(values[0], -16, 15);
    voice.soft = clamp(values[1], 0, 15);
}
inline void copy_text(char* text, const char* from) {
    u32 n = 0;
    for (; from[n] && n < 15; ++n) text[n] = from[n];
    text[n] = 0;
}
// Their captions: TUNE in semitones ("+3", "-12", "0"), SOFT "OFF" or 1..15.
inline bool pad_control_text(u32 field, i32 value, char* text) {
    char s[8];
    u32 n = 0;
    if (field == 0) {
        value = clamp(value, -16, 15);
        if (value > 0) s[n++] = '+';
        if (value < 0) { s[n++] = '-'; value = -value; }
    } else if (field == 1) {
        value = clamp(value, 0, 15);
        if (!value) { copy_text(text, "OFF"); return true; }
    } else {
        return false;
    }
    if (value >= 10) s[n++] = static_cast<char>('0' + value / 10);
    s[n++] = static_cast<char>('0' + value % 10);
    s[n] = 0;
    copy_text(text, s);
    return true;
}

// A note's sound has ended once its output has stayed 72 dB under the
// ceiling for 23 ms with nothing still to come (a bounce, a shake, a bow
// still drawn).
// Its voice then writes silence without running the string, and finished
// tells the SDK it may free the voice.
const i32 quiet_under = 1 << 18;
const u32 quiet_blocks = 16;
inline bool finished(const Voice& voice, const Shared&) { return voice.done != 0; }

// A control word (raw << 24) as a Q16 unit.
inline i32 unit7(i32 word) {
    const u32 raw = static_cast<u32>(word < 0 ? 0 : word) >> 24;
    return static_cast<i32>(((raw > 127 ? 127 : raw) * 528416u) >> 10);
}

// A soft knee over the last quarter under the ceiling: what the level trim
// leaves over passes as K + (H - K)(2e - e^2), e the excess over 2 (H - K).
inline i32 knee(i32 y) {
    const i32 k = half_scale - (half_scale >> 2);
    const i32 a = absolute(y);
    if (a <= k) return y;
    const i32 e = a - k >= (half_scale >> 1) ? one : (a - k) * 4;   // Q31
    const i32 b = k + mul(half_scale >> 2, e + mul(e, one - e));
    return y < 0 ? -b : b;
}

// A transient limiter, on the models levels::limit names: a block's peak
// over 0.8 of the ceiling pulls the gain down to it, ramped in over the
// samples before the first one over, and the gain comes back with a 5 ms
// time constant, so it shapes a felt's first knock and leaves the note.
// It watches the peaks between the samples too, where bright noise (paper,
// beads, a slap) swings past the samples' own: each midpoint estimated by
// the four samples around it, (9 (b + c) - (a + d)) / 16. All in halves, so
// the sums stay in range. No divide: the gain is 2^(log2 T - log2 peak).
// CHAIN's beads, bright noise whose peaks between samples stand furthest
// over the samples', are held at 0.65 of the ceiling, so its true peak keeps
// clear of -1 dB on every hit.
const i32 limit_top = half_scale - (half_scale / 5);
const i32 limit_log = L(0.8 * 1073741824.0);
const i32 limit_top_beads = static_cast<i32>(0.65 * 1073741824.0);
const i32 limit_log_beads = L(0.65 * 1073741824.0);
const i32 limit_back = Q31(0.2516);   // 1 - exp(-64 / (0.005 * 44100)) a block
// most: the block's samples' magnitudes ORed together, at least the largest.
// A sample's half is at most half of it, and a midpoint's estimate at most
// (9 + 9 + 1 + 1) / 32 of it, so under 0.8 of the top neither can pass half
// the top, and the block needs no search.
inline void limit(Voice& voice, i32* y, i32 most) {
    const bool beads = voice.model == M::CHAIN;
    const i32 top = beads ? limit_top_beads : limit_top;
    const i32 half_top = top / 2;
    i32 peak = 0;     // halves
    u32 first = frames;
    if (most > (top / 5) * 4) {
        for (unsigned i = 0; i < frames; ++i) {
            const i32 a = absolute(y[i] / 2);
            if (a > peak) peak = a;
            if (a > half_top && first == frames) first = i;
        }
        // each sample over 32 once, carried along the block
        i32 a = y[0] / 32, b = y[1] / 32, c = y[2] / 32;
        for (unsigned i = 1; i + 2 < frames; ++i) {
            const i32 d = y[i + 2] / 32;
            const i32 m = absolute((b + c) * 9 - (a + d));   // halves
            if (m > peak) peak = m;
            if (m > half_top && first > i) first = i;
            a = b;
            b = c;
            c = d;
        }
    }
    i32 g = voice.lim;
    i32 target = g >= one ? one : g + mul(one - g, limit_back);
    if (one - target < (one >> 12)) target = one;
    if (peak > half_top) {
        const i32 need = frac2((beads ? limit_log_beads : limit_log) - octave - log2q(static_cast<u32>(peak)));
        if (need < target) target = need;
    }
    if (target == one && g == one) return;
    if (target < g) {
        u32 k = 0;
        while ((2u << k) <= first + 1) ++k;
        const i32 step = (target - g) >> k;
        const unsigned n = 1u << k;
        for (unsigned i = 0; i < n; ++i) { g += step; y[i] = mul(y[i], g); }
        for (unsigned i = n; i < frames; ++i) y[i] = mul(y[i], target);
    } else {
        const i32 step = (target - g) >> 6;
        for (unsigned i = 0; i < frames; ++i) { g += step; y[i] = mul(y[i], g); }
    }
    voice.lim = target;
}

// The level trims of a control at x (Q16, 0..4 across its five points).
inline void trims(const signed char* t, const signed char* r, i32 x, i32& trim, i32& rise) {
    const u32 i = static_cast<u32>(x >> 16) < 3 ? static_cast<u32>(x >> 16) : 3;
    const i32 f = x - static_cast<i32>(i << 16);
    trim += t[i] * 2048 + mul16((t[i + 1] - t[i]) * 2048, f);
    rise += r[i] * 2048 + mul16((r[i + 1] - r[i]) * 2048, f);
}

inline void begin(Voice& voice, Shared& shared, opxy::Parameters parameters) {
    // Clear everything but the voice's identity and its pad controls, word by word.
    const u32 owner = voice.owner, model = voice.next;
    const i32 tune = voice.tune, soft = voice.soft;
    u32* words = reinterpret_cast<u32*>(&voice);
    const u32 count = static_cast<u32>(sizeof(Voice) / 4);
    for (u32 i = 0; i < count; ++i) words[i] = 0;
    voice.owner = owner;
    voice.model = voice.next = model;
    voice.tune = tune;
    voice.soft = soft;
    voice.seed = (++shared.notes) * 2654435761u + owner * 40503u;
    if (!voice.seed) voice.seed = 0x9e3779b9u;
    i32 c[8];
    for (u32 i = 0; i < 8; ++i) c[i] = unit7(parameters[i]);
    setup_model(voice, model, c, tune);
    voice.lim = one;
    // The level (levels.h, tools/calibrate.py): the model's gain brings its
    // default hit to its role's loudness; each control's trim (and SOFT's)
    // holds that loudness as it turns, and its rise tracks how far that lifts
    // the hit's peak. Where the rises together pass the room the default
    // leaves under the ceiling, the hit comes down by the excess.
    i32 trim = levels::gain[model], rise = 0;
    for (u32 s = 0; s < 8; ++s) trims(levels::loud[model][s], levels::rise[model][s], c[s] * 4, trim, rise);
    trims(levels::loud[model][8], levels::rise[model][8], soft * 17476, trim, rise);
    if (rise > levels::room[model]) trim -= rise - levels::room[model];   // level: cap
    voice.gain = static_cast<i32>(pow2(clamp(trim + 25 * octave, 0, 31 * octave - 1)));   // Q31 of 64
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    if (voice.pending) begin(voice, shared, parameters);
    if (voice.done) {
        for (unsigned i = 0; i < frames; ++i) output[i] = 0;
        return;
    }
    i32 x[frames];
    block_model(voice, x);
    voice.t += frames;
    // the model's gain, the transient limiter, then the soft knee if anything
    // reaches it; and whether anything was loud enough to keep the note going
    // every note ends within 13 s, whatever its settings: past 12 s it fades
    if (voice.t > 12u * 44100u) voice.gain = mul(voice.gain, Q31(0.8));
    const i32 gain = voice.gain;
    i32 most = 0;
    for (unsigned i = 0; i < frames; ++i) {
        const i32 y = clamp(mul(x[i], gain), -(one >> 6), one >> 6) * 64;
        output[i] = y;
        most |= magnitude(y);
    }
    if (levels::limit[voice.model]) limit(voice, output, most);   // level: limiter
    // (tools/calibrate.py builds a copy without the lines marked "level:", to
    // measure the peaks the cap, the limiter and the knee take)
    if (most > half_scale - (half_scale >> 2)) for (unsigned i = 0; i < frames; ++i) output[i] = knee(output[i]);   // level: knee
    const bool loud = most >= quiet_under;
    voice.quiet = loud ? 0 : voice.quiet + 1;
    if (voice.quiet >= quiet_blocks && !voice.waiting) voice.done = 1;
}
}
