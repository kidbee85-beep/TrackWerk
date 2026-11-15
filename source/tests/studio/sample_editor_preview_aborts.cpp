// ptsample-editor-preview-aborts-check: Codex's review-15 probe (reproduktionen/patch40-preview-aborts.cpp),
// adapted for patch 0041 (Claude Code): closing the editor ends a queued audition too.
// Host probe for queued auditions in the sample editor and review screens.
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


void enter_review(Sample& s, int mode) {
    auto& se = s.state.sampleEditor;
    se.selectionStart = 0;
    se.selectionEnd = 4096;
    if (mode == 0) {
        se.sliceMethod = pt::ui::SampleEditorModule::SLICE_DIVIDE;
        se.sliceDivisions = 8;
        s.tap(19, 3);
        assert(se.chopReview.open());
    } else if (mode == 1) {
        s.tap(15, pt::ui::SampleEditorModule::LEVEL_COL);
        assert(se.levelEdit.open());
    } else {
        se.sourceMode = 1;
        auto& review = se.hitReview;
        review.open = true;
        review.instrument = s.slot;
        review.first = 0;
        review.end = 4096;
        review.sourceMode = se.sourceMode;
        review.sensitivity = se.sliceSensitivity;
        review.snap = se.snapEnabled;
        review.generation = s.host.sample_generation(s.slot);
        review.hits = {1000, 2000};
        review.auditionSlice = 1;
    }
}

void review_abort(const std::filesystem::path& root, int mode, bool copyFailure) {
    Sample s(root, true);
    enter_review(s, mode);
    auto& se = s.state.sampleEditor;
    s.dispatch.on_start();
    assert(se.toolMessage.empty());
    if (copyFailure) {
        failBytes = (mode == 0 ? frames / 8 : mode == 1 ? 4096 : frames) * sizeof(float);
        skip = mode == 2 ? 0 : 1;
        s.dispatch.on_start();
        assert(failBytes == 0);
        assert(se.toolMessage == (mode == 2 ? refused : "NOT ENOUGH RAM FOR PREVIEW"));
    } else {
        s.dispatch.on_button_b();
        assert(!se.chopReview.open() && !se.levelEdit.open() && !se.hitReview.open);
    }
    const double result = energy(s);
    std::cout << (mode == 0 ? "CHOP" : mode == 1 ? "LEVEL" : "Hit Review")
              << (copyFailure ? " copy refusal" : " B") << " with queued note: energy " << result << '\n';
    assert(result == 0.0);
}

void ordinary_stop(const std::filesystem::path& root) {
    Sample s(root, true);
    s.dispatch.on_start(); // direct STEREO preview, no scratch copy
    s.dispatch.on_button_b();
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR);
    const double result = energy(s);
    std::cout << "Sample Editor B after queued STEREO START: energy " << result << '\n';
    assert(result == 0.0);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-preview-aborts.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    for (int mode : {0, 1, 2}) {
        review_abort(root / ("refusal-" + std::to_string(mode)), mode, true);
        review_abort(root / ("back-" + std::to_string(mode)), mode, false);
    }
    ordinary_stop(root / "close");
    std::filesystem::remove_all(root);
    std::puts("PASS: CHOP, LEVEL, Hit Review (refused copy or B) and closing the editor after a queued STEREO START leave nothing playing");
}
