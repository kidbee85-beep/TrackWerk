// Sample-editor edges from Codex reviews 6 and 7 (reproduktionen/patch24-editor-edges.cpp, adapted
// for patch 0025), on generated WAVs in a scratch directory and the real dispatcher:
//   CUT/DUPL with a refused backup keep PCM, generation, Undo and the clipboard;
//   slice markers follow a RATE step, so SEL SL, CHOP and SAVE see valid bounds;
//   SEL SL on 16 DIVIDE slices of a 3-frame buffer selects at least one frame.
#include "audio-engine.h"
#include "common/platform_memory.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/scene_presets.h"
#include "ui/std_filesystem.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <new>
#include <vector>
#include <unistd.h>

namespace {
// Refuses the `skip`+1-th nothrow array allocation of exactly `refusedBytes` (0 = none): the left
// or the right channel of a clipboard copy, as on a device that has run out of memory.
size_t refusedBytes = 0;
int refusedSkip = 0, refusedCount = 0;
}

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept {
    if (refusedBytes && bytes == refusedBytes) {
        if (refusedSkip > 0) { --refusedSkip; }
        else { refusedBytes = 0; ++refusedCount; return nullptr; }
    }
    try { return ::operator new[](bytes); } catch (const std::bad_alloc&) { return nullptr; }
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

std::vector<float> pcm(Sample& s) {
    std::vector<float> out(s.host.sample_length(s.slot));
    s.engine->getSampleData(s.slot, out.data());
    return out;
}

void refused_backup(const std::filesystem::path& root, int column, const char* label) {
    Sample s(root, true);
    auto& editor = s.state.sampleEditor;
    editor.selectionStart = 0;
    editor.selectionEnd = 1024;
    s.tap(13, 1);  // original clipboard
    editor.selectionStart = 0;
    editor.selectionEnd = 4096;
    const auto original = pcm(s);
    s.tap(14, 3);  // prior Undo
    const auto before = pcm(s);
    assert(before != original);
    editor.selectionStart = 8192;
    editor.selectionEnd = 10240;
    const auto generation = s.host.sample_generation(s.slot);
    assert(setenv("PT_LOAD_BUDGET_MB", "1", 1) == 0);
    assert(pt::load_budget_bytes() == 1024 * 1024);
    s.tap(13, column);
    unsetenv("PT_LOAD_BUDGET_MB");
    assert(editor.toolMessage == "EDIT CANCELLED: UNDO MEMORY UNAVAILABLE");
    assert(pcm(s) == before && s.host.sample_generation(s.slot) == generation);
    std::cout << label << " refused: clipboard 1024 -> " << s.host.clipboard_length() << " frames\n";
    assert(s.host.clipboard_length() == 1024);
    s.tap(14, 5);
    assert(pcm(s) == original);
}

// Codex review 8: a copy that cannot be allocated (either stereo channel) cancels CUT/DUPL with a
// message, before the backup: clipboard, audio, generation, selection, markers and Undo stay.
void refused_copy(const std::filesystem::path& root, int column, int channel) {
    Sample s(root, true);
    auto& editor = s.state.sampleEditor;
    const auto original = pcm(s);
    editor.selectionStart = 0; editor.selectionEnd = 1024;
    s.tap(13, 1);                                    // clipboard: 1024 frames
    s.tap(14, 3);                                    // SILENCE: the Undo to keep
    const auto before = pcm(s);
    editor.selectionStart = 144000; editor.selectionEnd = frames;
    s.tap(12, 0);                                    // ADD @S: a manual marker
    assert(editor.marker_count() == 1);
    editor.selectionStart = 8192; editor.selectionEnd = 10240;
    const auto generation = s.host.sample_generation(s.slot);
    const int count = refusedCount;
    refusedBytes = 2048 * sizeof(float); refusedSkip = channel;
    s.tap(13, column);
    refusedBytes = 0; refusedSkip = 0;
    assert(refusedCount == count + 1);
    assert(editor.toolMessage == "EDIT CANCELLED: NOT ENOUGH MEMORY TO COPY");
    assert(s.host.clipboard_length() == 1024 && pcm(s) == before && s.host.sample_generation(s.slot) == generation);
    assert(editor.selectionStart == 8192 && editor.selectionEnd == 10240 && editor.marker_count() == 1);
    s.tap(14, 5);
    assert(pcm(s) == original);
    std::cout << (column == 2 ? "CUT" : "DUPL") << " with channel " << channel << " refused: kept\n";
}

// Slice numbers (patch 0027): DIVIDE 8 tags all eight slices at the foot of the wave, the one under
// the cursor filled in the cursor colour; OFF draws none; 512 slices are too narrow for any tag.
void slice_tags(const std::filesystem::path& root) {
    using pt::ui::SampleEditorModule;
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    s.state.theme = pt::ui::scene_preset(24, pt::ui::VisualizerType::SCOPE);
    pt::ui::TrackerLayout layout;
    auto draw = [&](int method, int divisions) {
        editor.sliceMethod = method; editor.sliceDivisions = divisions; editor.sliceIndex = 2;
        editor.cursorRow = 11; editor.cursorCol = 0;
        auto c = std::make_unique<pt::ui::Canvas>(); layout.draw(*c, s.state); return c;
    };
    const auto off = draw(SampleEditorModule::SLICE_OFF, 8);
    const auto eight = draw(SampleEditorModule::SLICE_DIVIDE, 8);
    const auto again = draw(SampleEditorModule::SLICE_DIVIDE, 8);
    const auto narrow = draw(SampleEditorModule::SLICE_DIVIDE, 512);
    const auto narrowAgain = draw(SampleEditorModule::SLICE_DIVIDE, 512);
    int cursorInk = 0, changed = 0;
    for (int i = 0; i < pt::ui::DESIGN_W * pt::ui::DESIGN_H; ++i) {
        cursorInk += eight->pixels()[i] == s.state.theme.rowCursor;
        changed += eight->pixels()[i] != off->pixels()[i];
    }
    assert(changed > 500 && cursorInk > 50);
    for (int i = 0; i < pt::ui::DESIGN_W * pt::ui::DESIGN_H; ++i) assert(eight->pixels()[i] == again->pixels()[i]);
    for (int i = 0; i < pt::ui::DESIGN_W * pt::ui::DESIGN_H; ++i) assert(narrow->pixels()[i] == narrowAgain->pixels()[i]);
    std::cout << "Slice tags: " << changed << " pixels differ from OFF, " << cursorInk << " in the cursor colour\n";
}

// Codex review 9 · CUT/DUPL hold the staged copy, the backup and the new buffer at once: the sum
// must fit. At 2 MiB each part fits, the sum does not, so CUT is refused and nothing is left behind.
void staged_budget(const std::filesystem::path& root) {
    Sample s(root, true);
    const auto initial = s.engine->audio_memory_bytes();
    auto& editor = s.state.sampleEditor;
    editor.selectionStart = 0;
    editor.selectionEnd = 120000;
    assert(setenv("PT_LOAD_BUDGET_MB", "2", 1) == 0);
    assert(s.host.stage_copy(s.slot, 0, 120000));
    assert(!s.host.backup_sample(s.slot));   // 1 536 000 + 960 000 > 2 MiB
    s.host.discard_copy();
    assert(s.engine->audio_memory_bytes() == initial);
    s.tap(13, 2);   // CUT through the dispatcher
    unsetenv("PT_LOAD_BUDGET_MB");
    assert(editor.toolMessage == "EDIT CANCELLED: UNDO MEMORY UNAVAILABLE");
    assert(editor.totalFrames == frames && s.host.clipboard_length() == 0);
    assert(s.engine->audio_memory_bytes() == initial);
    std::cout << "CUT at 2 MiB refused as a whole, nothing left behind\n";
}

// Codex review 9 · TSTRETCH from its box keeps the scaled marker; Undo brings back the marker of
// the audio it restores, and the saved cue follows.
void stretch_markers(const std::filesystem::path& root, bool undo) {
    using pt::ui::SampleEditorModule;
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    editor.selectionStart = 144000;
    editor.selectionEnd = frames;
    s.tap(12, 0);   // ADD @S
    assert(editor.marker_count() == 1 && editor.marker_position(0) == 144000);
    s.host.edit_project().tempo = 120;
    const auto& durations = SampleEditorModule::duration_values();
    const auto bar = std::find(durations.begin(), durations.end(), "1 BAR");
    assert(bar != durations.end());
    editor.durationIndex = static_cast<int>(bar - durations.begin());
    editor.fxType = SampleEditorModule::FX_SYNC;
    editor.syncType = SampleEditorModule::SYNC_TSTRETCH;
    editor.stretchBpm = 120;
    s.tap(16, 1);
    assert(editor.stretchOpen);
    editor.stretchCursor = pt::ui::StretchCell::APPLY;
    s.dispatch.on_button_a();
    assert(!editor.stretchOpen && editor.totalFrames == 96000 && editor.marker_position(0) == 72000);
    if (undo) {
        s.tap(14, 5);
        assert(editor.totalFrames == frames && editor.marker_count() == 1 && editor.marker_position(0) == 144000);
    }
    s.tap(19, 2);   // OVERWRITE
    const auto cues = songcore::read_cue_points(s.wav.string());
    assert(cues.size() == 1 && cues.front() == (undo ? 144000 : 72000));
    std::cout << "TSTRETCH" << (undo ? " + Undo" : "") << ": cue at " << cues.front() << '\n';
}

void stale_markers(const std::filesystem::path& root) {
    using pt::ui::SampleEditorModule;
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    editor.selectionStart = 144000;
    editor.selectionEnd = frames;
    s.tap(12, 0);  // ADD @S, a manual marker at 3 s of the 4 s WAV
    assert(editor.sliceMethod == SampleEditorModule::SLICE_MANUAL);
    assert(editor.marker_count() == 1 && editor.marker_position(0) == 144000);

    editor.cursorRow = 1;
    editor.cursorCol = 2;
    s.dispatch.on_a_right();  // NORM, output 96000 frames at 24 kHz
    assert(editor.rateMode == 1 && editor.totalFrames == 96000 && editor.sampleRate == 24000);
    s.tap(12, 2);  // SEL SL
    std::cout << "NORM after 3-second marker: " << editor.marker_position(0) << " of "
              << editor.totalFrames << " frames; SEL SL " << editor.selectionStart << ".."
              << editor.selectionEnd << '\n';
    s.tap(19, 3);  // CHOP, with slicing enabled
    std::cout << "CHOP review " << editor.chopReview.slices.size() << " slices; message "
              << editor.toolMessage << '\n';
    // Patch 0025: the marker follows NORM to 72000, so CHOP offers both slices and SAVE keeps the cue.
    assert(editor.marker_position(0) == 72000 && editor.selectionStart == 72000 && editor.selectionEnd == 96000);
    assert(editor.chopReview.open() && editor.chopReview.slices.size() == 2);
    editor.chopReview = {};   // close the review without exporting
    s.tap(19, 2);  // OVERWRITE the temporary WAV
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR);
    const auto cues = songcore::read_cue_points(s.wav.string());
    std::cout << "Saved WAV cue count " << cues.size() << (cues.empty() ? "" : " at " + std::to_string(cues[0])) << '\n';
    assert(cues.size() == 1);
}

void sync_whole_buffer(const std::filesystem::path& root) {
    using pt::ui::SampleEditorModule;
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    s.host.edit_project().tempo = 120;
    const auto& durations = SampleEditorModule::duration_values();
    const auto bar = std::find(durations.begin(), durations.end(), "1 BAR");
    assert(bar != durations.end());
    editor.durationIndex = static_cast<int>(bar - durations.begin());
    editor.fxType = SampleEditorModule::FX_SYNC;
    editor.syncType = SampleEditorModule::SYNC_RPITCH;
    editor.selectionStart = 1024;
    editor.selectionEnd = 1025;
    s.tap(16, 2);
    assert(s.host.sample_length(s.slot) == 96000);
    assert(editor.selectionStart == 0 && editor.selectionEnd == 96000);
    std::cout << "SYNC processes the full 192000-frame buffer despite a one-frame selection\n";
}

void divide_short(const std::filesystem::path& root) {
    using pt::ui::SampleEditorModule;
    Sample s(root, false);
    auto& editor = s.state.sampleEditor;
    editor.selectionStart = 0;
    editor.selectionEnd = 3;
    s.tap(13, 0);  // crop to 3 frames
    assert(editor.totalFrames == 3);
    editor.sliceMethod = SampleEditorModule::SLICE_DIVIDE;
    editor.sliceDivisions = 16;  // valid setting, larger than the buffer
    editor.sliceIndex = 0;
    s.tap(12, 2);  // SEL SL
    std::cout << "Three frames / 16 slices, first selection " << editor.selectionStart
              << ".." << editor.selectionEnd << '\n';
    assert(editor.selectionEnd > editor.selectionStart);   // Patch 0025: at least one frame
    s.tap(19, 3);
    assert(editor.chopReview.open());
    std::cout << "CHOP filters empty slices: " << editor.chopReview.slices.size() << " remain\n";
    for (const auto [a, b] : editor.chopReview.slices)
        assert(a >= 0 && a < b && b <= 3);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, made new for this run inside the one given (mkdtemp: never an
    // existing one), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-edges.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    refused_backup(root / "cut", 2, "CUT");
    refused_backup(root / "dupl", 3, "DUPL");
    for (const int column : {2, 3}) for (const int channel : {0, 1})
        refused_copy(root / ("copy-" + std::to_string(column) + "-" + std::to_string(channel)), column, channel);
    staged_budget(root / "budget");
    stretch_markers(root / "stretch", false);
    stretch_markers(root / "undo-stretch", true);
    slice_tags(root / "tags");
    stale_markers(root / "markers");
    divide_short(root / "divide");
    sync_whole_buffer(root / "sync");
    std::filesystem::remove_all(root);
    std::puts("PASS: CUT/DUPL with a refused backup or copy (either channel) keep clipboard, PCM, selection, markers and Undo; markers follow RATE (SEL SL, CHOP, SAVE cue); SEL SL on a 3-frame DIVIDE keeps one frame; SYNC takes the whole buffer; slice numbers tag each slice wide enough; CUT budget counts copy, backup and new buffer together; Undo after TSTRETCH restores its markers; own run directory");
}
