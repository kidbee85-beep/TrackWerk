// ptsample-editor-preview-check: Codex's review-13 probe (reproduktionen/patch38-preview-extra.cpp),
// adapted for patch 0039 (Claude Code): expects what KORREKTURAUFTRAG-CLAUDE-13 asks for.
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
#include <thread>
#include <chrono>
#include <atomic>
#include <array>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdio>

namespace {
std::atomic<bool> allocationEntered{false}, releaseAllocation{false};
size_t failedScalarBytes = 0;
size_t pausedScalarBytes = 0;
bool failPausedAllocation = false;
bool measure = false;
struct Allocation { void* pointer = nullptr; size_t bytes = 0; };
std::array<Allocation, 64> allocations{};
size_t liveBytes = 0;
size_t peakBytes = 0;
}

void* operator new(size_t bytes) {
    if (failedScalarBytes != 0 && bytes == failedScalarBytes) {
        failedScalarBytes = 0;
        throw std::bad_alloc{};
    }
    if (pausedScalarBytes != 0 && bytes == pausedScalarBytes) {
        pausedScalarBytes = 0;
        allocationEntered.store(true);
        while (!releaseAllocation.load()) std::this_thread::yield();
        if (failPausedAllocation) throw std::bad_alloc{};
    }
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

double block_energy(AudioEngine& engine) {
    float out[512]{};
    engine.processLiveBlock(out, 256, 2, 48000.f);
    double energy = 0;
    for (float sample : out) energy += sample * sample;
    return energy;
}

void preview_faults(const std::filesystem::path& root) {
    constexpr int scratch = songcore::SOURCE_PREVIEW_SLOT;
    Sample repitch(root / "repitch", true);
    configure_sync(repitch, false, 2, 0);
    const auto repitchGen = repitch.host.sample_generation(repitch.slot);
    failedScalarBytes = 96000 * sizeof(float);
    repitch.dispatch.on_start();
    assert(failedScalarBytes == 0);
    const auto& editor = repitch.state.sampleEditor;
    const int repitchLength = repitch.engine->getSampleLength(scratch);
    double previewEnergy = 0;
    for (int block = 0; block < 8; ++block) previewEnergy = block_energy(*repitch.engine);
    std::cout << "Preview RPITCH refusal: " << repitchLength << " frames, generation source +"
              << repitch.host.sample_generation(repitch.slot) - repitchGen
              << ", message [" << editor.toolMessage << "], audio energy " << previewEnergy << '\n';
    assert(repitchLength == frames && repitch.host.sample_generation(repitch.slot) == repitchGen);
    assert(editor.toolMessage == "PREVIEW CANCELLED: NOT ENOUGH MEMORY" && previewEnergy < 0.00001);

    Sample stretch(root / "stretch", true);
    configure_sync(stretch, true, 0, 12);
    const auto stretchGen = stretch.host.sample_generation(stretch.slot);
    failedScalarBytes = (384000 + 1920) * sizeof(float);
    bool escaped = false;
    try { stretch.dispatch.on_start(); } catch (const std::bad_alloc&) { escaped = true; }
    const int stretchLength = stretch.engine->getSampleLength(scratch);
    double stretchEnergy = 0;
    for (int block = 0; block < 8; ++block) stretchEnergy = block_energy(*stretch.engine);
    std::cout << "Preview TSTRETCH refusal: exception " << escaped
              << ", scratch " << stretchLength << " frames, source "
              << stretch.host.sample_length(stretch.slot) << " frames, message ["
              << stretch.state.sampleEditor.toolMessage << "], audio energy " << stretchEnergy << "\n";
    assert(!escaped && failedScalarBytes == 0 && stretchLength == frames && stretchEnergy < 0.00001);
    assert(stretch.state.sampleEditor.toolMessage == "PREVIEW CANCELLED: NOT ENOUGH MEMORY");
    assert(stretch.host.sample_length(stretch.slot) == frames);
    assert(stretch.host.sample_generation(stretch.slot) == stretchGen);
}

void overestimated_budget(const std::filesystem::path& root) {
    Sample s(root, true);
    configure_sync(s, true, 2, 0); // 192000 -> 96000
    assert(setenv("PT_LOAD_BUDGET_MB", "3", 1) == 0);
    assert(pt::load_budget_bytes() == 3 * 1024 * 1024);
    auto& editor = s.state.sampleEditor;
    measure = true;
    s.tap(16, 2);
    measure = false;
    unsetenv("PT_LOAD_BUDGET_MB");
    std::cout << "TSTRETCH 1 BAR at 120 BPM: passed at 3 MiB, "
              << "peak additional requested heap bytes " << peakBytes << '\n';
    assert(editor.totalFrames == 96000 && editor.toolMessage.empty() && peakBytes < 3 * 1024 * 1024);
}

void playback_lock(const std::filesystem::path& root, bool refuse) {
    Sample s(root, false);
    const auto before = s.host.sample_generation(s.slot);
    constexpr float c4 = 261.6256f;
    auto& engine = *s.engine;
    engine.requestResume();
    engine.scheduleNote(engine.getCurrentFrame() + 100, s.slot, 0, c4, c4, 0.7f);
    double normal = 0;
    for (int n = 0; n < 8; ++n) normal = block_energy(engine);
    assert(normal > 0.01);
    allocationEntered.store(false);
    releaseAllocation.store(false);
    pausedScalarBytes = (96000 + 1920) * sizeof(float);
    // The same allocation is paused before returning, or pauses and then fails.
    std::thread editor([&] {
        songcore::SampleSync sync;
        sync.kind = songcore::SampleSync::Kind::STRETCH;
        sync.frames = 96000;
        const bool ok = s.host.sync_sample(s.slot, sync);
        assert(ok == !refuse);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!allocationEntered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    if (!allocationEntered.load()) {
        releaseAllocation.store(true);
        editor.join();
        assert(false && "SOLA allocation was not reached");
    }
    double during = 0;
    for (int n = 0; n < 8; ++n) during = block_energy(engine);
    failPausedAllocation = refuse;
    releaseAllocation.store(true);
    editor.join();
    double after = 0;
    for (int n = 0; n < 8; ++n) after = block_energy(engine);
    std::cout << "Live callback " << (refuse ? "on failure" : "on success") << ": energy "
              << normal << " / " << during << " / " << after << ", generation +"
              << s.host.sample_generation(s.slot) - before << '\n';
    assert(during < 0.00001);
    assert(refuse ? after > 0.01 : after < 0.00001);
    assert(s.host.sample_generation(s.slot) - before == (refuse ? 0 : 1));
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-preview.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    preview_faults(root / "preview");
    overestimated_budget(root / "budget");
    playback_lock(root / "lock-success", false);
    playback_lock(root / "lock-failure", true);
    std::filesystem::remove_all(root);
    std::puts("PASS: a refused SYNC audition (RPITCH, or TSTRETCH after its pitch) plays nothing, says so and leaves the source; TSTRETCH 1 BAR passes at 3 MiB with the measured peak inside the budget; the audio callback never waits on the edit lock");
}
