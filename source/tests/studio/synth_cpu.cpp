// CPU cost of every Studio synth model: 9 voices (8 tracks + preview) rendered as the engine does,
// one Synth::next per output frame, 256-frame blocks at 48 kHz. Prints the share of one core's
// real time per model, sorted. Host numbers are a ranking, not a Brick budget: run the same binary
// built for aarch64 on the device (with its own release) for the A53 figures.
//   ptsynth-cpu-bench [seconds per model = 1.0] [voices = 9]
#include "synth.h"
#include "synth_params.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace studio;

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 1.0;
    const int voices = argc > 2 ? std::max(1, std::atoi(argv[2])) : 9;
    constexpr float rate = 48000.f;
    constexpr int block = 256;
    const int blocks = std::max(1, int(seconds * rate / block));
    struct Row { int model; double load; };
    std::vector<Row> rows;
    std::vector<std::unique_ptr<Synth>> synths;
    for (int v = 0; v < voices; ++v) synths.push_back(std::make_unique<Synth>());
    volatile float sink = 0;
    for (int model = 0; model < model_count(); ++model) {
        const int code = preset_code(model, 0);
        for (int v = 0; v < voices; ++v) { synths[v]->start(code); synths[v]->update(code, default_synth_params()); }
        // A chord spread over three octaves, re-struck every second like a busy pattern.
        auto hz = [](int v) { return 110.f * std::pow(2.f, float((v * 7) % 36) / 12.f); };
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            if (b % int(rate / block) == 0 && b)
                for (int v = 0; v < voices; ++v) synths[v]->start(code);
            for (int v = 0; v < voices; ++v) {
                synths[v]->update(code, default_synth_params());
                float acc = 0;
                for (int i = 0; i < block; ++i) acc += synths[v]->next(hz(v), rate);
                sink = sink + acc;
            }
        }
        const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        rows.push_back({model, used / (blocks * double(block) / rate)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.load > b.load; });
    std::printf("# %d voices, %d-frame blocks, %.0f Hz, %.2f s per model\n# load = share of one core's real time\n", voices, block, rate, seconds);
    for (const auto& r : rows) std::printf("%6.2f %%  %3d  %s\n", 100 * r.load, r.model, model_name(r.model));
    double total = 0; for (const auto& r : rows) total += r.load;
    std::printf("# mean %.2f %%, max %.2f %% (%s)\n", 100 * total / rows.size(), 100 * rows.front().load, model_name(rows.front().model));
    return sink == 12345.f;   // keep the renders
}
