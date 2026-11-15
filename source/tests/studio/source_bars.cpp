// ptsource-bars-check: the Sources screen's Bar length row sets OUT a whole number of 4/4 bars after
// IN at the song's tempo (source_sampler.h, SOURCE_BARS_ROW).
// usage: ptsource-bars-check <new run directory>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop
#include "ui/source_sampler.h"

namespace {
namespace fs = std::filesystem;

struct FixedBackend : pt::ui::SourceBackend {
    std::string snapshot;
    std::vector<std::string> commands;
    bool command(const std::string& action, const std::string& args) override {
        commands.push_back(action + " " + args);
        return true;
    }
    std::string state_json() override { return snapshot; }
};

void tap(pt::ui::Button b, pt::ui::SourceSamplerState& s, Sample& sample) {
    pt::ui::ButtonEvent e;
    e.button = b;
    e.action = pt::ui::ButtonAction::PRESSED;
    pt::ui::source_sampler_button(e, s, sample.host, *sample.engine);
    if (b == pt::ui::Button::A) {
        e.action = pt::ui::ButtonAction::RELEASED;
        pt::ui::source_sampler_button(e, s, sample.host, *sample.engine);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || fs::exists(argv[1])) {
        std::cerr << "usage: ptsource-bars-check <new run directory>\n";
        return 2;
    }
    assert(pt::ui::source_sampler_bars_ms(1, 120) == 2000);
    assert(pt::ui::source_sampler_bars_ms(2, 128) == 3750);
    assert(pt::ui::source_sampler_bars_ms(4, 90) == 10667);   // 10 666.7 rounded
    assert(pt::ui::source_sampler_bars_ms(8, 60) == 32000);
    assert(pt::ui::source_sampler_bars_ms(1, 0) == 240000);    // tempo 0 counts as 1, no division by zero

    Sample sample(fs::path(argv[1]), true, 48000, 256, 1);
    sample.host.edit_project().tempo = 128;
    FixedBackend backend;
    backend.snapshot = songcore::json{{"status", "ready"}, {"title", "Loop"}, {"duration_ms", 30000},
                                      {"in_ms", 5000}, {"out_ms", 6000}}.dump();
    auto& s = sample.state.sourceSampler;
    s.backend = &backend;
    assert(pt::ui::source_sampler_poll(s, 1000));
    s.row = pt::ui::SOURCE_BARS_ROW;

    // Default 2 bars at 128 BPM: OUT = 5000 + 3750.
    tap(pt::ui::Button::A, s, sample);
    assert(backend.commands.back() == R"(set_region {"in_ms":5000,"out_ms":8750})");
    // Right: 4 bars = 7500 ms.
    tap(pt::ui::Button::DPAD_RIGHT, s, sample);
    assert(s.barLengthIndex == 2);
    tap(pt::ui::Button::A, s, sample);
    assert(backend.commands.back() == R"(set_region {"in_ms":5000,"out_ms":12500})");
    // 8 bars at 40 BPM = 48 s: within 180 s, but not within the 25 s left after IN.
    tap(pt::ui::Button::DPAD_RIGHT, s, sample);
    tap(pt::ui::Button::DPAD_RIGHT, s, sample);   // stays at the last length
    assert(s.barLengthIndex == 3);
    sample.host.edit_project().tempo = 40;
    const auto sent = backend.commands.size();
    tap(pt::ui::Button::A, s, sample);
    assert(backend.commands.size() == sent && s.status == "Not enough audio after IN; move IN earlier.");
    // A live station hides the row and refuses.
    backend.snapshot = songcore::json{{"status", "ready"}, {"title", "Radio"}, {"live", true}}.dump();
    s.hasPolled = false;
    assert(pt::ui::source_sampler_poll(s, 2000));
    tap(pt::ui::Button::A, s, sample);
    assert(backend.commands.size() == sent);
    s.backend = nullptr;
    std::cout << "ptsource-bars-check: 1/2/4/8 bars at the song tempo set OUT after IN; limits refused\n";
    return 0;
}
