// ptsync-budget-check: Codex's review-14 probe (reproduktionen/patch39-budget-extra.cpp),
// adapted for patch 0040 (Claude Code): the estimate (plus the editor's 4 KiB bookkeeping) covers the measured peak.
// Host heap-peak probes for Patch 0039. Only requested allocation sizes are measured.
#include "audio-engine.h"
#include "common/platform_memory.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <array>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdio>

namespace {
bool measure = false;
struct Allocation { void* pointer = nullptr; size_t bytes = 0; };
std::array<Allocation, 64> allocations{};
size_t liveBytes = 0;
size_t peakBytes = 0;
}

void* operator new(size_t bytes) {
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        if (measure) {
            auto entry = std::find_if(allocations.begin(), allocations.end(),
                                      [](const Allocation& a) { return !a.pointer; });
            assert(entry != allocations.end());
            *entry = {memory, bytes};
            liveBytes += bytes;
            peakBytes = std::max(liveBytes, peakBytes);
        }
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    if (measure && memory) {
        auto entry = std::find_if(allocations.begin(), allocations.end(),
                                  [memory](const Allocation& a) { return a.pointer == memory; });
        if (entry != allocations.end()) {
            liveBytes -= entry->bytes;
            *entry = {};
        }
    }
    std::free(memory);
}

void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }

namespace {
constexpr int frames = 192000;
constexpr int fileRate = 48000;
constexpr int instrument = 1;

struct Sample {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), fileRate};
    pt::ui::AppState state;
    pt::ui::StdFileSystem fs;
    pt::ui::InputDispatcher dispatch{state, host, fs};
    std::filesystem::path wav;
    int slot = -1;

    Sample(const std::filesystem::path& path, bool stereo, int sourceRate = fileRate, int frameCount = frames) : fs(path.string()), wav(path / "input.wav") {
        engine->setDeviceSampleRate(fileRate);
        std::filesystem::create_directories(path);
        const int channels = stereo ? 2 : 1;
        std::vector<float> audio(frameCount * channels);
        for (int i = 0; i < frameCount; ++i) {
            audio[channels * i] = .51f * std::sin(.031f * i);
            if (stereo) audio[channels * i + 1] = .34f * std::sin(.037f * i);
        }
        songcore::WavStreamWriter writer(wav.string(), sourceRate, channels);
        assert(writer.is_open());
        writer.append_interleaved(audio.data(), frameCount);
        assert(writer.finish());
        auto project = songcore::make_default_project();
        project.instruments[instrument].sampleFilePath = wav.string();
        assert(host.push_project(songcore::serialize_project(project)));
        assert(host.load_media("").failed == 0);
        slot = host.project().instruments[instrument].sampleId;
        assert(host.sample_length(slot) == frameCount);
        state.project = &host.edit_project();
        state.currentInstrument = instrument;
        state.currentScreen = pt::ui::ScreenType::INSTRUMENT;
        dispatch.open_imported_sample_editor();
        assert(state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR);
    }

    void tap(int row, int column) {
        state.sampleEditor.cursorRow = row;
        state.sampleEditor.cursorCol = column;
        dispatch.on_button_a();
    }
};

void configure_sync(Sample& s, bool stretch, int duration, int pitch) {
    using pt::ui::SampleEditorModule;
    auto& editor = s.state.sampleEditor;
    editor.fxType = SampleEditorModule::FX_SYNC;
    editor.syncType = stretch ? SampleEditorModule::SYNC_TSTRETCH : SampleEditorModule::SYNC_RPITCH;
    editor.pitchSemitones = pitch;
    editor.stretchBpm = 120;
    editor.stretchGrainMs = 40;
    editor.durationIndex = duration;
    s.host.edit_project().tempo = 120;
}

void begin_measurement() {
    allocations.fill({});
    liveBytes = 0;
    peakBytes = 0;
    measure = true;
}

void editor_budget(const std::filesystem::path& root) {
    constexpr int input = 191996;
    constexpr int output = 96000;
    constexpr int64_t budget = 4 * 1024 * 1024;
    Sample s(root, true, 48000, input);
    configure_sync(s, true, 2, 12);
    songcore::SampleSync sync;
    sync.kind = songcore::SampleSync::Kind::STRETCH;
    sync.frames = output;
    sync.semitones = 12.f;
    sync.grainMs = 40.f;
    const auto estimate = songcore::sync_peak_bytes(input, 2, sync, 48000.f);
    const int64_t undo = int64_t(input) * 2 * sizeof(float);
    assert(setenv("PT_LOAD_BUDGET_MB", "4", 1) == 0);
    assert(pt::load_budget_bytes() == budget);
    begin_measurement();
    s.tap(16, 2);
    measure = false;
    unsetenv("PT_LOAD_BUDGET_MB");
    const auto& editor = s.state.sampleEditor;
    std::cout << "Editor: input " << input << ", after pitch 95998, output " << editor.totalFrames
              << ", estimate incl. Undo " << estimate + undo << ", measured " << peakBytes
              << ", budget " << budget << ", message [" << editor.toolMessage << "]\n";
    assert(editor.toolMessage.empty() && editor.totalFrames == output);
    assert(peakBytes <= static_cast<size_t>(estimate + undo + 4096) && peakBytes <= static_cast<size_t>(budget));
}

void matrix(const std::filesystem::path& root) {
    int cases = 0;
    int underestimated = 0;
    int growingShortcut = 0;
    int outsideShortcut = 0;
    int64_t maxGap = 0;
    for (int channels : {1, 2}) {
        for (int len : {9999, 48000}) {
            Sample s(root / (std::to_string(channels) + "-" + std::to_string(len)),
                     channels == 2, 48000, len);
            std::vector<float> left(len), right(channels == 2 ? len : 0);
            s.engine->getSampleData(s.slot, left.data());
            if (channels == 2) s.engine->getSampleDataRight(s.slot, right.data());
            for (float pitch : {-12.f, 0.f, 12.f}) {
                const int pitched = songcore::pitch_result_frames(len, pitch);
                const std::array<int, 12> targets = {
                    std::max(1, pitched / 20), std::max(1, pitched / 10 - 1),
                    std::max(1, pitched / 10), pitched / 2,
                    pitched - 2, pitched - 1, pitched, pitched + 1, pitched + 2,
                    pitched * 2, pitched * 10, pitched * 11
                };
                for (int target : targets) for (float grain : {10.f, 40.f, 120.f}) {
                    assert(s.host.load_sample(s.slot, s.wav.string()));
                    songcore::SampleSync sync;
                    sync.kind = songcore::SampleSync::Kind::STRETCH;
                    sync.frames = target;
                    sync.semitones = pitch;
                    sync.grainMs = grain;
                    const int64_t estimate = songcore::sync_peak_bytes(len, channels, sync, 48000.f);
                    begin_measurement();
                    const bool ok = s.engine->syncSample(s.slot, pitch, target, grain, 48000.f);
                    measure = false;
                    assert(ok);
                    ++cases;
                    if (peakBytes > static_cast<size_t>(estimate)) {
                        ++underestimated;
                        if (target == pitched + 1 || target == pitched + 2) ++growingShortcut;
                        else ++outsideShortcut;
                        maxGap = std::max(maxGap, static_cast<int64_t>(peakBytes) - estimate);
                        if (underestimated <= 3) {
                            std::cout << "Underestimate: " << channels << " ch, input " << len
                                      << ", pitch " << pitch << ", target " << target << ", grain "
                                      << grain << ", estimated " << estimate << ", measured " << peakBytes << '\n';
                        }
                    }
                }
            }
        }
    }
    std::cout << "Matrix: " << cases << " cases, " << underestimated << " underestimates, "
              << growingShortcut << " growing shortcuts, " << outsideShortcut
              << " other cases, max gap " << maxGap << " bytes\n";
    assert(underestimated == 0);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsync-budget.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    editor_budget(root / "editor");
    matrix(root / "matrix");
    std::filesystem::remove_all(root);
    std::puts("PASS: sync_peak_bytes (plus the editor bookkeeping) covers the measured peak in the editor and in 432 engine cases, growing SOLA shortcuts included");
}
