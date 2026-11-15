// ptsample-editor-preview-faults-check: Codex's review-14 probe (reproduktionen/patch39-preview-extra.cpp),
// adapted for patch 0040 (Claude Code): expects what KORREKTURAUFTRAG-CLAUDE-14 asks for.
// Host probes for additional sample preview paths after Patch 0039.
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
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdio>

namespace {
size_t failBytes = 0;
int skip = 0;
}

void* operator new[](size_t bytes) {
    if (failBytes && bytes == failBytes) {
        if (skip == 0) {
            failBytes = 0;
            throw std::bad_alloc{};
        }
        --skip;
    }
    return ::operator new(bytes);
}

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

constexpr int scratchSlot = songcore::SOURCE_PREVIEW_SLOT;
constexpr const char* refused = "PREVIEW CANCELLED: NOT ENOUGH MEMORY";

double energy(Sample& s, int blocks = 8) {
    double sum = 0;
    for (int n = 0; n < blocks; ++n) {
        float out[512]{};
        s.engine->processLiveBlock(out, 256, 2, 48000.f);
        for (float sample : out) sum += sample * sample;
    }
    return sum;
}

std::vector<float> channel(Sample& s, int slot, bool right = false) {
    std::vector<float> pcm(s.engine->getSampleLength(slot));
    if (right) s.engine->getSampleDataRight(slot, pcm.data());
    else s.engine->getSampleData(slot, pcm.data());
    return pcm;
}

void source_and_review(const std::filesystem::path& root) {
    for (int mode : {0, 1, 3}) {
        Sample s(root / ("source-" + std::to_string(mode)), true);
        auto& editor = s.state.sampleEditor;
        editor.sourceMode = mode;
        assert(s.engine->prepareSourcePreviewRange(scratchSlot, s.slot, 0, 0, 1024));
        const auto before = channel(s, scratchSlot);
        const auto gen = s.host.sample_generation(scratchSlot);
        failBytes = frames * sizeof(float);
        s.dispatch.on_start();
        assert(failBytes == 0 && editor.toolMessage == refused);
        assert(energy(s) == 0.0 && channel(s, scratchSlot) == before);
        assert(s.host.sample_generation(scratchSlot) == gen);
        s.dispatch.on_start();
        const auto output = channel(s, scratchSlot);
        const auto left = channel(s, s.slot);
        const auto right = channel(s, s.slot, true);
        assert(output.size() == left.size());
        for (size_t i = 0; i < output.size(); ++i) {
            const float want = mode == 0 ? left[i] : mode == 1 ? right[i] : (left[i] + right[i]) * 0.5f;
            assert(output[i] == want);
        }
        assert(energy(s) > 0.01 && editor.toolMessage.empty());   // the retry clears the refusal
        std::cout << "SOURCE " << mode << ": refusal silent, retry uses requested channel, message ["
                  << editor.toolMessage << "]\n";
    }

    Sample s(root / "hit-review", true);
    auto& editor = s.state.sampleEditor;
    editor.sourceMode = 1;
    editor.selectionStart = 0;
    editor.selectionEnd = 4000;
    auto& hit = editor.hitReview;
    hit.open = true;
    hit.instrument = s.slot;
    hit.first = 0;
    hit.end = 4000;
    hit.sourceMode = editor.sourceMode;
    hit.sensitivity = editor.sliceSensitivity;
    hit.snap = editor.snapEnabled;
    hit.generation = s.host.sample_generation(s.slot);
    hit.hits = {1000, 2000};
    hit.auditionSlice = 1;
    failBytes = frames * sizeof(float);
    s.dispatch.on_start();
    assert(failBytes == 0 && hit.open && editor.toolMessage == refused && energy(s) == 0.0);
    std::cout << "Hit Review RIGHT: refusal reported and silent\n";

    for (bool level : {false, true}) {
        Sample copy(root / (level ? "level" : "chop"), true);
        auto& e = copy.state.sampleEditor;
        e.selectionStart = 0;
        e.selectionEnd = 4096;
        if (level) {
            copy.tap(15, pt::ui::SampleEditorModule::LEVEL_COL);
            assert(e.levelEdit.open());
        } else {
            e.sliceMethod = pt::ui::SampleEditorModule::SLICE_DIVIDE;
            e.sliceDivisions = 8;
            copy.tap(19, 3);
            assert(e.chopReview.open());
        }
        failBytes = (level ? 4096 : frames / 8) * sizeof(float);
        skip = 1; // right scratch channel
        copy.dispatch.on_start();
        assert(failBytes == 0 && e.toolMessage == "NOT ENOUGH RAM FOR PREVIEW");
        assert(energy(copy) == 0.0);
        std::cout << (level ? "LEVEL" : "CHOP") << ": right copy refusal reported and silent\n";
    }
}

void pending_preview(const std::filesystem::path& root) {
    Sample s(root, true);
    auto& editor = s.state.sampleEditor;
    editor.sourceMode = 0;
    s.dispatch.on_start(); // schedules the note 100 frames ahead; no audio callback yet
    assert(editor.toolMessage.empty());
    failBytes = frames * sizeof(float);
    s.dispatch.on_start();
    assert(failBytes == 0 && editor.toolMessage == refused);
    const double sound = energy(s);
    std::cout << "Failed START with earlier queued note: audio energy " << sound << '\n';
    assert(sound < 0.00001);   // the queued note never starts
}

void fx_faults(const std::filesystem::path& root) {
    for (int side : {0, 1}) {
        Sample s(root / ("backup-" + std::to_string(side)), true);
        auto& editor = s.state.sampleEditor;
        editor.fxType = pt::ui::SampleEditorModule::FX_DRIVE;
        editor.fxValue = 220;
        const auto left = channel(s, s.slot), right = channel(s, s.slot, true);
        const auto bytes = s.engine->audio_memory_bytes();
        failBytes = frames * sizeof(float);
        skip = side;
        bool escaped = false;
        try { s.dispatch.on_start(); } catch (const std::bad_alloc&) { escaped = true; }
        const auto after = s.engine->audio_memory_bytes();
        std::cout << "FX backup channel " << side << ": exception " << escaped
                  << ", reported PCM heap delta " << after - bytes
                  << ", message [" << editor.toolMessage << "]\n";
        assert(!escaped && failBytes == 0 && editor.toolMessage == refused && after == bytes);
        assert(channel(s, s.slot) == left && channel(s, s.slot, true) == right && energy(s) == 0.0);
    }

    Sample s(root / "source-copy", true);
    auto& editor = s.state.sampleEditor;
    editor.sourceMode = 1;
    editor.fxType = pt::ui::SampleEditorModule::FX_DRIVE;
    editor.fxValue = 220;
    const auto before = channel(s, s.slot), rightBefore = channel(s, s.slot, true);
    const auto gen = s.host.sample_generation(s.slot);
    failBytes = frames * sizeof(float);
    skip = 2; // FX backup L/R succeed, then SOURCE RIGHT scratch copy fails
    s.dispatch.on_start();
    assert(failBytes == 0 && editor.toolMessage == refused && energy(s) == 0.0);
    // The clean source is back at once, with the refusal.
    assert(channel(s, s.slot) == before && channel(s, s.slot, true) == rightBefore);
    s.dispatch.set_now(200);
    std::cout << "FX then refused SOURCE copy: source restored at once, generation +"
              << s.host.sample_generation(s.slot) - gen << '\n';
    assert(channel(s, s.slot) == before && channel(s, s.slot, true) == rightBefore);
    s.dispatch.on_button_b(); // exit; restores the backup
    assert(channel(s, s.slot) == before && channel(s, s.slot, true) == rightBefore);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-preview-faults.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    source_and_review(root / "copy-paths");
    pending_preview(root / "pending");
    fx_faults(root / "fx");
    std::filesystem::remove_all(root);
    std::puts("PASS: refused preview copies (SOURCE, Hit Review, CHOP, LEVEL) are reported and silent, a retry clears the refusal; a queued audition never starts after a refusal; a refused FX backup or audition copy is reported with the source PCM clean at once");
}
