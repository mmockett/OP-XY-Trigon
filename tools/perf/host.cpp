// A kit's renders for CI's render-identity and compare jobs (.github/workflows/
// perf.yml). Built on the host with the kit's own engine.cpp:
//   c++ -O2 -std=c++11 -I<kit> -I<sdk>/sdk/include -I<sdk>/sdk/platform/opxy-1.2.1 \
//       $(python3 tools/perf/kit.py <kit>) tools/perf/host.cpp <kit sources> -o render
//   ./render out.bin [raw|hash] < jobs > index.txt
// Each job line on stdin:
//   bench <model> <level>          the profiler's whole workload in its order, its
//                                  retriggers and full polyphony on that model and level
//   ceiling <model> <level>        full polyphony of one model and level
//   choke <model> <level>          same-key retriggers of one model and level
//   hit <name> <model> c0..c7 p0 p1 <blocks>   one voice straight through the engine
// Every run's output goes to out.bin, and a line to stdout:
//   <name> <raw|hash> <voices> <blocks> <offset> <bytes>
// raw: each block's 64 samples per voice, int32 little-endian, in block then
// voice order. hash: a 32-bit FNV-1a of each voice's block instead (runs of
// more than one voice, and hits unless the second argument is raw). State carries from line to line, as on the OP-XY.
#include "engine.cpp"
#include "bench.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
FILE* out;
unsigned long offset;
bool raw_hits;

struct Recorder {
    bool raw;
    unsigned long bytes;
    opxy::u32 clock() { return 0; }
    void spent(opxy::u32) {}
    void block(opxy::u32, const opxy::i32* samples) {
        if (raw) {
            unsigned char b[256];
            for (int i = 0; i < 64; ++i) {
                const opxy::u32 s = static_cast<opxy::u32>(samples[i]);
                b[4 * i] = static_cast<unsigned char>(s);
                b[4 * i + 1] = static_cast<unsigned char>(s >> 8);
                b[4 * i + 2] = static_cast<unsigned char>(s >> 16);
                b[4 * i + 3] = static_cast<unsigned char>(s >> 24);
            }
            std::fwrite(b, 1, sizeof b, out);
            bytes += sizeof b;
        } else {
            opxy::u32 h = 2166136261u;
            for (int i = 0; i < 64; ++i)
                for (int k = 0; k < 4; ++k) h = (h ^ static_cast<unsigned char>(static_cast<opxy::u32>(samples[i]) >> (8 * k))) * 16777619u;
            unsigned char b[4] = {static_cast<unsigned char>(h), static_cast<unsigned char>(h >> 8),
                                  static_cast<unsigned char>(h >> 16), static_cast<unsigned char>(h >> 24)};
            std::fwrite(b, 1, 4, out);
            bytes += 4;
        }
    }
};

int model_of(const char* name) {
    for (opxy::u32 m = 0; m < bench::kModels; ++m)
        if (!std::strcmp(KIT_NS::model_name(m), name)) return static_cast<int>(m);
    char* end;
    const long m = std::strtol(name, &end, 10);
    if (*end || m < 0 || m >= static_cast<long>(bench::kModels)) { std::fprintf(stderr, "no model %s\n", name); std::exit(2); }
    return static_cast<int>(m);
}
opxy::u32 level_of(int level) {
    for (opxy::u32 i = 0; i < 3; ++i)
        if (bench::kLevels[i] == level) return i;
    std::fprintf(stderr, "no level %d\n", level);
    std::exit(2);
}

void one_run(const char* name, opxy::u32 index, opxy::u32 model, opxy::u32 level) {
    const opxy::u32 voices = bench::run_voices(index);
    Recorder r{voices == 1, 0};
    bench::run(index, model, level, r);
    std::printf("%s %s %u %u %lu %lu\n", name, r.raw ? "raw" : "hash", voices, bench::kBlocks, offset, r.bytes);
    offset += r.bytes;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !(out = std::fopen(argv[1], "wb"))) { std::fprintf(stderr, "usage: render out.bin [raw|hash] < jobs\n"); return 2; }
    raw_hits = argc > 2 && !std::strcmp(argv[2], "raw");
    char line[512], kind[32], name[128], mname[64];
    opxy::u32 job = 0;
    while (std::fgets(line, sizeof line, stdin)) {
        if (std::sscanf(line, "%31s", kind) != 1 || kind[0] == '#') continue;
        int level;
        if (!std::strcmp(kind, "bench") || !std::strcmp(kind, "ceiling") || !std::strcmp(kind, "choke")) {
            if (std::sscanf(line, "%*s %63s %d", mname, &level) != 2) { std::fprintf(stderr, "bad line: %s", line); return 2; }
            const opxy::u32 m = static_cast<opxy::u32>(model_of(mname)), l = level_of(level);
            char label[160];
            if (!std::strcmp(kind, "bench")) {
                for (opxy::u32 index = 0; index < bench::kRuns; ++index) {
                    if (index == 0) std::snprintf(label, sizeof label, "bench:kit");
                    else if (index <= bench::kSingle)
                        std::snprintf(label, sizeof label, "bench:%s@%u", KIT_NS::model_name((index - 1) / 3), bench::kLevels[(index - 1) % 3]);
                    else std::snprintf(label, sizeof label, "bench:%s:%s@%u", index == bench::kSingle + 1 ? "choke" : "ceiling",
                                       KIT_NS::model_name(m), bench::kLevels[l]);
                    one_run(label, index, m, l);
                }
            } else {
                const bool ceiling = !std::strcmp(kind, "ceiling");
                std::snprintf(label, sizeof label, "%s:%s@%u", kind, KIT_NS::model_name(m), bench::kLevels[l]);
                one_run(label, ceiling ? bench::kSingle + 2 : bench::kSingle + 1, m, l);
            }
        } else if (!std::strcmp(kind, "hit")) {
            int c[8], pad[2];
            unsigned blocks;
            if (std::sscanf(line, "%*s %127s %63s %d %d %d %d %d %d %d %d %d %d %u", name, mname, &c[0], &c[1], &c[2], &c[3], &c[4],
                            &c[5], &c[6], &c[7], &pad[0], &pad[1], &blocks) != 13) {
                std::fprintf(stderr, "bad line: %s", line);
                return 2;
            }
            Recorder r{raw_hits, 0};
            const opxy::u32 b = bench::hit(job++, static_cast<opxy::u32>(model_of(mname)), c, pad, blocks, r);
            std::printf("%s %s 1 %u %lu %lu\n", name, r.raw ? "raw" : "hash", b, offset, r.bytes);
            offset += r.bytes;
        } else {
            std::fprintf(stderr, "bad line: %s", line);
            return 2;
        }
    }
    std::fclose(out);
    return 0;
}
