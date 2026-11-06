// FELT's voice, traced for tools/cpu.py: which of the engine's countable
// functions (its mode loops, its stages, its note-on set-up) each block
// calls, and how often, for every model at its defaults, all 0, all 64 and
// all 127, with the pad controls at their tops. Built with
// -finstrument-functions-after-inlining, so only real calls are seen. Not
// part of the machine.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>

namespace {
struct Named { void* fn; const char* name; };
unsigned calls[64];
unsigned depth_string = 0, partials = 0;
Named named[64];
unsigned count = 0;
void add(void* fn, const char* name) { named[count].fn = fn; named[count].name = name; ++count; }
}

extern "C" {
void __cyg_profile_func_enter(void* fn, void*) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void* fn, void*) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void* fn, void*) {
    for (unsigned i = 0; i < count; ++i)
        if (named[i].fn == fn) {
            ++calls[i];
            if (!std::strcmp(named[i].name, "add_string")) depth_string = 1;
            if (depth_string && !std::strcmp(named[i].name, "add_mode")) ++partials;
            return;
        }
}
void __cyg_profile_func_exit(void* fn, void*) {
    for (unsigned i = 0; i < count; ++i)
        if (named[i].fn == fn && !std::strcmp(named[i].name, "add_string")) depth_string = 0;
}
}

int main() __attribute__((no_instrument_function));
int main() {
    using namespace felt;
    add(reinterpret_cast<void*>(&run_free), "run_free");
    add(reinterpret_cast<void*>(&run_driven), "run_driven");
    add(reinterpret_cast<void*>(&run_free_high), "run_free_high");
    add(reinterpret_cast<void*>(&run_driven_high), "run_driven_high");
    add(reinterpret_cast<void*>(&stage_buzz), "stage_buzz");
    add(reinterpret_cast<void*>(&stage_ring), "stage_ring");
    add(reinterpret_cast<void*>(&stage_slaps), "stage_slaps");
    add(reinterpret_cast<void*>(&stage_sweep), "stage_sweep");
    add(reinterpret_cast<void*>(&stage_hiss), "stage_hiss");
    add(reinterpret_cast<void*>(&stage_beads), "stage_beads");
    add(reinterpret_cast<void*>(&stage_bow), "stage_bow");
    add(reinterpret_cast<void*>(&stage_grain), "stage_grain");
    add(reinterpret_cast<void*>(&stage_top), "stage_top");
    add(reinterpret_cast<void*>(&stage_highpass), "stage_highpass");
    add(reinterpret_cast<void*>(&add_mode), "add_mode");
    add(reinterpret_cast<void*>(&add_pedal), "add_pedal");
    add(reinterpret_cast<void*>(&add_string), "add_string");
    add(reinterpret_cast<void*>(&add_knock), "add_knock");
    add(reinterpret_cast<void*>(&strike), "strike");
    add(reinterpret_cast<void*>(&land_damper), "land_damper");
    add(reinterpret_cast<void*>(&bend_modes), "bend_modes");
    add(reinterpret_cast<void*>(&pedal_strings), "pedal_strings");
    add(reinterpret_cast<void*>(&common), "common");
    add(reinterpret_cast<void*>(&setup_thud), "setup_thud");
    add(reinterpret_cast<void*>(&setup_rattle), "setup_rattle");
    add(reinterpret_cast<void*>(&setup_lid), "setup_lid");
    add(reinterpret_cast<void*>(&setup_tack), "setup_tack");
    add(reinterpret_cast<void*>(&setup_screw), "setup_screw");
    add(reinterpret_cast<void*>(&setup_brush), "setup_brush");
    add(reinterpret_cast<void*>(&setup_cluster), "setup_cluster");
    add(reinterpret_cast<void*>(&setup_beads), "setup_beads");
    add(reinterpret_cast<void*>(&setup_pluck), "setup_pluck");
    add(reinterpret_cast<void*>(&setup_bow), "setup_bow");
    add(reinterpret_cast<void*>(&screw_modes), "screw_modes");
    add(reinterpret_cast<void*>(&cluster_bolts), "cluster_bolts");
    static Voice voice;
    static Shared shared;
    static opxy::i32 out[64];
    opxy::u32 pitch[64] = {};
    const int settings[4] = {-1, 0, 64, 127};
    for (u32 m = 0; m < kit::models; ++m)
        for (int s = 0; s < 4; ++s) {
            opxy::i32 params[8];
            for (int c = 0; c < 8; ++c) {
                const int raw = settings[s] < 0 ? kit::model[m].defaults[c] : settings[s];
                params[c] = raw >= 127 ? 0x7fffffff : raw << 24;
            }
            const opxy::i32 pads[2] = {15, 15};
            std::memset(&voice, 0, sizeof voice);
            initialize(voice, shared, 1);
            start(voice, shared, 1);
            for (int b = 0; b < 400 && !(b && finished(voice, shared)); ++b) {
                std::memset(calls, 0, sizeof calls);
                partials = 0;
                drum_select(voice, shared, 1, m);
                drum_pad(voice, shared, 1, pads);
                render(voice, shared, 1, out, params, pitch);
                std::printf("%u %d %d n=%u pedal=%u glides=%u partials=%u", m, settings[s], b, voice.n, voice.npedal, voice.glides, partials);
                for (unsigned i = 0; i < count; ++i) if (calls[i]) std::printf(" %s=%u", named[i].name, calls[i]);
                std::printf("\n");
            }
        }
    return 0;
}
