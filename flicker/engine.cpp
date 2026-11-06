#include "core.h"
#include "levels.h"

// FLICKER's engine: the kit's callbacks, the projector every drawing passes
// through, and a voice's note from start to silence. The drawings themselves
// are in models.h.
namespace flicker {
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls) {
    const u32 k = key >= 53 && key < 53 + kit::keys ? key - 53 : 0;
    *model = kit::key_model[k];
    for (u32 c = 0; c < 8; ++c) controls[c] = kit::key_values[k][c];
}
const char* model_name(u32 model) { return kit::model[model < kit::models ? model : 0].name; }
const char* parameter_name(u32 model, u32 slot) { return slot < 8 ? kit::model[model < kit::models ? model : 0].labels[slot] : ""; }
u32 parameter_count(u32) { return 8; }

inline void initialize(Voice& voice, Shared&, u32 owner) {
    voice.owner = owner;
    voice.model = voice.next = 0;
    voice.pending = 0;
    voice.done = 1;
    voice.quiet = 0;
    voice.tune = voice.slit = 0;
}
inline void start(Voice& voice, Shared&, u32) { voice.pending = 1; }
inline void destroy(Voice&, Shared&, u32) {}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }

// The key's pad controls: TUNE, -16..15 semitones; SLIT, -8..7 (narrower and
// brighter below 0, wider and duller above).
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.tune = clamp(values[0], -16, 15);
    voice.slit = clamp(values[1], -8, 7);
}
inline void copy_text(char* text, const char* from) {
    u32 n = 0;
    for (; from[n] && n < 15; ++n) text[n] = from[n];
    text[n] = 0;
}
// Their captions, signed whole numbers: "+3", "-12", "0".
inline bool pad_control_text(u32 field, i32 value, char* text) {
    char s[8];
    u32 n = 0;
    if (field == 0) value = clamp(value, -16, 15);
    else if (field == 1) value = clamp(value, -8, 7);
    else return false;
    if (value > 0) s[n++] = '+';
    if (value < 0) { s[n++] = '-'; value = -value; }
    if (value >= 10) s[n++] = static_cast<char>('0' + value / 10);
    s[n++] = static_cast<char>('0' + value % 10);
    s[n] = 0;
    copy_text(text, s);
    return true;
}

// A note's sound has ended once its output has stayed 72 dB under the
// ceiling for 23 ms with nothing still to come (a stroke, an echo, a claw's
// tick). Its
// voice then writes silence without drawing, and finished tells the SDK it
// may free the voice.
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
    const i32 a = y < 0 ? (y < -one ? one : -y) : y;
    if (a <= k) return y;
    const i32 e = a - k >= (half_scale >> 1) ? one : (a - k) * 4;   // Q31
    const i32 b = k + multiply(half_scale >> 2, e + multiply(e, one - e));
    return y < 0 ? -b : b;
}

// A transient limiter, on the models levels::limit names: a block's peak over
// 0.8 of the ceiling pulls the gain down to it, ramped in over the samples
// before the first one over, and the gain comes back with a 5 ms time
// constant, so it shapes the transient and leaves the body. No divide: the
// gain is 2^(log2 T - log2 peak).
const i32 limit_top = half_scale - (half_scale / 5);
const i32 limit_log = L(0.8 * 1073741824.0);
const i32 limit_back = Q31(0.2516);   // 1 - exp(-64 / (0.005 * 44100)) a block
FLICKER_APART void limit(Voice& voice, i32* y) {
    i32 peak = 0;
    for (unsigned i = 0; i < frames; ++i) {
        const i32 a = absolute(y[i]);
        if (a > peak) peak = a;
    }
    i32 g = voice.lim;
    i32 target = g >= one ? one : g + multiply(one - g, limit_back);
    if (one - target < (one >> 12)) target = one;
    if (peak > limit_top) {
        const i32 need = frac2(limit_log - log2q(static_cast<u32>(peak)));
        if (need < target) target = need;
    }
    if (target == one && g == one) return;
    if (target < g) {
        u32 first = 0;
        while (absolute(y[first]) <= limit_top) ++first;
        u32 k = 0;
        while ((2u << k) <= first + 1) ++k;
        const i32 step = (target - g) >> k;
        const unsigned n = 1u << k;
        for (unsigned i = 0; i < n; ++i) { g += step; y[i] = multiply(y[i], g); }
        for (unsigned i = n; i < frames; ++i) y[i] = multiply(y[i], target);
    } else {
        const i32 step = (target - g) / 64;
        for (unsigned i = 0; i < frames; ++i) { g += step; y[i] = multiply(y[i], g); }
    }
    voice.lim = target;
}

// A key's own hash: the dirt on its film is the same every time it plays.
inline u32 key_hash(u32 model, const i32* word) {
    u32 h = 2166136261u ^ model;
    for (u32 i = 0; i < 8; ++i) h = (h ^ (static_cast<u32>(word[i]) >> 24)) * 16777619u;
    return h ? h : 1u;
}

// The projector's settings for a note: INK, FLUTTER, DUST (row 2's last
// three controls) and the key's SLIT.
inline void setup_projector(Voice& v, const i32* c, u32 hash) {
    // INK: the drawing drawn 0.85 to 4.5 times as tall (Q31 of 8)
    v.cut.ink = frac2(lmix(L(0.9), L(7.5), c[5]) - 3 * octave);
    // the cut's span past its knee, 0.8 of the edge falling to 0.1 as the
    // ink grows heavy, from a sixth of its travel to two thirds (the knee is
    // 1 - span / 2)
    const i32 heavy = clamp((c[5] - 10923) * 2, 0, 65536);
    const i32 lspan = lmix(L(0.8), L(0.1), heavy);
    v.cut.span = static_cast<i32>(pow2(lspan + 25 * octave));            // in E / 8
    v.cut.knee = (E / 8) - (v.cut.span >> 1);
    v.cut.curve = frac2(-(lspan + 5 * octave));
    // up to a sixth of its travel the ink bleeds: soft at 400 Hz, crisp by 21
    const i32 u = clamp(c[5] * 6, 0, 65536);
    v.bleed = u >= 65536 ? 0 : onepole_k(lmix(L(400), L(20000), u));
    // SLIT: 11 kHz at 0; narrowed toward 22 kHz, widened down to 2.7 kHz
    const i32 s = v.slit;
    const i32 lslit = L(11000) - (s > 0 ? s * Q16(0.29) : s * Q16(0.125));
    v.slit_k = onepole_k(clamp(lslit, L(1000), L(21000)));
    v.lens_k = onepole_k(L(16000));
    v.cap_k = onepole_k(L(25));
    // the valve warms what is low and round; on a hiss or a wash its even
    // bend would only thump, so those drawings take little of it
    static const i32 warmth[12] = {Q31(0.8), Q31(0.3), Q31(0.8), Q31(0.1), Q31(0.3), Q31(0.5),
                                   Q31(0.04), Q31(0.04), Q31(0.04), Q31(0.6), Q31(0.8), Q31(0.3)};
    v.valve = warmth[v.model < 12 ? v.model : 0];
    // FLUTTER: the claw's 24 Hz in the film's speed and in its light, and
    // the loop's slow wow
    const i32 g = 65536 - c[6];
    const i32 f = clamp(65536 - mul16(sq16(g), g), 0, 65535);   // quick to come in: 1 - (1 - c)^3
    v.fl_depth = mul16(f, Q16(0.012)) * 16384;                      // Q30
    v.wow_depth = mul16(f, Q16(0.05)) * 16384;
    v.am_k = multiply(f * 32768, Q31(0.75));
    v.fl_inc = increment(L(24) + 6 * octave);                      // a block's turn
    v.wow_inc = increment(L(3.1) + 6 * octave);
    v.fl_ph = hash * 2654435761u;
    v.wow_ph = hash * 40503u;
    v.fm = 1 << 30;
    v.am = one;
    // DUST: specks that pop, grain that crackles and a hair, all the key's own
    const i32 dg = 65536 - c[7];
    v.dust = c[7] ? clamp(65536 - mul16(sq16(dg), dg), 0, 65535) * 32768 : 0;   // quick to come in, as FLUTTER
    v.dseed = nonzero_seed(hash);
    v.pop_wait = 90 + static_cast<i32>(hash >> 22);                 // the first speck within 25 ms
    v.pop = 0;
    v.pop_k = decay(L(0.0004));
    v.crackle = static_cast<i32>(pow2(lmix(L(40), L(500), c[7]) - lsr + 31 * octave));   // a crackle's chance a sample
    v.hair_at = 1500 + static_cast<i32>((hash >> 8) & 0x1fffu);    // 34 to 220 ms in
    v.hair_inc = static_cast<u32>(pow2(32 * octave - lsr - L(0.006)));
    v.hair_ph = 0xf0000000u;
    if (!c[6]) v.fl_depth = v.wow_depth = v.am_k = 0;
}

FLICKER_APART void begin(Voice& voice, Shared& shared, opxy::Parameters parameters) {
    // Clear everything but the voice's identity and its pad controls.
    const u32 owner = voice.owner, model = voice.next;
    const i32 tune = voice.tune, slit = voice.slit;
    u32* words = reinterpret_cast<u32*>(&voice);
    for (u32 i = 0; i < sizeof(Voice) / 4; ++i) words[i] = 0;
    voice.owner = owner;
    voice.model = voice.next = model;
    voice.tune = tune;
    voice.slit = slit;
    voice.seed = nonzero_seed((++shared.notes) * 2654435761u + owner * 40503u);
    i32 c[8];
    for (u32 i = 0; i < 8; ++i) c[i] = unit7(parameters[i]);
    const u32 hash = key_hash(model, parameters);
    setup_projector(voice, c, hash);
    setup_model(voice, model, c, tune * semitone);
    voice.lim = one;
    voice.run = one;
    // The level (levels.h, tools/calibrate.py): the model's gain brings its
    // default hit to its role's loudness; each control's trim holds that
    // loudness as it turns, and its rise tracks how far that lifts the hit's
    // peak. Where the rises of all eight controls together pass the room the
    // default leaves under the ceiling, the hit comes down by the excess.
    i32 trim = levels::gain[model], rise = 0;
    for (u32 s = 0; s < 8; ++s) {
        const signed char* t = levels::loud[model][s];
        const signed char* r = levels::rise[model][s];
        const i32 x = c[s] * 8;                       // 0..8 in Q16: the table's nine points
        const u32 i = static_cast<u32>(x >> 16) < 7 ? static_cast<u32>(x >> 16) : 7;
        const i32 f = x - static_cast<i32>(i << 16);
        trim += t[i] * 2048 + mul16((t[i + 1] - t[i]) * 2048, f);
        rise += r[i] * 2048 + mul16((r[i + 1] - r[i]) * 2048, f);
    }
    if (rise > levels::room[model]) trim -= rise - levels::room[model];   // level: cap
    voice.gain = static_cast<i32>(pow2(clamp(trim + 25 * octave, 0, 31 * octave - 1)));   // Q31 of 64
}

// The film's speed and light for a block (FLUTTER).
inline void flutter(Voice& v) {
    if (!v.fl_depth && !v.wow_depth) return;
    v.fl_ph += v.fl_inc;
    v.wow_ph += v.wow_inc;
    const i32 s = sine(v.fl_ph), w = sine(v.wow_ph);
    v.fm = (1 << 30) + multiply(s, v.fl_depth) + multiply(w, v.wow_depth);
    // the light flickers as each frame is pulled down: deepest once a frame
    v.am = one - multiply(v.am_k, (s >> 1) + 0x3fffffff);
}

// DUST: specks pop (dark ones down, scratches through the emulsion up),
// grain crackles, and a hair crosses the track once; all under the drawing's
// height, so the dirt ends with it.
FLICKER_APART void dust(Voice& v, i32* x) {
    // the dirt plays while the drawing does, and for as long as the frame
    // takes to pass the gate (at least 0.18 s, as the screen pulls it)
    const i32 t = static_cast<i32>(v.t);
    const i32 passing = t < 6000 ? one : t < 10000 ? (10000 - t) * 536870 : 0;
    const i32 drawn_ = v.run > (one >> 2) ? one : v.run * 4;
    const i32 level = multiply(v.dust, drawn_ > passing ? drawn_ : passing);
    if (level < 65536) return;
    u32 seed = v.dseed;
    i32 pop = v.pop, wait = v.pop_wait;
    const i32 pk = v.pop_k, chance = v.crackle;
    const i32 crack = multiply(level, QE(0.07));
    for (unsigned i = 0; i < frames; ++i) {
        if (--wait <= 0) {
            const u32 r = rng(seed);
            pop = multiply(level, QE(0.8) + (static_cast<i32>(r >> 9) & 0x3fffff) * 32);
            if (r & 1u) pop = -pop;
            // the next speck: 10 to 330 ms on, sooner as DUST rises
            wait = 441 + static_cast<i32>((r >> 12) & 0x3fffu) - multiply(static_cast<i32>((r >> 12) & 0x3fffu), v.dust >> 1);
        }
        const u32 r = rng(seed);
        // a crackle, up (bit 1 set) or down, when r's top 31 bits fall under its chance
        const i32 on = -static_cast<i32>(static_cast<i32>(r >> 1) < chance);
        const i32 down = static_cast<i32>((r >> 1) & 1u) - 1;   // 0 up, -1 down
        x[i] += pop + (((crack ^ down) - down) & on);
        pop = multiply(pop, pk);
    }
    // the hair: a dark line across the track, a few milliseconds long
    if (static_cast<i32>(v.t) >= v.hair_at && static_cast<i32>(v.t) < v.hair_at + 64) v.hair_ph = 0;
    if (v.hair_ph < 0xf0000000u) {
        const i32 h = multiply(level, QE(0.35));
        u32 p = v.hair_ph;
        for (unsigned i = 0; i < frames; ++i) {
            const u32 q = p;
            p += v.hair_inc;
            if (p < q) { p = 0xf0000000u; break; }
            x[i] -= multiply(bump(q), h);
        }
        v.hair_ph = p;
    }
    v.dseed = seed;
    v.pop = pop;
    v.pop_wait = wait;
}

// The drawing inked past the track's edges and cut off there.
FLICKER_APART void inked(Voice& voice, i32* x) {
    const Cut ink = voice.cut;
    const i32 k = voice.bleed;
    if (k) {
        i32 b = voice.bleed_s;
        for (unsigned i = 0; i < frames; ++i) {
            b += multiply(x[i] - b, k);
            x[i] = ink_cut(b, ink);
        }
        voice.bleed_s = b;
        return;
    }
    for (unsigned i = 0; i < frames; ++i) x[i] = ink_cut(x[i], ink);
}
// The light through the slit and the lens (flickering, by FLUTTER: Am), the
// valve, the coupling capacitor and the model's gain; returns the block's
// loudest sample (or'd), to tell whether the note still sounds. With Ink,
// INK's cut (and a thin ink's soft edge, Bleed) comes first in the same pass.
template <bool Ink, bool Bleed, bool Am> inline i32 project(Voice& voice, i32* x, i32* output) {
    const Cut ink = voice.cut;
    const i32 bk = voice.bleed, am = voice.am;
    const i32 sk = voice.slit_k, lk = voice.lens_k, ck = voice.cap_k, gain = voice.gain, valve = voice.valve;
    i32 b = voice.bleed_s, s1 = voice.slit_s, s2 = voice.lens_s, cap = voice.cap_s;
    i32 most = 0;
    for (unsigned i = 0; i < frames; ++i) {
        i32 v = x[i];
        if (Ink) {
            if (Bleed) {
                b += multiply(v - b, bk);
                v = b;
            }
            v = ink_cut(v, ink);
        }
        if (Am) v = multiply(v, am);
        s1 += multiply(v - s1, sk);
        s2 += multiply(s1 - s2, lk);
        const i32 w = s2 + multiply(multiply(s2, s2), valve) * 2;   // the valve: x + a x^2 (in track edges), a up to 0.25
        cap += multiply(w - cap, ck);
        const i32 y = clamp(multiply(w - cap, gain), -(one >> 6), one >> 6) * 64;
        output[i] = y;
        most |= y < 0 ? -y : y;
    }
    if (Bleed) voice.bleed_s = b;
    voice.slit_s = s1;
    voice.lens_s = s2;
    voice.cap_s = cap;
    return most;
}
template <bool Ink, bool Bleed> inline i32 project_am(Voice& voice, i32* x, i32* output) {
    return voice.am_k ? project<Ink, Bleed, true>(voice, x, output) : project<Ink, Bleed, false>(voice, x, output);
}
// The film after the drawing: inked, then its dirt (DUST, which the ink
// does not cut), then projected. Without dust, the ink and the projection
// share a pass.
FLICKER_APART i32 projected(Voice& voice, i32* x, i32* output) {
    if (voice.dust) {
        inked(voice, x);
        dust(voice, x);
        return project_am<false, false>(voice, x, output);
    }
    return voice.bleed ? project_am<true, true>(voice, x, output) : project_am<true, false>(voice, x, output);
}
// The soft knee over a block.
FLICKER_APART void kneed(i32* output) {
    for (unsigned i = 0; i < frames; ++i) output[i] = knee(output[i]);
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    if (voice.pending) begin(voice, shared, parameters);
    if (voice.done) {
        for (unsigned i = 0; i < frames; ++i) output[i] = 0;
        return;
    }
    flutter(voice);
    i32 x[frames];
    block_model(voice, x);
    const i32 most = projected(voice, x, output);
    voice.t += frames;
    if (levels::limit[voice.model] && (most > limit_top || voice.lim != one)) limit(voice, output);   // level: limiter
    // (tools/calibrate.py builds a copy without the lines marked "level:", to
    // measure the peaks the cap, the limiter and the knee take)
    if (most > half_scale - (half_scale >> 2)) kneed(output);   // level: knee
    const bool loud = most >= quiet_under;
    voice.quiet = loud ? 0 : voice.quiet + 1;
    if (voice.quiet >= quiet_blocks && !voice.waiting) voice.done = 1;
}
}
