#include "core.h"
#include "levels.h"

// DUST's engine: the kit's callbacks, and a voice's note from the needle's
// landing to its lift. The hits are in models.h, their parts in voice.h.
namespace dust {
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
    voice.tune = voice.tone = 0;
}
inline void start(Voice& voice, Shared&, u32) { voice.pending = 1; }
inline void destroy(Voice&, Shared&, u32) {}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }

// The key's pad controls: TUNE, -16..15 semitones; TONE, -8..7 dB of treble.
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.tune = clamp(values[0], -16, 15);
    voice.tone = clamp(values[1], -8, 7);
}
inline void copy_text(char* text, const char* from) {
    u32 n = 0;
    for (; from[n] && n < 15; ++n) text[n] = from[n];
    text[n] = 0;
}
// Their captions: TUNE in semitones ("+3", "-12", "0"), TONE in dB ("+3dB", "-8dB", "0dB").
inline bool pad_control_text(u32 field, i32 value, char* text) {
    char s[8];
    u32 n = 0;
    if (field > 1) return false;
    value = field == 0 ? clamp(value, -16, 15) : clamp(value, -8, 7);
    if (value > 0) s[n++] = '+';
    if (value < 0) { s[n++] = '-'; value = -value; }
    if (value >= 10) s[n++] = static_cast<char>('0' + value / 10);
    s[n++] = static_cast<char>('0' + value % 10);
    if (field == 1) { s[n++] = 'd'; s[n++] = 'B'; }
    s[n] = 0;
    copy_text(text, s);
    return true;
}

// A voice has ended once the needle is lifted and its thump is over, or
// sooner once its output has stayed under -96 dB for 60 ms with nothing
// still to come and no worn surface still playing.
const i32 quiet_under = 1 << 15;
const u32 quiet_blocks = 42;
inline bool finished(const Voice& voice, const Shared&) { return voice.done != 0; }

// A soft knee over the last quarter under the ceiling: what the level trim
// leaves over passes as K + (H - K)(2e - e^2), e the excess over 2 (H - K).
// (no branches: under the knee e is 0 and the sample passes as it came)
inline i32 knee(i32 y) {
    const i32 k = half_scale - (half_scale >> 2);
    const i32 s = y >> 31, a = y ^ s;
    const i32 e = shl(clamp(a - k, 0, (half_scale >> 1) - 1), 2);
    const i32 b = imin(a, k) + multiply(half_scale >> 2, e + multiply(e, one - e));
    return b ^ s;
}

// A transient limiter on the models levels::limit names: a block's peak over
// 0.8 of the ceiling pulls the gain down to it, ramped in over the samples
// before the first one over, and the gain comes back with a 5 ms time
// constant: it shapes a stick's crack and leaves the body.
const i32 limit_top = half_scale - (half_scale / 5);
const i32 limit_log = L(0.8 * 1073741824.0);
const i32 limit_back = Q31(0.2516);   // 1 - exp(-64 / (0.005 * 44100)) a block
// Its gain for this block: where it starts, its step a sample and where it
// stops (a step down ramps over the samples before the first over the
// limit, at least 16 of them, so the gain never jumps; the knee takes what
// passes).
inline void limit_gain(Voice& voice, const i32* y, i32 peak, i32& g, i32& step, i32& target) {
    g = voice.lim;
    target = g >= one ? one : g + multiply(one - g, limit_back);
    if (one - target < (one >> 12)) target = one;
    if (peak > limit_top) {
        const i32 need = frac2(limit_log - log2u(static_cast<u32>(peak)));
        if (need < target) target = need;
    }
    if (target < g) {
        u32 first = 0;
        while (first < frames && absolute(y[first]) <= limit_top) ++first;
        u32 k = 4;
        while (k < 6 && (2u << k) <= first + 1) ++k;
        step = (target - g) >> k;
    } else {
        step = (target - g) >> 6;
    }
    voice.lim = target;
}

inline i32 control(i32 word) {
    const u32 r = static_cast<u32>(word < 0 ? 0 : word) >> 24;
    return static_cast<i32>(r > 127 ? 127 : r);
}

__attribute__((cold)) inline void begin(Voice& v, Shared& shared, opxy::Parameters parameters) {
    // Clear the voice word by word up to its room, which set_room clears as
    // far as the room uses it.
    const u32 owner = v.owner, model = v.next;
    const i32 tune = v.tune, tone = v.tone;
    u32* words = reinterpret_cast<u32*>(&v);
    const u32 count = static_cast<u32>(reinterpret_cast<const char*>(v.room) - reinterpret_cast<const char*>(&v)) / 4;
    for (u32 i = 0; i < count; ++i) words[i] = 0;
    v.owner = owner;
    v.model = v.next = model;
    v.tune = tune;
    v.tone = tone;
    v.seed = ((++shared.notes) * 2654435761u + owner * 40503u) | 1u;
    i32 r[8];
    for (u32 i = 0; i < 8; ++i) r[i] = control(parameters[i]);

    // the platter's speed, and the key's TUNE as a finer change of it
    v.lbase = rec::speed_log(r[rec::SPEED]) + ST(1) * tune;
    v.lrate = v.lbase;
    v.r16 = static_cast<i32>(pow2(v.lbase + 16 * octave));
    // the hit: the bar the needle drops on
    Pick p;
    p.force = rec::force(r[rec::PICK]);
    p.bar = rec::bar(r[rec::PICK]);
    p.bleed = rec::bleed(r[rec::PICK]);
    v.comp_thr = U;   // the valve compressor's threshold (a model may set its own)
    setup_model(v, model, p);
    v.glide_k = frac2(block_slope(L(0.022) - v.lbase));
    // WARP: the disc rises and falls once a turn (and a little twice), from a
    // fixed point of the turn, so a key plays the same each time
    v.warp_depth = rec::warp_log(r[rec::WARP]);
    v.warp_inc = increment(L(0.75) + rec::speed_log(r[rec::SPEED])) * 64u;
    v.warp_phase = 0x1a000000u;   // the hit starts as the stylus reaches the first crest
    v.warp_k = onepole_k(L(18000));
    v.warp_to = one;
    // DRAG: the brush slows the platter towards its full sag in about a quarter second
    v.drag_depth = rec::drag_log(r[rec::DRAG]);
    v.drag_x = one;
    v.drag_k = frac2(block_slope(L(0.16)));
    // CUE: early, the groove before the hit (its crackle and the last bar's
    // tail in the room); late, the needle lands into the hit
    v.pre = rec::cue_blocks(r[rec::CUE]) * frames;
    v.land_len = static_cast<u32>((rec::cue_late_tenths(model, r[rec::CUE]) * 4516) >> 10);
    v.out_g = v.land_len ? 0 : one;
    v.land_step = v.land_len ? static_cast<i32>(pow2(31 * octave - log2u(v.land_len))) : 0;
    // the scratch of the stylus settling, louder the later it lands (at most
    // about -30 dB under the ceiling)
    v.land_noise = multiply(half_scale / 24, q31(rec::force(r[rec::CUE] > 64 ? (r[rec::CUE] - 64) * 2 : 0)));
    // LIFT: the needle lifted, then its soft thump
    v.lift_at = v.pre + samples(rec::lift_log(model, r[rec::LIFT]));
    v.lift_len = 441;
    v.lift_step = static_cast<i32>(pow2(31 * octave - log2u(v.lift_len)));
    v.thump_inc = increment(L(55.0));
    v.thump_amp = half_scale / 40;
    v.end_at = v.lift_at + v.lift_len + 1000;
    // WEIGHT: either side of 2 g. Lighter, the stylus cannot follow a groove
    // that moves faster than its force allows and breaks up (a slew limit
    // that falls an octave each third of the way); heavier, it rounds the
    // loud peaks, darkens, fills out the low end and rides the dust.
    const i32 lw = rec::weight_log(r[rec::WEIGHT]);
    const i32 light0 = clamp((L(2.0) - lw) >> 1, 0, 65536), heavy0 = clamp(mul16(lw - L(2.0), Q16(65536.0 / 86633.0)), 0, 65536);
    // (each side heard soon after it leaves 2 g)
    const i32 light = light0 + mul16(light0 - mul16(light0, light0), Q16(0.3)), heavy = heavy0 + mul16(heavy0 - mul16(heavy0, heavy0), Q16(0.6));
    v.light = q31(light);
    v.heavy = q31(heavy);
    v.w_ks = onepole_k(L(2400));
    v.w_kd = onepole_k(lmix(L(17000), L(3000), heavy0));
    v.w_kl = onepole_k(L(120));
    v.w_seed = (v.seed ^ 0x5bd1e995u) | 1u;
    // WEAR: crackle, hiss, rumble, worn grooves and a dulled top (and a heavy
    // arm picks up some of the dust on any copy)
    // (its noise rises quickly at first: a little wear is already heard)
    const i32 w0 = rec::force(r[rec::WEAR]), w1 = w0 + mul16(w0 - mul16(w0, w0), Q16(0.6));
    const i32 wear = w1 > mul16(heavy, Q16(0.25)) ? w1 : mul16(heavy, Q16(0.25));
    v.wear = q31(wear);
    // crackles: up to 110 a second, at most one a block
    v.crackle_p = static_cast<u32>(multiply(v.wear, Q31(110.0 * 64 / 44100.0 / 2))) * 2u;
    v.k_hiss = onepole_k(L(3500));
    v.k_dull = onepole_k(lmix(L(18000), L(2400), w0));
    v.w1 = w1;
    // TONE: the player's tone control, a treble shelf of so many dB over
    // 2 kHz; the hit's level comes back by what the shelf adds to or takes
    // from its loudness (its treble's share, levels::treble), so TONE
    // changes the colour, not the level
    v.tone_k = onepole_k(L(2000));
    {
        const i32 lg = tone * L(1.1220184543);   // the shelf's gain, log2
        const i32 s16 = levels::treble[model];
        const u32 g2 = pow2(2 * lg + 16 * octave);   // its power gain, Q16
        const u32 total = static_cast<u32>(65536 - s16) + ((static_cast<u32>(s16) * (g2 >> 4)) >> 12);
        const i32 back = -(log2u(total) - 16 * octave) / 2;
        v.tone_lo = static_cast<i32>(pow2(back + 28 * octave));
        v.tone_hi = static_cast<i32>(pow2(back + lg + 28 * octave));
    }
    // the record: the valve compressor, the cut's bandwidth (at the record's speed)
    v.comp_g = one;
    v.comp_back = frac2(block_slope(L(0.09)));
    v.cut_kl = onepole_k(clamp(L(12500) + v.lbase, L(4000), L(19000)));
    v.cut_kh = onepole_k(clamp(L(26) + v.lbase, L(12), L(60)));

    // The level (levels.h, tools/calibrate.py): the model's gain brings its
    // default hit to its role's loudness; each control's trim holds that
    // loudness as it turns, and its rise tracks how far that lifts the hit's
    // peak; where the rises together pass the room the default leaves under
    // the ceiling, the hit comes down by the excess.
    // WEIGHT, WEAR and WARP darken through one low-pass (the darkest of
    // them sets it), so what they lift to make up for it overlaps: the
    // largest lift counts, and a quarter of the others'.
    i32 trim = levels::gain[model], rise = 0;
    i32 dark_t = 0, dark_tm = 0, dark_r = 0, dark_rm = 0;
    for (u32 s = 0; s < 8; ++s) {
        const signed char* t = levels::loud[model][s];
        const signed char* q = levels::rise[model][s];
        const i32 x = (r[s] * 4 * 65536) / 127;       // 0..4 in Q16
        const u32 i = static_cast<u32>(x >> 16) < 3 ? static_cast<u32>(x >> 16) : 3;
        const i32 f = x - static_cast<i32>(i << 16);
        const i32 dt = t[i] * 2048 + mul16((t[i + 1] - t[i]) * 2048, f);
        const i32 dq = q[i] * 2048 + mul16((q[i + 1] - q[i]) * 2048, f);
        if ((s == rec::WEIGHT || s == rec::WEAR || s == rec::WARP)) {
            dark_t += imax(dt, 0); dark_tm = imax(dark_tm, dt);
            dark_r += imax(dq, 0); dark_rm = imax(dark_rm, dq);
            trim += imin(dt, 0);
            rise += imin(dq, 0);
        } else {
            trim += dt;
            rise += dq;
        }
    }
    trim += dark_tm + ((dark_t - dark_tm) >> 2);
    rise += dark_rm + ((dark_r - dark_rm) >> 2);
    if (rise > levels::room[model]) trim -= rise - levels::room[model];   // level: cap
    v.gain = static_cast<i32>(pow2(clamp(trim + 25 * octave, 0, 31 * octave - 1)));   // Q31 of 64
    v.lim = one;

    // The player's own levels are the output's, whatever the hit's: the
    // groove's swings the stylus loses, the worn grooves' fuzz, the peaks a
    // heavy arm rounds and the noise of the surface and the brush are set
    // against the ceiling (half scale out is 2^(30 - trim) inside).
    // (against the model's own gain, not its controls' trims, so a control's
    // own loudness trim never moves what it does)
    const i32 base = levels::gain[model];
    const i32 top = 30 * octave - clamp(base, -6 * octave, 12 * octave);
    v.w_thr = static_cast<i32>(pow2(clamp(top - octave / 2 - mul16(5 * octave, light), 16 * octave, 30 * octave)));   // the highs it holds
    v.w_low_thr = static_cast<i32>(pow2(clamp(top - octave / 5 - mul16(3 * octave, light), 16 * octave, 30 * octave)));   // and the lows
    v.worn = static_cast<i32>(pow2(clamp(top + lmix(octave, -octave * 3 / 2, v.w1), 16 * octave, 30 * octave)));
    v.round_k = static_cast<u32>(clamp(((base + 32768) >> 16) + 1, 0, 6));
    const i32 lt = -clamp(base, -6 * octave, 12 * octave);
    // (the crackle's loudness rises fastest: a little wear is heard at once)
    const i32 w0c = rec::force(r[rec::WEAR]);
    v.n_crackle = multiply(frac2(clamp(L(0.2) + 3 * octave + lt, -30 * octave, 0)), q31(2 * w0c - mul16(w0c, w0c)));
    v.n_hiss = multiply(frac2(clamp(L(0.21) + lt, -30 * octave, 0)), v.wear);
    v.n_rumble = multiply(frac2(clamp(L(1.3) + lt, -30 * octave, 0)), v.wear);
    v.brush = multiply(frac2(clamp(L(1.0) + lt, -30 * octave, 0)), q31(rec::drag_share(r[rec::DRAG])));
    v.tail = v.pre ? frac2(clamp(L(0.3) + lt, -30 * octave, 0)) : 0;
}

// The record's content for this block: the hit (once the needle reaches
// it) and the room, into x.
inline void record_block(Voice& v, i32* x) {
    for (unsigned i = 0; i < frames; ++i) x[i] = 0;
    // the platter's rate: its speed, the warp's wow, the brush's drag
    bool retune = false;
    if (v.warp_depth || v.drag_depth) {
        i32 l = v.lbase;
        if (v.warp_depth) {
            // two crests a turn and a slower swell: the disc's height under the stylus
            v.warp_phase += v.warp_inc;
            const i32 a = sine(v.warp_phase * 2u) >> 15, b = sine(v.warp_phase + 0x50000000u) >> 15;   // Q16
            const i32 h = a + mul16(b, Q16(0.35));
            l += mul16(v.warp_depth, h);
            // the stylus lightens over a crest: up to 4 dB down, and it loses
            // the groove's finest detail, the top falling as far as 3.2 kHz
            const i32 crest = h > 0 ? mul16(h, v.warp_depth) : 0;   // up to 1/8 octave of wow
            v.warp_to = frac2(-mul16(crest, Q16(4 / 6.02 / (150.0 / 1200))));
            v.warp_k = onepole_k(L(18000) - mul16(crest, Q16(2.5 / (150.0 / 1200))));
        }
        if (v.drag_depth) {
            v.drag_x = multiply(v.drag_x, v.drag_k);
            l -= mul16(v.drag_depth, 65536 - (v.drag_x >> 15));
        }
        v.lrate = l;
        v.r16 = static_cast<i32>(pow2(l + 16 * octave));
        retune = true;
    }
    if (v.t >= v.pre) {
        i32 e[frames];
        const unsigned struck = strike_block(v, e, x);
        v.glide = multiply(v.glide, v.glide_k);
        if (v.nmodes) modes_block(v, e, struck, x);
        layer_block(v, v.layer[0], x, retune);
        layer_block(v, v.layer[1], x, retune);
        metal_block(v, x, retune);
        v.hit_t += frames;
        v.tail = 0;
    } else if (v.tail) {
        v.work |= W_TAIL;
        // the last bar's tail: a dark wash sent into the room, falling away
        i32 l = v.tail_l;
        for (unsigned i = 0; i < frames; ++i) { l += multiply(noise(v.w_seed) - l, Q31(0.02)); x[i] = multiply(shl(l, 3), v.tail); }
        v.tail_l = l;
        v.tail = multiply(v.tail, Q31(0.93));
    }
    room_block(v, x);
}

// The record's chain and the player: the valve compressor and tape, the
// cut's bandwidth, then the stylus, the wear, the tone control and the drag
// brush. x in, y out (before the level and the needle).
inline void chain_block(Voice& v, const i32* x, i32* y) {
    // the compressor: a peak detector a block that lets a hit's first 10 ms
    // through and recovers in 90 ms, about 3:1 over its threshold
    i32 peak = 0;
    for (unsigned i = 0; i < frames; ++i) { const i32 a = absolute(x[i]); if (a > peak) peak = a; }
    v.comp = peak > v.comp ? v.comp + multiply(peak - v.comp, Q31(0.12)) : multiply(v.comp, v.comp_back);
    i32 target = v.comp > v.comp_thr ? frac2(-mul16(log2u(static_cast<u32>(v.comp)) - log2u(static_cast<u32>(v.comp_thr)), Q16(2.0 / 3.0))) : one;
    // WARP: the arm rides the warp; at its crests the stylus lightens, the
    // level dips and the top dulls (record_block sets how far); the dip rides
    // on the compressor's gain, the dulling on the cut's low-pass
    if (v.warp_depth) target = multiply(target, v.warp_to);
    // one low-pass for the cut, the warp's crest, a heavy arm's and a worn
    // copy's darkening: the lowest of them (the arm and the wear add a
    // second pole of their own)
    i32 kl = v.cut_kl;
    if (v.warp_depth) kl = imin(kl, v.warp_k);
    if (v.heavy) kl = imin(kl, v.w_kd);
    if (v.wear) kl = imin(kl, v.k_dull);
    if (v.metal.on) kl = imin(kl, v.metal.kl);
    i32 g = v.comp_g;
    const i32 dg = (target - g) >> 6, kh = v.cut_kh;
    i32 cl = v.cut_l, ch = v.cut_h;
    v.work |= peak > U / 2 ? W_VALVE : W_QUIET;
    if (peak > U / 2) {
        // loud: the valve, a soft cubic: (z - z^3 / 3) / 4, z the input at a
        // quarter of its clip (4 U)
        for (unsigned i = 0; i < frames; ++i) {
            const i32 z = shl(clamp(multiply(x[i], g), -(1 << 29) + 1, (1 << 29) - 1), 2);
            g += dg;
            const i32 s = (z >> 2) - multiply(multiply(multiply(z, z), z), Q31(1.0 / 12.0));
            cl += multiply(s - cl, kl);
            ch += multiply(cl - ch, kh);
            y[i] = cl - ch;
        }
    } else {
        // quiet: the valve is as good as straight
        for (unsigned i = 0; i < frames; ++i) {
            const i32 s = multiply(x[i], g);
            g += dg;
            cl += multiply(s - cl, kl);
            ch += multiply(cl - ch, kh);
            y[i] = cl - ch;
        }
    }
    v.cut_l = cl;
    v.cut_h = ch;
    v.comp_g = target;
}
inline void light_block(Voice& v, i32* y) {
    // WEIGHT, light: the stylus cannot hold the groove's loudest swings:
    // the highs over its grip clip and splatter, the loud lows break up
    if (v.light) {
        v.work |= W_LIGHT;
        const i32 th = v.w_thr, tl = v.w_low_thr, k = v.w_ks;
        i32 sp = v.w_s;
        u32 seed = v.w_seed;
        for (unsigned i = 0; i < frames; ++i) {
            sp += multiply(y[i] - sp, k);
            const i32 hi = y[i] - sp;
            const i32 h = clamp(hi, -th, th), lo = clamp(sp, -tl, tl);
            // the highs it loses, it splatters: noise as loud as their excess
            y[i] = lo + h + multiply(noise(seed), imin(absolute(hi - h), U) << 2);
        }
        v.w_s = sp;
        v.w_seed = seed;
    }
}
inline void heavy_block(Voice& v, i32* y) {
    // WEIGHT, heavy: rounder, the low end fuller
    if (v.heavy) {
        v.work |= W_HEAVY;
        const i32 kb = v.w_kl, h = multiply(v.heavy, Q31(0.99)), c = multiply(v.heavy, Q31(0.6));
        const u32 k = v.round_k;
        const i32 lim = static_cast<i32>(0x7fffffffu >> k);
        // the darkening's second pole (a worn copy's too, the lower of the two)
        const i32 kd = v.wear ? imin(v.w_kd, v.k_dull) : v.w_kd;
        i32 lo = v.w_lo, d = v.w_d2;
        for (unsigned i = 0; i < frames; ++i) {
            d += multiply(y[i] - d, kd);
            lo += multiply(d - lo, kb);
            const i32 f = d + multiply(lo, h);
            // round the peaks: f - c f^3, z = f against the ceiling
            const i32 z = shl(clamp(f, -lim, lim), k);
            y[i] = f - (multiply(multiply(multiply(z, z), z), c) >> k);
        }
        v.w_lo = lo;
        v.w_d2 = d;
    }
}
inline void wear_block(Voice& v, i32* y) {
    // WEAR: crackle, hiss, rumble, the worn grooves' fuzz on loud passages
    if (v.wear) {
        v.work |= W_WEAR;
        const i32 t3 = v.worn, hs = v.n_hiss;
        // the rumble: a slow random walk, a value a block, ramped
        const i32 r0 = v.rumble;
        v.rumble_to += multiply(multiply(noise(v.seed), v.n_rumble) - v.rumble_to, Q31(0.225));
        v.rumble = v.rumble_to;
        i32 rm = r0;
        const i32 dr = (v.rumble - r0) >> 6;
        i32 last = v.hiss, d = v.w_d2;
        // the darkening's second pole, unless a heavy arm's loop has run it
        const i32 kd = v.heavy ? one : v.k_dull;
        u32 seed = v.seed;
        for (unsigned i = 0; i < frames; ++i) {
            d += multiply(y[i] - d, kd);
            const i32 s = clamp(d, -t3, t3);
            // hiss: noise less the noise before it, a first difference (the top end)
            const i32 n = noise(seed);
            y[i] = s + ((d - s) >> 2) + multiply(n - last, hs) + rm;
            last = n;
            rm += dr;
        }
        v.hiss = last;
        if (!v.heavy) v.w_d2 = d;
        // a crackle: most small, a few loud, at a random sample of the block
        if (rand32(seed) < v.crackle_p) {
            const i32 a = static_cast<i32>(rand32(seed)) >> 2;
            const i32 c = multiply(multiply(a, absolute(a) << 1), v.n_crackle);
            const u32 at = rand32(seed) >> 26;
            y[at] = add(y[at], c);
            if (at + 1 < frames) y[at + 1] = add(y[at + 1], -(c >> 1));
        }
        v.seed = seed;
    }
}
inline void tone_block(Voice& v, i32* y) {
    // TONE: the player's tone control, a shelf at 2 kHz
    if (v.tone) {
        v.work |= W_TONE;
        const i32 hi = v.tone_hi, lo = v.tone_lo, k = v.tone_k;
        i32 l1 = v.tone_l;
        for (unsigned i = 0; i < frames; ++i) {
            l1 += multiply(y[i] - l1, k);
            y[i] = shl(multiply(l1, lo) + multiply(y[i] - l1, hi), 3);
        }
        v.tone_l = l1;
    }
}
inline void brush_block(Voice& v, i32* y) {
    // DRAG: the brush's bristles on the vinyl (not while the strike sounds:
    // a few milliseconds of them under a transient tens of dB louder)
    if (v.brush && !(v.work & W_STRIKE)) {
        v.work |= W_BRUSH;
        const i32 b = multiply(v.brush, one - v.drag_x);
        i32 l = v.brush_l;
        u32 seed = v.w_seed;
        for (unsigned i = 0; i < frames; ++i) { l += multiply(noise(seed) - l, Q31(0.15)); y[i] = add(y[i], multiply(l, b)); }
        v.brush_l = l;
        v.w_seed = seed;
    }
}
inline void play_block(Voice& v, const i32* x, i32* y) {
    chain_block(v, x, y);
    light_block(v, y);
    heavy_block(v, y);
    wear_block(v, y);
    tone_block(v, y);
    brush_block(v, y);
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    voice.work = voice.struck_modes = voice.ringing_modes = voice.noise_layers = voice.grain_layers = 0;
    if (voice.pending) {
        // the note's setup takes its first block, which is silent: the hit
        // starts on the next (1.5 ms later), so no block does both
        begin(voice, shared, parameters);
        voice.work = W_BEGIN;
        for (unsigned i = 0; i < frames; ++i) output[i] = 0;
        return;
    }
    if (voice.done) {
        for (unsigned i = 0; i < frames; ++i) output[i] = 0;
        return;
    }
    i32 x[frames];
    record_block(voice, x);
    play_block(voice, x, output);
    // the needle: landing late fades the hit in; lifting fades all of it out
    const u32 t = voice.t;
    i32 og = voice.out_g, step = 0;
    if (voice.land_len && t + frames > voice.pre) step = voice.land_step;
    if (t + frames > voice.lift_at) step = -voice.lift_step;
    voice.t += frames;
    const i32 gain = voice.gain;
    i32 most = 0;
    if (step || og != one) {
        voice.work |= W_NEEDLE;
        const i32 to = clamp(add(og, clamp(step, -(one >> 6), one >> 6) * 64), 0, one);
        voice.out_g = to;
        // landing, the hit comes up slowly at first (the square of the ramp)
        if (voice.land_len) { og = multiply(og, og); }
        const i32 to_ = voice.land_len ? multiply(to, to) : to;
        const i32 d = (to_ - og) >> 6;
        if (voice.land_len) {
            // landing: the stylus settles into the groove, scratching as it comes in
            u32 seed = voice.w_seed;
            i32 last = voice.drop;
            const i32 lvl = voice.land_noise;
            for (unsigned i = 0; i < frames; ++i) {
                const i32 n = noise(seed);
                const i32 y = add(shl(clamp(multiply(multiply(output[i], og), gain), -(one >> 6), one >> 6), 6), multiply(n - last, multiply(one - og, lvl)));
                last = n;
                og += d;
                output[i] = y;
                most = imax(most, absolute(y));
            }
            voice.w_seed = seed;
            voice.drop = last;
        } else {
            for (unsigned i = 0; i < frames; ++i) {
                const i32 y = shl(clamp(multiply(multiply(output[i], og), gain), -(one >> 6), one >> 6), 6);
                og += d;
                output[i] = y;
                most = imax(most, absolute(y));
            }
        }
        if (voice.land_len && to >= one) voice.land_len = 0;
    } else {
        for (unsigned i = 0; i < frames; ++i) {
            const i32 y = shl(clamp(multiply(output[i], gain), -(one >> 6), one >> 6), 6);
            output[i] = y;
            most = imax(most, absolute(y));
        }
    }
    // the stylus leaving the groove: one cycle at 55 Hz, its second half
    // smaller, at a level of its own whatever the hit's
    if (voice.t > voice.lift_at + voice.lift_len / 2 && voice.thump_phase < 0xf0000000u) {
        for (unsigned i = 0; i < frames && voice.thump_phase < 0xf0000000u; ++i) {
            const i32 s = multiply(sine(voice.thump_phase), voice.thump_amp);
            output[i] = add(output[i], voice.thump_phase < 0x80000000u ? s : s >> 1);
            voice.thump_phase += voice.thump_inc;
        }
    }
    // the transient limiter and the soft knee, in one pass
    // (tools/calibrate.py builds a copy without the lines marked "level:",
    // to measure the peaks the cap, the limiter and the knee take)
    i32 lg = one, lstep = 0, lto = one;
    bool limited = false, kneed = false;
    if (levels::limit[voice.model]) { limit_gain(voice, output, most, lg, lstep, lto); limited = lg != one || lto != one; }   // level: limiter
    if (most > half_scale - (half_scale >> 2)) kneed = true;   // level: knee
    if (limited) voice.work |= W_LIMIT;
    if (kneed) voice.work |= W_KNEE;
    if (limited && kneed) {
        for (unsigned i = 0; i < frames; ++i) { lg = lstep < 0 ? imax(lg + lstep, lto) : imin(lg + lstep, lto); output[i] = knee(multiply(output[i], lg)); }
    } else if (limited) {
        for (unsigned i = 0; i < frames; ++i) { lg = lstep < 0 ? imax(lg + lstep, lto) : imin(lg + lstep, lto); output[i] = multiply(output[i], lg); }
    } else if (kneed) {
        for (unsigned i = 0; i < frames; ++i) output[i] = knee(output[i]);
    }
    const bool loud = most >= quiet_under;
    voice.quiet = loud ? 0 : voice.quiet + 1;
    const bool waiting = voice.t < voice.pre + 4410 || voice.wear || voice.layer[0].next < voice.layer[0].bursts ||
                         voice.metal.next < voice.metal.bursts;
    if (voice.t >= voice.end_at || (voice.quiet >= quiet_blocks && !waiting)) voice.done = 1;
}
}
