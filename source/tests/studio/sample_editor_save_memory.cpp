// ptsample-editor-save-memory-check: Codex's review-17 probe (reproduktionen/patch42-save-memory.cpp),
// adapted for patch 0043 (Claude Code): a refused save is reported, nothing escapes, and WAV and project cues always agree.
// Host probes for allocation failures in WAV saving after a successful pitch.
#include "audio-engine.h"
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
#include <fstream>
#include <iterator>
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

void* operator new(size_t bytes) {
    if (failBytes && bytes == failBytes) {
        if (skip == 0) {
            failBytes = 0;
            throw std::bad_alloc{};
        }
        --skip;
    }
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
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

    Sample(const std::filesystem::path& path, bool stereo, int sourceRate = fileRate, int frameCount = frames, int fileMarkers = 0) : fs(path.string()), wav(path / "input.wav") {
        engine->setDeviceSampleRate(fileRate);
        std::filesystem::create_directories(path);
        const int channels = stereo ? 2 : 1;
        std::vector<float> left(frameCount), right(stereo ? frameCount : 0);
        for (int i = 0; i < frameCount; ++i) {
            left[i] = .51f * std::sin(.031f * i);
            if (stereo) right[i] = .34f * std::sin(.037f * i);
        }
        std::vector<int> cues;
        for (int n = 1; n <= fileMarkers; ++n) cues.push_back(n * 8);
        assert(songcore::write_wav(wav.string(), left, right, sourceRate, cues, channels));
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

std::vector<char> file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void refused_save_pcm(const std::filesystem::path& root, bool wavBuffer) {
    Sample s(root, true, 48000, frames, 17);
    auto& se = s.state.sampleEditor;
    const auto originalWav = file_bytes(s.wav);
    se.pitchSemitones = 12;
    const auto generation = s.host.sample_generation(s.slot);
    constexpr int pitchedFrames = frames / 2;
    if (wavBuffer) {
        failBytes = 44 + pitchedFrames * 2 * sizeof(int16_t) + 12 + 17 * 24;
        skip = 0;  // WAV vector reserve before replacing the file
    } else {
        failBytes = pitchedFrames * sizeof(float);
        skip = 4;  // two pitch work buffers and two published PCM buffers
    }
    bool escaped = false;
    try { s.tap(19, 2); } catch (const std::bad_alloc&) { escaped = true; }
    const bool sameWav = file_bytes(s.wav) == originalWav;
    std::cout << (wavBuffer ? "WAV byte buffer" : "Save PCM plane") << " refused: escaped " << escaped
              << ", frames " << se.totalFrames << ", modified " << se.isModified
              << ", unchanged file " << sameWav << ", message [" << se.toolMessage
              << "], status [" << s.state.statusMessage << "]\n";
    assert(!escaped && failBytes == 0 && se.totalFrames == pitchedFrames && se.isModified && sameWav);
    assert(se.toolMessage == "SAVE FAILED: NOT ENOUGH MEMORY" && s.state.statusMessage == se.toolMessage);
    assert(s.host.sample_generation(s.slot) == generation + 1 && se.pitchSemitones == 0);
    assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR);
    assert(se.fileMarkers.size() == 17 && se.fileMarkers.front() == 4);

    s.tap(19, 2); // retry must not apply the same pitch twice
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR);
    assert(s.host.sample_length(s.slot) == pitchedFrames);
    assert(songcore::read_cue_points(s.wav.string()).front() == 4);
}

void refused_project_cues(const std::filesystem::path& root) {
    Sample s(root, true, 48000, frames);
    auto& se = s.state.sampleEditor;
    for (int i = 1; i <= 17; ++i) {
        se.selectionStart = i * 8;
        se.selectionEnd = frames;
        s.tap(12, 0); // ADD @S
    }
    assert(se.manualMarkers.size() == 17);
    assert(s.host.project().instruments[instrument].sliceMarkers.empty());
    se.pitchSemitones = 12;
    failBytes = 17 * sizeof(int64_t); // exact reserve for the project after the WAV was replaced
    skip = 0;
    bool escaped = false;
    try { s.tap(19, 2); } catch (const std::bad_alloc&) { escaped = true; }
    const auto cues = songcore::read_cue_points(s.wav.string());
    const auto projectCues = s.host.project().instruments[instrument].sliceMarkers.size();
    std::cout << "Project cue reserve refused: escaped " << escaped << ", file cues " << cues.size()
              << ", project cues " << projectCues << ", modified " << se.isModified << '\n';
    // Prepared before the file is replaced: refused there, so file and project both still have none.
    assert(!escaped && failBytes == 0 && cues.empty() && projectCues == 0);
    assert(se.isModified && s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR);
    assert(se.toolMessage == "SAVE FAILED: NOT ENOUGH MEMORY");
    s.tap(19, 2);   // the retry saves both, together
    const auto saved = songcore::read_cue_points(s.wav.string());
    const auto& project = s.host.project().instruments[instrument].sliceMarkers;
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR);
    assert(saved.size() == 17 && saved.front() == 4 && project.size() == 17 && project.front() == 4);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-save-memory.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    refused_save_pcm(root / "pcm", false);
    refused_save_pcm(root / "wav", true);
    refused_project_cues(root / "project");
    std::filesystem::remove_all(root);
    std::puts("PASS: a refused PCM copy, WAV buffer or project-cue preparation fails the save with a message, keeps the old file and the edited unsaved audio; a retry saves WAV and project cues together");
}
