#pragma once
// The SDK's drum adapter and its profiling workload, for CI only: a copy of
// what open-xy 810db9d8 builds into a kit's firmware (sdk/platform/opxy-1.2.1/
// drum_voice.cpp.in, the parts that shape a voice's output) and of the
// opxy-drum-callback-v1 benchmark (benchmark_drum.cpp.in), so a host build
// can render, and the Blackfin simulator can count, exactly what the
// OP-XY's profiler plays. Not part of any kit.
//
// Include the kit's engine.cpp first, and define
//   KIT_NS        the kit's namespace (dust, felt, flicker)
//   KIT_CAPACITY  resources.voiceCapacity
//   KIT_MODELS    resources.modelCount
//   KIT_CHOKE     resources.chokeSameKey (0 or 1)
//   KIT_PADMIX    resources.padMix (0 or 1)
//   KIT_PAD_COUNT the number of resources.padControls
//   KIT_PAD_BITS  their bits, comma-separated
//   KIT_PAD_SIGNS bit f set when pad control f is signed
// (tools/perf/kit.py prints them from a kit's definition.json).
#include <opxy/q31.hpp>
#include "drum_storage.hpp"

namespace bench {
using opxy::i32;
using opxy::u32;
using opxy::u8;
typedef KIT_NS::Voice Voice;
typedef KIT_NS::Shared Shared;
const u32 kCapacity = KIT_CAPACITY, kModels = KIT_MODELS, kPadCount = KIT_PAD_COUNT, kPadSigns = KIT_PAD_SIGNS;
const bool kChoke = KIT_CHOKE != 0, kPadMix = KIT_PADMIX != 0;
static const u8 pad_bits[] = {KIT_PAD_BITS};
const u32 kVoiceIndices = 256, kChokeFrames = 128, kFrames = 64;

// ---- the adapter (drum_voice.cpp.in) ----
struct Handle { u32 slot, key, track; };
struct Slot { Voice voice; u32 owner, started, pad, fade, faded; };
static Handle handles[kVoiceIndices];
static Slot voices[kCapacity];
static u32 notes;
static Shared shared;
static opxy::platform::DrumCodec codec;
inline const opxy::platform::DrumCodec& storage() {
    if (!codec.models) codec = opxy::platform::drum_codec(kModels, KIT_NS::parameter_count);
    return codec;
}
inline u32 owner(const Handle* handle) { return static_cast<u32>(handle - handles) + 1; }
inline Slot* held(const Handle* handle) {
    return handle->slot && voices[handle->slot - 1].owner == owner(handle) ? &voices[handle->slot - 1] : 0;
}
inline void release(Slot* slot) {
    KIT_NS::destroy(slot->voice, shared, slot->owner);
    slot->owner = 0;
    slot->fade = 0;
    slot->faded = 0;
}
inline bool sooner(const Slot& a, const Slot& b) {
    if (kChoke && (a.fade || b.fade)) return a.fade && (!b.fade || a.fade < b.fade);
    return notes - a.started > notes - b.started;
}
// stock_sample.hpp sample_drum_key, with the track's keyboard octave at 0.
inline u32 drum_key(u32 key) { return 53u + static_cast<u32>((static_cast<i32>(key) + 19) % 24); }

inline void start(Handle* handle, u32 key) {
    const bool keyed = key < 128 && handle->track < 8;
    handle->key = keyed ? drum_key(key) : 53;
    const u32 id = owner(handle);
    Slot* slot = held(handle);
    if (!slot) {
        slot = &voices[0];
        for (u32 i = 0; i < kCapacity; ++i) {
            if (!voices[i].owner) { slot = &voices[i]; break; }
            if (sooner(voices[i], *slot)) slot = &voices[i];
        }
        if (slot->owner) release(slot);
        unsigned char* bytes = reinterpret_cast<unsigned char*>(&slot->voice);
        for (unsigned i = 0; i < sizeof(Voice); ++i) bytes[i] = 0;
        slot->owner = id;
        KIT_NS::initialize(slot->voice, shared, id);
        handle->slot = static_cast<u32>(slot - voices) + 1;
    }
    slot->started = ++notes;
    slot->pad = keyed ? (handle->track << 8 | (handle->key + 1)) : 0;
    slot->fade = 0;
    slot->faded = 0;
    if (kChoke && slot->pad)
        for (u32 i = 0; i < kCapacity; ++i)
            if (voices[i].owner && &voices[i] != slot && voices[i].pad == slot->pad && !voices[i].fade)
                voices[i].fade = kChokeFrames;
    KIT_NS::start(slot->voice, shared, id);
}
inline void destroy(Handle* handle) {
    if (Slot* slot = held(handle)) release(slot);
    handle->slot = 0;
}
inline Handle* factory(u32 index, u32 track) {
    Handle* handle = &handles[index];
    if (Slot* slot = held(handle)) release(slot);
    handle->key = 53;
    handle->track = track;
    handle->slot = 0;
    return handle;
}
inline void render_voice(Handle* handle, i32* output, const i32* region, const i32* parameters,
                         const u32* increments, const i32* amplitude) {
    using namespace opxy::platform;
    if (Slot* slot = held(handle)) {
        u8 model, stored[8];
        KIT_NS::drum_defaults(handle->key, &model, stored);
        if (region && drum_marked_q31(region[0])) drum_unpack(region + 1, &model, stored, storage());
        if (model >= kModels) KIT_NS::drum_defaults(handle->key, &model, stored);
        const bool ready = region && drum_marked_q31(region[0]) && drum_pad_ready(region[kDrumPadPayload]);
        u8 played[8];
        drum_offset(stored, parameters, played);
        i32 live[8];
        for (u32 i = 0; i < 8; ++i)
            live[i] = played[i] == 127 ? 0x7fffffff : static_cast<i32>(static_cast<u32>(played[i]) << 24);
        u32 pitch[kFrames];
        for (unsigned i = 0; i < kFrames; ++i) pitch[i] = increments[i] << 16;
        KIT_NS::drum_select(slot->voice, shared, slot->owner, model);
        if (kPadCount) {
            i32 pads[kDrumKitPadFields];
            for (u32 i = 0; i < kDrumKitPadFields; ++i)
                pads[i] = ready && i < kPadCount ? drum_pad_get(region[kDrumPadPayload], i, pad_bits, kPadSigns) : 0;
            KIT_NS::drum_pad(slot->voice, shared, slot->owner, static_cast<const i32*>(pads));
        }
        KIT_NS::render(slot->voice, shared, slot->owner, output, live, pitch);
        for (unsigned i = 0; i < kFrames; ++i) output[i] = opxy::q31::multiply(output[i], amplitude[i]);
        const i32 gain = kPadMix && ready ? region[kDrumGainWord] : 0;
        if (gain > 0 && gain < kDrumUnityGain) {
            const i32 scale = gain << 4;
            for (unsigned i = 0; i < kFrames; ++i) output[i] = opxy::q31::multiply(output[i], scale);
        }
        if (kChoke && slot->fade) {
            u32 left = slot->fade;
            for (unsigned i = 0; i < kFrames; ++i) {
                output[i] = left ? opxy::q31::multiply(output[i], static_cast<i32>((left - 1) << 24)) : 0;
                if (left) --left;
            }
            slot->fade = left;
            if (!left) {
                const u32 id = slot->owner;
                release(slot);
                slot->faded = id;
            }
        }
    } else {
        for (unsigned i = 0; i < kFrames; ++i) output[i] = 0;
        if (handle->slot && voices[handle->slot - 1].faded == owner(handle)) voices[handle->slot - 1].faded = 0;
    }
}

// ---- the workload (benchmark_drum.cpp.in) ----
const u32 kHit = 100, kBlocks = 1500;
const u32 kVoices = kCapacity < 2 ? 2 : kCapacity;
const u8 kLevels[3] = {64, 0, 127};
const u32 kSingle = kModels * 3u;
// Runs as the profiler numbers them: 0 the kit's own keys at full polyphony;
// 1..kSingle one voice of model (run - 1) / 3 at level kLevels[(run - 1) % 3];
// kSingle + 1 same-key retriggers and kSingle + 2 full polyphony, both of
// the model and level given (on the OP-XY, the heaviest single run).
const u32 kRuns = kSingle + 3;
inline u32 run_voices(u32 run) { return run >= 1 && run <= kSingle ? 1 : run == kSingle + 1 ? 2 : kVoices; }
inline i32 marked() { return static_cast<i32>(42u * 32537631u + 16268815u); }

// Sink: u32 clock(); void block(u32 voice, const i32* output); void spent(u32 cycles).
template <class Sink> void run(u32 index, u32 heavy_model, u32 heavy_level, Sink& sink) {
    using namespace opxy::platform;
    const bool one = index >= 1 && index <= kSingle, choke = index == kSingle + 1;
    const u32 sounding = run_voices(index);
    const u32 model = one ? (index - 1) / 3 : heavy_model, level = one ? (index - 1) % 3 : heavy_level;
    const u32 saved = index == 0 ? 24 : 1;
    i32 pad_word = static_cast<i32>(kDrumPadReady);
    for (u32 f = 0; f < kPadCount && f < kDrumKitPadFields; ++f)
        pad_word = drum_field_set(pad_word, drum_pad_low(f, pad_bits), pad_bits[f],
                                  drum_field_max(pad_bits[f], (kPadSigns >> f & 1u) != 0));
    static i32 payloads[24][8];
    for (u32 k = 0; k < 24; ++k)
        for (u32 w = 0; w < 8; ++w) payloads[k][w] = 0;
    for (u32 k = 0; k < saved; ++k) {
        u8 stored[8];
        u8 chosen = static_cast<u8>(model);
        if (index == 0) KIT_NS::drum_defaults(53 + k, &chosen, stored);
        else for (u32 i = 0; i < 8; ++i) stored[i] = kLevels[level];
        payloads[k][0] = marked();
        payloads[k][kDrumPadPayload] = index == 0 ? 0 : pad_word;
        payloads[k][kDrumGainWord] = index == 0 || !kPadMix ? kDrumUnityGain : kDrumUnityGain / 2;
        drum_pack(chosen, stored, payloads[k] + 1, storage());
    }
    u32 pitch[kFrames];
    i32 gain[kFrames], parameters[8];
    for (u32 i = 0; i < kFrames; ++i) { pitch[i] = 25480118u >> 16; gain[i] = 0x7fffffff; }
    for (u32 i = 0; i < 8; ++i) parameters[i] = 0x40000000;
    Handle* voices_of[kVoices];
    for (u32 v = 0; v < sounding; ++v) voices_of[v] = factory(kVoiceIndices - 1 - v, 0);
    i32 output[kFrames];
    for (u32 block = 0; block < kBlocks; ++block) {
        u32 spent = sink.clock();
        if (choke) {
            if (block % 8u == 0) start(voices_of[(block / 8u) % 2u], 53);
        } else {
            for (u32 v = 0; v < sounding; ++v)
                if ((block + v * 5u) % kHit == 0) start(voices_of[v], 53 + v % 24u);
        }
        spent = sink.clock() - spent;
        for (u32 v = 0; v < sounding; ++v) {
            const u32 begin = sink.clock();
            render_voice(voices_of[v], output, payloads[index == 0 ? v % 24u : 0], parameters, pitch, gain);
            spent += sink.clock() - begin;
            sink.block(v, output);
        }
        sink.spent(spent);
    }
    for (u32 v = 0; v < sounding; ++v) destroy(voices_of[v]);
}

// A single hit straight through the engine, as the kits' own sweeps play it
// (tools/ub.sh): a fresh voice, the model, its eight controls and its two pad
// controls, for up to `blocks` blocks or until the voice says it has finished.
template <class Sink> u32 hit(u32 job, u32 model, const int* controls, const int* pad, u32 blocks, Sink& sink) {
    static Voice voice;
    static Shared alone;
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&voice);
    for (unsigned i = 0; i < sizeof(Voice); ++i) bytes[i] = 0;
    alone.notes = job;
    KIT_NS::initialize(voice, alone, 1);
    KIT_NS::start(voice, alone, 1);
    i32 params[8];
    for (int i = 0; i < 8; ++i) params[i] = controls[i] >= 127 ? 0x7fffffff : static_cast<i32>(static_cast<u32>(controls[i]) << 24);
    const i32 pads[2] = {pad[0], pad[1]};
    u32 pitch[kFrames] = {};
    i32 out[kFrames];
    u32 b = 0;
    for (; b < blocks && !(b && KIT_NS::finished(voice, alone)); ++b) {
        KIT_NS::drum_select(voice, alone, 1, model);
        KIT_NS::drum_pad(voice, alone, 1, pads);
        KIT_NS::render(voice, alone, 1, out, params, pitch);
        sink.block(0, out);
    }
    return b;
}
}  // namespace bench
