// ptsample-editor-memory-check: Codex's review-11 probe (reproduktionen/patch36-editor-extra.cpp),
// adapted for patch 0037 and run as a check. Memory refusals, markers across Undo, and a failed
// replacement buffer, through the real sample-editor dispatcher on generated WAVs.
// Adapted for patch 0037 (Claude Code): every case now expects the behaviour KORREKTURAUFTRAG-CLAUDE-11
// asks for — refusals that change nothing, markers back after a whole-length CROP, in-place Undo
// keeping the current markers, and a failed replacement buffer (either channel) reported, not thrown.
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
#include <vector>
#include <unistd.h>
#include <string>
#include <cstdio>

namespace {
size_t failArrayBytes = 0;
int failSkip = 0;   // matching allocations to let through first (1 = fail the right channel)
}

void* operator new[](size_t bytes) {
    if (bytes == failArrayBytes && failArrayBytes != 0) {
        if (failSkip > 0) { --failSkip; return ::operator new(bytes); }
        failArrayBytes = 0;
        throw std::bad_alloc{};
    }
    return ::operator new(bytes);
}

void operator delete[](void* memory) noexcept {
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

    Sample(const std::filesystem::path& path, bool stereo, int sourceRate = fileRate) : fs(path.string()), wav(path / "input.wav") {
        engine->setDeviceSampleRate(fileRate);
        std::filesystem::create_directories(path);
        const int channels = stereo ? 2 : 1;
        std::vector<float> audio(frames * channels);
        for (int i = 0; i < frames; ++i) {
            audio[channels * i] = .51f * std::sin(.031f * i);
            if (stereo) audio[channels * i + 1] = .34f * std::sin(.037f * i);
        }
        songcore::WavStreamWriter writer(wav.string(), sourceRate, channels);
        assert(writer.is_open());
        writer.append_interleaved(audio.data(), frames);
        assert(writer.finish());
        auto project = songcore::make_default_project();
        project.instruments[instrument].sampleFilePath = wav.string();
        assert(host.push_project(songcore::serialize_project(project)));
        assert(host.load_media("").failed == 0);
        slot = host.project().instruments[instrument].sampleId;
        assert(host.sample_length(slot) == frames);
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

void budget_case(const std::filesystem::path& root, int column, const char* name,
                 int selected, int wantFrames) {
    Sample s(root / name, true);
    select(s, 0, selected);
    if (column == 4) s.tap(13, 1);
    const auto initial = s.engine->audio_memory_bytes();
    const int clipboard = s.host.clipboard_length();
    assert(setenv("PT_LOAD_BUDGET_MB", "2", 1) == 0);
    assert(pt::load_budget_bytes() == 2 * 1024 * 1024);
    s.tap(13, column);
    const auto added = s.engine->audio_memory_bytes() - initial;
    std::cout << name << ": " << s.state.sampleEditor.totalFrames << " frames; added "
              << added << " bytes, message [" << s.state.sampleEditor.toolMessage << "]\n";
    unsetenv("PT_LOAD_BUDGET_MB");
    (void)wantFrames;   // what it would have made: the edit plus its backup exceed 2 MiB
    assert(s.state.sampleEditor.totalFrames == frames && added == 0 && s.host.clipboard_length() == clipboard);
    assert(s.state.sampleEditor.toolMessage == "EDIT CANCELLED: UNDO MEMORY UNAVAILABLE");
}

void resampling_budget(const std::filesystem::path& root, int mode) {
    const bool save = mode == 2;
    Sample s(root, true);
    auto& editor = s.state.sampleEditor;
    assert(setenv("PT_LOAD_BUDGET_MB", "2", 1) == 0);
    assert(pt::load_budget_bytes() == 2 * 1024 * 1024);
    if (save) {
        editor.pitchSemitones = -12;
        s.tap(19, 2);
        // Refused before anything changed: still in the editor, the pitch still pending.
        assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR && editor.pitchSemitones == -12);
        assert(editor.toolMessage == "SAVE CANCELLED: NOT ENOUGH MEMORY FOR PITCH");
        unsetenv("PT_LOAD_BUDGET_MB");
        Sample readback(root / "readback", false);
        assert(readback.host.load_sample(readback.slot, s.wav.string()));
        assert(readback.host.sample_length(readback.slot) == frames);   // the file as it was
    } else {
        using pt::ui::SampleEditorModule;
        editor.fxType = SampleEditorModule::FX_SYNC;
        editor.syncType = mode == 0 ? SampleEditorModule::SYNC_RPITCH : SampleEditorModule::SYNC_TSTRETCH;
        editor.stretchBpm = 120;
        const auto& durations = SampleEditorModule::duration_values();
        const auto bars = std::find(durations.begin(), durations.end(), "4 BAR");
        assert(bars != durations.end());
        editor.durationIndex = static_cast<int>(bars - durations.begin());
        s.host.edit_project().tempo = 120;
        s.tap(16, 2);
        assert(editor.toolMessage == "EDIT CANCELLED: UNDO MEMORY UNAVAILABLE");
    }
    std::cout << (save ? "OVERWRITE PITCH -12" : mode == 0 ? "SYNC RPITCH" : "SYNC TSTRETCH") << ": "
              << editor.totalFrames << " frames; refused [" << editor.toolMessage << "]\n";
    assert(editor.totalFrames == frames);
    unsetenv("PT_LOAD_BUDGET_MB");
}

void same_length_crop(const std::filesystem::path& root) {
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    add_marker(s, 144000);
    assert(editor.marker_count() == 1 && editor.marker_position(0) == 144000);
    select(s, 0, frames);
    s.tap(13, 0);
    assert(editor.totalFrames == frames && editor.marker_count() == 0);
    s.tap(14, 5);
    std::cout << "Whole CROP -> UNDO: " << editor.totalFrames << " frames, "
              << editor.marker_count() << " markers\n";
    assert(editor.totalFrames == frames && editor.marker_count() == 1 && editor.marker_position(0) == 144000);
    s.tap(19, 2);
    const auto cues = songcore::read_cue_points(s.wav.string());
    assert(cues.size() == 1 && cues.front() == 144000);
}

void marker_edits(const std::filesystem::path& root) {
    Sample s(root / "in-place", false);
    auto& editor = s.state.sampleEditor;
    add_marker(s, 144000);
    select(s, 0, 4096);
    s.tap(14, 1); // FADE+
    add_marker(s, 48000);
    s.tap(14, 5);
    std::cout << "FADE+ -> ADD @S -> UNDO: " << editor.marker_count()
              << " markers, first " << editor.marker_position(0) << '\n';
    assert(editor.marker_count() == 2 && editor.marker_position(0) == 48000);

    Sample r(root / "rate", false);
    auto& rate = r.state.sampleEditor;
    add_marker(r, 144000);
    select(r, 0, 4096);
    r.tap(14, 1); // FADE+ backup at HIGH
    rate.cursorRow = 1;
    rate.cursorCol = 2;
    r.dispatch.on_a_right(); // NORM
    assert(rate.totalFrames == 96000 && rate.marker_position(0) == 72000);
    rate.sliceIndex = 1;
    r.tap(12, 1); // REMOVE the previously scaled marker
    add_marker(r, 24000);
    assert(rate.marker_count() == 1 && rate.marker_position(0) == 24000);
    r.tap(14, 5);
    std::cout << "FADE+ -> NORM -> REMOVE -> ADD @S -> UNDO: " << rate.totalFrames
              << " frames, " << rate.marker_count() << " marker, first "
              << rate.marker_position(0) << '\n';
    // In-place Undo keeps the markers being worked on; the length moved back, so they moved with it.
    assert(rate.totalFrames == frames && rate.marker_count() == 1 && rate.marker_position(0) == 48000);
}

void failed_replacement(const std::filesystem::path& root, int column, const char* name, int replacement,
                        int channel) {
    Sample s(root / (std::string(name) + "-" + std::to_string(channel)), true);
    auto& editor = s.state.sampleEditor;
    select(s, 0, 1024);
    s.tap(13, 1);
    const float original = s.host.sample_level_peak(s.slot, 0, 4096);
    select(s, 0, 4096);
    s.tap(14, 3); // SLNC with Undo
    assert(original > 0.0f && s.host.sample_level_peak(s.slot, 0, 4096) == 0.0f);
    add_marker(s, 144000);
    select(s, 0, 120000);
    failArrayBytes = size_t(replacement) * sizeof(float);
    failSkip = channel;
    bool escaped = false;
    try {
        s.tap(13, column);
    } catch (const std::bad_alloc&) {
        escaped = true;
    }
    assert(!escaped && failArrayBytes == 0);
    assert(editor.toolMessage == "EDIT CANCELLED: NOT ENOUGH MEMORY");
    assert(s.host.sample_length(s.slot) == frames && editor.totalFrames == frames);
    assert(editor.selectionStart == 0 && editor.selectionEnd == 120000 && editor.marker_count() == 1);
    const int clipboard = s.host.clipboard_length();
    s.tap(14, 5);
    const float peakAfterUndo = s.host.sample_level_peak(s.slot, 0, 4096);
    std::cout << name << " channel " << channel << ": reported; clipboard 1024 -> " << clipboard
              << "; peak after Undo " << peakAfterUndo << " (original " << original << ")\n";
    assert(clipboard == 1024 && peakAfterUndo == original);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-memory.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    budget_case(root, 0, "CROP", 120000, 120000);
    budget_case(root, 4, "PASTE", 120000, 312000);
    budget_case(root, 5, "DEL", 72000, 120000);
    resampling_budget(root / "repitch", 0);
    resampling_budget(root / "stretch", 1);
    resampling_budget(root / "save-pitch", 2);
    same_length_crop(root / "same-length");
    marker_edits(root / "markers");
    for (int channel : {0, 1}) {
        failed_replacement(root / "allocation", 2, "CUT", 72000, channel);
        failed_replacement(root / "allocation", 3, "DUPL", 312000, channel);
    }
    std::filesystem::remove_all(root);
    std::puts("PASS: CROP/PASTE/DEL/SYNC and a pitched save refused within the budget with nothing changed; whole-length CROP Undo restores its markers; in-place Undo keeps the current ones; a failed replacement buffer (CUT/DUPL, either channel) is reported with clipboard, audio, markers and Undo kept");
}
