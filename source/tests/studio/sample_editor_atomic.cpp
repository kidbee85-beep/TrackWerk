// ptsample-editor-atomic-check: Codex's review-12 probe (reproduktionen/patch37-editor-extra.cpp),
// adapted for patch 0038 (Claude Code): every case expects what KORREKTURAUFTRAG-CLAUDE-12 asks for.
// Host probes for Patch 0037. Pass a new scratch directory for WAV fixtures.
#include "audio-engine.h"
#include "common/platform_memory.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include "ui/input_dispatcher.h"
#include "ui/live_resample.h"
#include "ui/std_filesystem.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <fstream>
#include <iterator>
#include <vector>
#include <unistd.h>
#include <string>
#include <cstdio>

namespace {
size_t failArrayBytes = 0;
int failSkip = 0;
size_t failScalarBytes = 0;
size_t trackArrayBytes = 0;
void* trackedArray = nullptr;
}

void* operator new(size_t bytes) {
    if (bytes == failScalarBytes && failScalarBytes != 0) {
        failScalarBytes = 0;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }

void* operator new[](size_t bytes) {
    if (bytes == failArrayBytes && failArrayBytes != 0) {
        if (failSkip == 0) {
            failArrayBytes = 0;
            throw std::bad_alloc{};
        }
        --failSkip;
    }
    void* memory = ::operator new(bytes);
    if (bytes == trackArrayBytes && trackArrayBytes != 0) trackedArray = memory;
    return memory;
}

void operator delete[](void* memory) noexcept {
    if (memory == trackedArray) trackedArray = nullptr;
    ::operator delete(memory);
}

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

void select(Sample& s, int first, int last) {
    s.state.sampleEditor.selectionStart = first;
    s.state.sampleEditor.selectionEnd = last;
}

void add_marker(Sample& s, int frame) {
    select(s, frame, s.state.sampleEditor.totalFrames);
    s.tap(12, 0);
}

void configure_stretch(Sample& s, const std::string& duration, int pitch) {
    using pt::ui::SampleEditorModule;
    auto& editor = s.state.sampleEditor;
    editor.fxType = SampleEditorModule::FX_SYNC;
    editor.syncType = SampleEditorModule::SYNC_TSTRETCH;
    editor.pitchSemitones = pitch;
    editor.stretchBpm = 120;
    const auto& durations = SampleEditorModule::duration_values();
    const auto it = std::find(durations.begin(), durations.end(), duration);
    assert(it != durations.end());
    editor.durationIndex = static_cast<int>(it - durations.begin());
}

void marker_rollback(const std::filesystem::path& root) {
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    add_marker(s, 144000);
    select(s, 0, frames);
    s.tap(13, 0); // same-length CROP; its Undo should restore the marker
    assert(editor.marker_count() == 0 && editor.totalFrames == frames);
    select(s, 0, 120000);
    failArrayBytes = 120000 * sizeof(float);
    s.tap(13, 0); // rejected CROP should leave the earlier Undo available
    assert(failArrayBytes == 0 && editor.toolMessage == "EDIT CANCELLED: NOT ENOUGH MEMORY");
    s.tap(14, 5);
    std::cout << "CROP -> rejected CROP -> UNDO: " << editor.marker_count() << " markers\n";
    assert(editor.totalFrames == frames && editor.marker_count() == 1 && editor.marker_position(0) == 144000);
    s.tap(19, 2);
    const auto cues = songcore::read_cue_points(s.wav.string());
    assert(cues.size() == 1 && cues.front() == 144000);
}

void marker_snapshot_failure(const std::filesystem::path& root) {
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    select(s, 0, 4096);
    const float original = s.host.sample_level_peak(s.slot, 0, 4096);
    s.tap(14, 3); // prior Undo stores the unmuted audio
    assert(s.host.sample_level_peak(s.slot, 0, 4096) == 0.0f);
    add_marker(s, 144000);
    select(s, 0, 120000);
    const auto before = s.engine->audio_memory_bytes();
    failScalarBytes = sizeof(pt::ui::SliceMarker); // copy of exactly one manual marker into undoMarkers
    bool escaped = false;
    try { s.tap(13, 0); } catch (const std::bad_alloc&) { escaped = true; }
    const auto after = s.engine->audio_memory_bytes();
    const std::string message = s.state.sampleEditor.toolMessage;
    s.tap(14, 5);
    const float peak = s.host.sample_level_peak(s.slot, 0, 4096);
    std::cout << "Marker snapshot failure: escaped " << escaped << ", held +"
              << after - before << " bytes, peak after Undo " << peak << " (original " << original << ")\n";
    assert(!escaped && failScalarBytes == 0 && after == before && peak == original);
    assert(message == "EDIT CANCELLED: NOT ENOUGH MEMORY");
}

void partial_sync(const std::filesystem::path& root) {
    Sample s(root, true);
    auto& editor = s.state.sampleEditor;
    add_marker(s, 144000);
    configure_stretch(s, "4 BAR", 12);
    const auto gen = s.host.sample_generation(s.slot);
    // At 48 kHz, 40 ms gives a 1920-frame chunk. Refuse the first SOLA output vector after Pitch.
    failScalarBytes = (384000 + 1920) * sizeof(float);
    s.tap(16, 2);
    const int actual = s.host.sample_length(s.slot);
    std::cout << "SYNC Pitch +12 then SOLA refusal: engine " << actual << ", editor "
              << editor.totalFrames << ", pitch " << editor.pitchSemitones << ", marker "
              << editor.marker_position(0) << ", generation +" << s.host.sample_generation(s.slot) - gen
              << ", message [" << editor.toolMessage << "]\n";
    assert(failScalarBytes == 0 && editor.toolMessage == "EDIT CANCELLED: NOT ENOUGH MEMORY");
    assert(actual == frames && editor.totalFrames == frames && editor.marker_position(0) == 144000);
    assert(editor.pitchSemitones == 12 && s.host.sample_generation(s.slot) == gen);
}

std::vector<char> file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void pitch_right_failure(const std::filesystem::path& root) {
    Sample s(root, true);
    auto& editor = s.state.sampleEditor;
    editor.cursorRow = 1;
    editor.cursorCol = 2;
    s.dispatch.on_a_right(); // NORM
    assert(editor.totalFrames == 96000 && s.host.sample_rate_factor(s.slot) == 2);
    editor.pitchSemitones = -12;
    const auto originalWav = file_bytes(s.wav);
    failArrayBytes = 192000 * sizeof(float);
    trackArrayBytes = failArrayBytes;
    failSkip = 1; // only the second channel of the pending pitch fails
    const auto gen = s.host.sample_generation(s.slot);
    s.tap(19, 2);
    const int rate = s.host.sample_rate_factor(s.slot);
    std::cout << "OVERWRITE right pitch failure: RATE engine " << rate << ", editor "
              << editor.rateMode << ", generation +" << s.host.sample_generation(s.slot) - gen
              << ", outstanding left array " << (trackedArray ? trackArrayBytes : 0) << " bytes"
              << ", message [" << editor.toolMessage << "]\n";
    assert(editor.toolMessage == "SAVE CANCELLED: NOT ENOUGH MEMORY FOR PITCH");
    assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR && editor.pitchSemitones == -12);
    assert(rate == 2 && editor.rateMode == 1 && trackedArray == nullptr);
    assert(s.host.sample_generation(s.slot) == gen && editor.totalFrames == 96000);
    assert(file_bytes(s.wav) == originalWav);
    trackArrayBytes = 0;
}

void clamped_stretch_budget(const std::filesystem::path& root) {
    constexpr int inputFrames = 256000;
    Sample s(root, true, fileRate, inputFrames);
    configure_stretch(s, "1/32", 0);
    assert(setenv("PT_LOAD_BUDGET_MB", "2", 1) == 0);
    assert(pt::load_budget_bytes() == 2097152);
    s.tap(16, 2);
    unsetenv("PT_LOAD_BUDGET_MB");
    const int output = s.host.sample_length(s.slot);
    std::cout << "SYNC 1/32 at 120 BPM, 2 MiB: engine " << output
              << " frames; Undo and replacement at least " << (inputFrames + output) * 8 << " bytes\n";
    assert(output == inputFrames && s.state.sampleEditor.toolMessage == "EDIT CANCELLED: UNDO MEMORY UNAVAILABLE");
}

void editor_entry_points(const std::filesystem::path& root) {
    Sample s(root, true);
    using namespace pt::ui;
    s.state.currentScreen = ScreenType::INSTRUMENT;
    s.state.studioOpen = true;
    s.state.studioTab = 0;
    s.state.sampleGranular.open = true;
    s.state.sampleGranular.request = SampleGranularRequest::CROP_SOURCE;
    s.dispatch.set_now(0);
    assert(s.state.sampleGranular.cropReturn && s.state.currentScreen == ScreenType::SAMPLE_EDITOR);
    select(s, 0, 120000);
    failArrayBytes = 120000 * sizeof(float);
    s.tap(13, 0);
    assert(failArrayBytes == 0 && s.state.sampleEditor.toolMessage == "EDIT CANCELLED: NOT ENOUGH MEMORY");
    assert(s.state.sampleGranular.cropReturn && s.state.sampleEditor.totalFrames == frames);
    s.dispatch.on_button_b();
    assert(s.state.studioOpen && s.state.sampleGranular.open);
    std::cout << "Granular Crop refusal: source and Studio return kept\n";

    s.state.studioOpen = false;
    s.state.resampleView.frames = frames;
    s.state.resampleView.path = s.wav.string();
    live_resample_use(s.state, s.host, *s.engine, s.fs);
    assert(s.state.pendingSampleEditor);
    s.state.pendingSampleEditor = false;
    s.dispatch.open_imported_sample_editor();
    auto& editor = s.state.sampleEditor;
    select(s, 0, 1024);
    s.tap(13, 1);
    failArrayBytes = (frames + 1024) * sizeof(float);
    s.tap(13, 4);
    assert(failArrayBytes == 0 && editor.toolMessage == "EDIT CANCELLED: NOT ENOUGH MEMORY");
    assert(editor.totalFrames == frames && !s.state.sampleGranular.cropReturn && s.host.clipboard_length() == 1024);
    std::cout << "Live resample take PASTE refusal: length, clipboard and editor kept\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-atomic.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    marker_rollback(root / "marker-rollback");
    marker_snapshot_failure(root / "snapshot");
    partial_sync(root / "partial-sync");
    pitch_right_failure(root / "pitch-save");
    clamped_stretch_budget(root / "stretch-budget");
    editor_entry_points(root / "entry-points");
    std::filesystem::remove_all(root);
    std::puts("PASS: a refused CROP keeps the earlier Undo with its markers; a refused marker snapshot changes nothing; SYNC pitch+stretch publish together or not at all; a refused pitched save keeps RATE, generation and no orphan; SOLA's real length is budgeted; Granular Crop and Live-Resample editor refusals");
}
