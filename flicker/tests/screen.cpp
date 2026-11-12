// FLICKER's screen checks, run by sdk.py test. The screen runs on the host
// for every model with each control through all 128 of its values (and TUNE
// and SLIT through all of theirs), and fails on any frame the OP-XY would draw
// wrongly beyond what opxy::draw::valid checks: a shown outline draws streaks
// or slivers on the device when it has no area (traced out and back, or
// turning straight back), when neighbouring points lie within an eighth of a
// pixel, or when its closing edge is under a quarter of a pixel. It follows a
// hit at every eighth value: how many nodes change a frame while the film
// moves (the device slows past about 20), that a still screen changes none,
// and that the film comes to rest a whole frame on. The drawings lie on the
// film's soundtrack in isometric (display.cpp's lay()): the outline checks
// read each one back off the track, face on, to see it is still one
// bilateral shape. It measures every caption
// in the OP-XY's font, checks the Shift rows, that the track's amp envelope
// moves nothing, that reduced motion keeps the film still, and that another
// key's notes move nothing.
#include "../display.cpp"
#include "../kit.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
using namespace opxy::draw;
typedef flicker::Frame F;
const opxy::i32 apart = unit / 8;
int failures = 0;

void fail_path(const char* when, const char* what, unsigned p, long a, long b) {
    if (failures < 40) std::printf("%s: path %u %s (%ld, %ld)\n", when, p, what, a, b);
    ++failures;
}
void check_paths(const F& f, const char* when) {
    for (unsigned p = 0; p < F::path_count; ++p) {
        if (f.nodes[p].x >= hidden_x * unit) continue;
        const Point* q = f.paths[p].points;
        const unsigned n = F::point_count;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            const long dx = b.x - a.x, dy = b.y - a.y;
            const long size = dx * dx + dy * dy;
            if (i + 1 < n && dx < apart && dx > -apart && dy < apart && dy > -apart) fail_path(when, "points merge at", p, i, i + 1);
            if (i + 1 == n && size < 4L * apart * apart) fail_path(when, "closes over", p, dx, dy);
            const Point& c = q[(i + 2) % n];
            const long ex = c.x - b.x, ey = c.y - b.y;
            const long cross = dx * ey - dy * ex, dot = dx * ex + dy * ey, next = ex * ex + ey * ey;
            if (dot < 0 && size && next) {
                // a turn within 2.5 degrees of straight back: sin^2 under 0.0019
                const double c2 = static_cast<double>(cross) * cross, s = static_cast<double>(size) * next;
                if (c2 < 0.0019 * s) fail_path(when, "turns straight back at point", p, (i + 1) % n, 0);
            }
        }
        double area = 0;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            area += (static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y) / 2;
        }
        area /= static_cast<double>(unit) * unit;
        if (area < 4 && area > -4) fail_path(when, "has almost no area (square px)", p, static_cast<long>(area * 1000), 0);
        // the outline stays one simple shape: read back off the track (the
        // inverse of lay()'s projection), its top edge runs left to right and
        // its bottom edge mirrors it
        double fx[64], fy[64];
        const double ax = flicker::art::along_x, ay = flicker::art::along_y, bx = flicker::art::across_x, by = flicker::art::across_y;
        const double det = ax * by + ay * bx;
        for (unsigned i = 0; i < n; ++i) {
            fx[i] = (static_cast<double>(q[i].x) * by + static_cast<double>(q[i].y) * bx) / det * unit;
            fy[i] = (static_cast<double>(q[i].y) * ax - static_cast<double>(q[i].x) * ay) / det * unit;
        }
        for (unsigned i = 0; i + 1 < 32; ++i)
            if (fx[i + 1] <= fx[i] + 64 || fx[63 - i] < fx[i] - 8 || fx[63 - i] > fx[i] + 8 || fy[i] > -600 || fy[63 - i] < 600) {
                fail_path(when, "is not a bilateral outline at point", p, i, 0);
                break;
            }
    }
}

// Univers Next Pro Light's advance widths for printable ASCII (32 to 126), in
// thousandths of an em: the OP-XY lays out its header text by them.
const int advance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560,
    560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745,
    602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589,
    574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
double width(const char* text, double px) {
    int total = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) return -1;
        total += advance[*c - 32];
    }
    return total * px / 1000.0;
}

flicker::DisplayState state;
F frame, before;
Input input;
void set(unsigned m, const int* c, unsigned row) {
    std::memset(&input, 0, sizeof input);
    input.track = 1;
    input.model = m;
    for (int i = 0; i < 8; ++i) input.slots[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
    for (unsigned i = 0; i < 4; ++i) {
        input.shown[i] = static_cast<opxy::u8>(row * 4 + i);
        input.parameters[i] = input.slots[row * 4 + i];
    }
    input.line = static_cast<opxy::u8>(row);
    input.elapsed = 34;   // 30 frames a second
    input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = envelope_unavailable;
}
// Nodes and outlines that changed since the last frame.
unsigned changed() {
    unsigned n = 0;
    for (unsigned i = 0; i < F::node_count; ++i)
        if (std::memcmp(&frame.nodes[i], &before.nodes[i], sizeof(Node))) ++n;
    for (unsigned p = 0; p < F::path_count; ++p)
        if (frame.paths[p].key != before.paths[p].key) ++n;
    before = frame;
    return n;
}
// Every visible text: values fit 53 px at the header's full 20 px (the OP-XY
// draws header values at full size, whatever their scale), titles 56 px.
std::set<std::string> seen;
void measure_texts(const char* when) {
    for (unsigned i = 0; i < 16; ++i) {
        const Text& t = frame.texts[i];
        const bool value = (i >= 4 && i < 8) || i >= 12;
        const double px = value ? 20.0 : 10.0 * t.scale / unit;
        const double w = width(t.chars, px);
        const double most = value ? 53.8 : 56.0;
        if (w < 0 || w > most) {
            std::printf("%s: text '%s' is %.1f px wide (at most %.0f)\n", when, t.chars, w, most);
            ++failures;
        }
        if (value && 20.0 * t.scale / unit < 8.0 && t.chars[0]) {
            std::printf("%s: text '%s' drawn under 8 px\n", when, t.chars);
            ++failures;
        }
        seen.insert(t.chars);
    }
}
unsigned peak = 0;
// A hit at 30 frames a second: the frames it moves for, and the changed nodes
// a frame on average over them.
void hit(unsigned frames, unsigned& moving, double& churn, const char* when, bool check) {
    flicker::display_reset(state);
    std::memset(&frame, 0, sizeof frame);
    flicker::display(state, input, frame);
    before = frame;
    for (int k = 0; k < 3; ++k) {
        flicker::display(state, input, frame);
        if (changed()) { std::printf("%s: the screen moves at rest\n", when); ++failures; break; }
    }
    const F rest = frame;
    input.note_serial += 1;
    input.held_notes = 1;
    unsigned total = 0;
    moving = 0;
    for (unsigned t = 0; t < frames; ++t) {
        if (t == 4) { input.held_notes = 0; input.note_off_serial += 1; }
        flicker::display(state, input, frame);
        if (!valid(frame)) { std::printf("%s frame %u: invalid\n", when, t); ++failures; }
        if (check) check_paths(frame, when);
        const unsigned n = changed();
        if (n) { total += n; moving = t + 1; }
        if (check && n > peak) peak = n;
    }
    churn = moving ? static_cast<double>(total) / moving : 0;
    // the film settles a whole frame on: it looks exactly as it did
    if (std::memcmp(frame.nodes, rest.nodes, sizeof frame.nodes)) { std::printf("%s: the film does not settle a frame on\n", when); ++failures; }
}
}

int main() {
    // every model, every control through all its values: outlines, validity, captions
    char when[96];
    for (unsigned m = 0; m < 12; ++m) {
        int c[8];
        for (unsigned s = 0; s < 8; ++s) {
            for (int v = 0; v <= 127; ++v) {
                for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[m].defaults[i];
                c[s] = v;
                set(m, c, s / 4);
                std::snprintf(when, sizeof when, "%s %s %d", flicker::view::names[m], flicker::view::labels[m][s], v);
                flicker::display_reset(state);
                std::memset(&frame, 0, sizeof frame);
                flicker::display(state, input, frame);
                if (!valid(frame)) { std::printf("%s: invalid\n", when); ++failures; }
                check_paths(frame, when);
                measure_texts(when);
                if (s == 0)
                    for (int tune = -16; tune <= 15; ++tune) {
                        input.pad[0] = static_cast<opxy::i8>(tune);
                        flicker::display_reset(state);
                        flicker::display(state, input, frame);
                        if (!valid(frame)) { std::printf("%s tune %d: invalid\n", when, tune); ++failures; }
                        check_paths(frame, when);
                        measure_texts(when);
                    }
            }
        }
        // SLIT through its values
        for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[m].defaults[i];
        for (int slit = -8; slit <= 7; ++slit) {
            set(m, c, 0);
            input.pad[1] = static_cast<opxy::i8>(slit);
            flicker::display_reset(state);
            flicker::display(state, input, frame);
            if (!valid(frame)) { std::printf("%s slit %d: invalid\n", flicker::view::names[m], slit); ++failures; }
        }
        // the Shift rows' captions
        for (unsigned row = 0; row < 2; ++row)
            for (int k = 0; k < 64; ++k) {
                set(m, c, row);
                input.shift = 1;
                input.pan = static_cast<opxy::i8>(k * 200 / 63 - 100);
                input.level = static_cast<opxy::i8>(-(k * 30 / 63));
                input.choices[1] = static_cast<opxy::u8>(k & 3);
                input.pad[0] = static_cast<opxy::i8>(k / 2 - 16);
                input.pad[1] = static_cast<opxy::i8>((k & 15) - 8);
                input.pad[2] = static_cast<opxy::i8>(k * 101 / 63);          // TRK, then percent + 1 (SDK 1.15)
                input.pad[3] = static_cast<opxy::i8>((63 - k) * 101 / 63);
                flicker::display_reset(state);
                flicker::display(state, input, frame);
                std::snprintf(when, sizeof when, "%s shift row %u", flicker::view::names[m], row);
                measure_texts(when);
            }
    }
    std::printf("%u texts measured\n", static_cast<unsigned>(seen.size()));

    // the Shift rows, as MD KIT shows them
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[0].defaults[i];
        set(0, c, 0);
        input.shift = 1;
        input.pan = -40;
        input.level = -6;
        input.choices[1] = 1;
        flicker::display_reset(state);
        flicker::display(state, input, frame);
        const char* want[4] = {"swp", "1", "-40", "-6dB"};
        const char* title[4] = {"machine", "choke", "pan", "level"};
        for (int i = 0; i < 4; ++i) {
            if (std::strcmp(frame.texts[4 + i].chars, want[i])) { std::printf("FAIL shift row 1 shows '%s' for '%s'\n", frame.texts[4 + i].chars, want[i]); ++failures; }
            if (std::strcmp(frame.texts[i].chars, title[i])) { std::printf("FAIL shift row 1 titled '%s' for '%s'\n", frame.texts[i].chars, title[i]); ++failures; }
        }
        const char* grey[4] = {"tune", "slit", "fx i", "fx ii"};
        for (int i = 0; i < 4; ++i)
            if (std::strcmp(frame.texts[8 + i].chars, grey[i])) { std::printf("FAIL shift row 2 grey titled '%s' for '%s'\n", frame.texts[8 + i].chars, grey[i]); ++failures; }
        set(0, c, 1);
        input.shift = 1;
        input.pan = -40;
        input.level = -6;
        input.choices[1] = 1;
        input.pad[0] = 7;
        input.pad[1] = -3;
        input.pad[2] = 0;
        input.pad[3] = 101;   // 100: percent + 1
        flicker::display_reset(state);
        flicker::display(state, input, frame);
        const char* want2[4] = {"+7", "-3", "TRK", "100"};
        for (int i = 0; i < 4; ++i) {
            if (std::strcmp(frame.texts[4 + i].chars, want2[i])) { std::printf("FAIL shift row 2 shows '%s' for '%s'\n", frame.texts[4 + i].chars, want2[i]); ++failures; }
            if (std::strcmp(frame.texts[i].chars, grey[i])) { std::printf("FAIL shift row 2 titled '%s' for '%s'\n", frame.texts[i].chars, grey[i]); ++failures; }
            if (std::strcmp(frame.texts[12 + i].chars, want[i])) { std::printf("FAIL shift row 1 grey shows '%s' for '%s'\n", frame.texts[12 + i].chars, want[i]); ++failures; }
        }
        // every model's name fits its column at full size
        for (unsigned mm = 0; mm < 12; ++mm) {
            set(mm, c, 0);
            input.shift = 1;
            flicker::display_reset(state);
            flicker::display(state, input, frame);
            if (std::strcmp(frame.texts[4].chars, flicker::view::names[mm])) { std::printf("FAIL model %u's name reads '%s'\n", mm, frame.texts[4].chars); ++failures; }
            measure_texts("a model's name");
        }
        set(7, c, 0);
        input.shift = 1;
        flicker::display_reset(state);
        flicker::display(state, input, frame);
        if (std::strcmp(frame.texts[4].chars, "orn")) { std::printf("FAIL ornament's name reads '%s'\n", frame.texts[4].chars); ++failures; }
    }

    // dead steps: every control walked in steps of 8 moves the drawing (its
    // shape or its ink's weight), the pencil guides, the gate (FLUTTER) or
    // the specks (DUST) at every step
    {
        unsigned dead = 0;
        for (unsigned m = 0; m < 12; ++m)
            for (unsigned s = 0; s < 8; ++s) {
                F last;
                for (int v = 0; v <= 128; v += 8) {
                    int c[8];
                    for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[m].defaults[i];
                    c[s] = v > 127 ? 127 : v;
                    set(m, c, 0);
                    flicker::display_reset(state);
                    std::memset(&frame, 0, sizeof frame);
                    flicker::display(state, input, frame);
                    // (DOT's GAP spaces its echoes: with none, as its default has, it has nothing to space)
                    const bool gapless = m == 4 && s == 4 && flicker::kit::model[m].defaults[3] == 0;
                    if (v && !gapless && !std::memcmp(frame.paths[0].points, last.paths[0].points, sizeof frame.paths[0].points) && !std::memcmp(frame.nodes + 12, last.nodes + 12, sizeof(Node) * (F::node_count - 12)) &&
                        !std::memcmp(frame.nodes, last.nodes, sizeof(Node) * F::path_count)) {
                        std::printf("dead step: %s %s %d\n", flicker::view::names[m], flicker::view::labels[m][s], c[s]);
                        ++dead;
                    }
                    last = frame;
                }
            }
        std::printf("%u dead steps of %u\n", dead, 12u * 8u * 16u);
        if (dead) ++failures;
    }

    // churn and motion: every model, each control at every eighth value and
    // its top, a hit followed for five seconds
    double worst_churn = 0, sum_churn = 0;
    unsigned runs = 0;
    for (unsigned m = 0; m < 12; ++m) {
        double model_sum = 0, model_worst = 0;
        unsigned model_runs = 0;
        for (int s = -1; s < 8; ++s)
            for (int v = 0; v <= 128; v += 8) {
                if (s < 0 && v) break;
                int c[8];
                for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[m].defaults[i];
                if (s >= 0) c[s] = v > 127 ? 127 : v;
                set(m, c, s >= 4 ? 1 : 0);
                std::snprintf(when, sizeof when, "%s %s %d", flicker::view::names[m], s >= 0 ? flicker::view::labels[m][s] : "defaults", s >= 0 ? c[s] : 0);
                unsigned moving;
                double churn;
                hit(150, moving, churn, when, true);
                if (moving < 5) { std::printf("%s: a hit moves the film for only %u frames\n", when, moving); ++failures; }
                if (moving >= 150) { std::printf("%s: the film never settles\n", when); ++failures; }
                if (churn > worst_churn) worst_churn = churn;
                if (churn > model_worst) model_worst = churn;
                sum_churn += churn;
                model_sum += churn;
                ++runs;
                ++model_runs;
                if (churn > 20) { std::printf("%s: %.1f nodes change a frame while it moves\n", when, churn); ++failures; }
            }
        std::printf("churn %-8s mean %.1f, worst %.1f nodes a frame\n", flicker::view::names[m], model_sum / model_runs, model_worst);
    }
    std::printf("churn: %.1f nodes a frame on average while the film moves, %.1f at the worst setting, %u in the busiest frame\n",
                sum_churn / runs, worst_churn, peak);
    if (peak > 32) { std::printf("the busiest frame changes %u nodes (at most 32)\n", peak); ++failures; }

    // the amp envelope moves nothing; reduced motion keeps the film still
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[0].defaults[i];
        unsigned plain, shorter, longer;
        double churn;
        set(0, c, 0);
        hit(150, plain, churn, "unknown envelope", false);
        set(0, c, 0);
        input.amp.attack = 0;
        input.amp.decay = 20 << 24;
        input.amp.sustain = 0;
        input.amp.release = 0x7f000000;
        hit(150, shorter, churn, "short envelope", false);
        set(0, c, 0);
        input.amp.attack = 0;
        input.amp.decay = 0x7fffffff;
        input.amp.sustain = 0x7fffffff;
        input.amp.release = 0;
        hit(150, longer, churn, "long envelope", false);
        std::printf("a hit moves for %u frames with no envelope, %u with a short one, %u with a long one\n", plain, shorter, longer);
        if (shorter != plain || longer != plain) { std::printf("FAIL the amp envelope changes the film's motion\n"); ++failures; }
        set(0, c, 0);
        input.reduced_motion = 1;
        unsigned still;
        hit(60, still, churn, "reduced motion", false);
        if (still > 0) { std::printf("FAIL reduced motion still moves the film for %u frames\n", still); ++failures; }
    }
    // a change of the held notes without a new note (another key) moves nothing
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[0].defaults[i];
        set(0, c, 0);
        flicker::display_reset(state);
        flicker::display(state, input, frame);
        before = frame;
        input.held_notes = 2;
        input.note_off_serial += 1;
        flicker::display(state, input, frame);
        if (changed()) { std::printf("FAIL another key's notes move the screen\n"); ++failures; }
    }
    // a second note mid-pull carries on from where the film is: nothing jumps
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = flicker::kit::model[0].defaults[i];
        set(0, c, 0);
        flicker::display_reset(state);
        flicker::display(state, input, frame);
        input.note_serial += 1;
        opxy::i32 last = frame.nodes[0].x;
        for (int t = 0; t < 90; ++t) {
            if (t == 6 || t == 9) input.note_serial += 1;
            flicker::display(state, input, frame);
            const opxy::i32 x = frame.nodes[0].x;
            // the drawing's node moves left, or wraps back by a whole frame
            // (a frame along the film is pitch_x across the screen)
            opxy::i32 d = last - x;
            if (d < 0) d += flicker::art::pitch_x;
            if (d > 20 * unit) { std::printf("FAIL the film jumps %d px at frame %d\n", d / unit, t); ++failures; }
            last = x;
        }
    }
    if (failures) { std::printf("%d failures\n", failures); return 1; }
    return 0;
}
