// The profiler's drum workload (opxy-drum-callback-v1, tools/perf/bench.h)
// for the GNU Blackfin simulator, counted with the CYCLES register as the
// OP-XY's profiler counts it: each block's starts and renders. Built with the
// SDK's pinned bfin-elf-g++ and flags (tooling/native.py) against a kit's
// engine.cpp, linked with crt0.s and sim.ld, and run in CI by bfin-elf-run.
// The simulator counts an instruction a cycle, with its own fixed delays for
// branches, calls, returns and 32-bit multiplies, and no memory or register
// stalls: a count, not a measurement. To keep the simulator's time down it
// runs each run's first 200 blocks and takes the profiler's 1500 as those
// plus thirteen more of the strikes' 100-block cycle (blocks 100 to 199):
// the voices are struck every 100 blocks, so later cycles repeat it. Prints
// one line per run:
//   run <index> <label> <voices> <total cycles> <worst block> <mean per block>
// in the profiler's order, choosing the heaviest single run for the
// retriggers and full polyphony as the profiler does. Not part of any kit.
#include "engine.cpp"
#include "bench.h"

extern "C" void sim_write(const char* text, unsigned length) __asm__("sim_write");
extern "C" int main() __asm__("_main");

namespace {
void put(const char* s) {
    unsigned n = 0;
    while (s[n]) ++n;
    sim_write(s, n);
}
void put(opxy::u32 v) {
    char b[12];
    int n = 11;
    b[n] = 0;
    do { b[--n] = static_cast<char>('0' + v % 10u); v /= 10u; } while (v);
    put(b + n);
}
const opxy::u32 kCounted = 200, kCycle = 100;
struct Counter {
    opxy::u32 total_low, total_high, worst, n, cycle;
    opxy::u32 clock() {
        opxy::u32 c;
        __asm__ volatile("%0 = CYCLES;" : "=d"(c));
        return c;
    }
    void block(opxy::u32, const opxy::i32*) {}
    void add(opxy::u32 cycles) {
        const opxy::u32 before = total_low;
        total_low += cycles;
        total_high += total_low < before;
    }
    void spent(opxy::u32 cycles) {
        add(cycles);
        if (n >= kCounted - kCycle) cycle += cycles;
        ++n;
        if (cycles > worst) worst = cycles;
    }
    // the rest of the profiler's blocks: the last cycle again, as often as it fits
    void finish() {
        for (opxy::u32 k = kCounted; k < bench::kBlocks; k += kCycle) add(cycle);
    }
};
}  // namespace

int main() {
    opxy::u32 heavy_model = 0, heavy_level = 0, most = 0;
    for (opxy::u32 index = 0; index < bench::kRuns; ++index) {
        Counter c = {0, 0, 0, 0, 0};
        const bool one = index >= 1 && index <= bench::kSingle;
        bench::run(index, heavy_model, heavy_level, c, kCounted);
        c.finish();
        const opxy::u32 mean = c.total_high ? 0xffffffffu : c.total_low / bench::kBlocks;
        if (one && (index == 1 || mean > most)) { most = mean; heavy_model = (index - 1) / 3; heavy_level = (index - 1) % 3; }
        put("run ");
        put(index);
        put(" ");
        if (index == 0) put("kit");
        else {
            const opxy::u32 m = one ? (index - 1) / 3 : heavy_model, l = one ? (index - 1) % 3 : heavy_level;
            if (!one) put(index == bench::kSingle + 1 ? "choke:" : "ceiling:");
            put(KIT_NS::model_name(m));
            put("@");
            put(bench::kLevels[l]);
        }
        put(" ");
        put(bench::run_voices(index));
        put(" ");
        put(c.total_low);
        put(" ");
        put(c.total_high);
        put(" ");
        put(c.worst);
        put(" ");
        put(mean);
        put("\n");
    }
    return 0;
}
