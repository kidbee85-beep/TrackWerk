// ptsample-editor-save-io-check: Codex's review-18 probe (reproduktionen/patch43-save-io.cpp),
// adapted for patch 0044 (Claude Code): a failed rename puts the original back; a failed mono reload is reported and the slot follows the file.
// Host-only WAV write and post-save reload probes, using synthetic files.
#include "audio-engine.h"
#include "common/byte_source.h"
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
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <cstdio>

namespace {
size_t failBytes = 0;
size_t trackedArrayBytes = 0;
void* trackedArray = nullptr;
}

void* operator new(size_t bytes) {
    if (failBytes && bytes == failBytes) {
        failBytes = 0;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void* operator new[](size_t bytes) {
    void* memory = ::operator new(bytes);
    if (trackedArrayBytes && bytes == trackedArrayBytes) trackedArray = memory;
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


void assert_save_failed(Sample& s) {
    const auto& se = s.state.sampleEditor;
    assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR);
    assert(se.isModified && se.toolMessage == "SAVE FAILED");
    assert(s.state.statusMessage == "SAVE FAILED");
}

void write_failure(const std::filesystem::path& root, int length, bool opening) {
    Sample s(root, true, 48000, length, 1);
    const auto original = file_bytes(s.wav);
    s.state.sampleEditor.isModified = true;
    const auto tmp = s.wav.string() + ".tmp";
    if (opening) {
        // No temp can be created: the folder is read-only (since patch 0049 every local save writes
        // a temp of its own name, so an entry at "<path>.tmp" no longer blocks it).
        const auto folder = s.wav.parent_path();
        assert(::chmod(folder.c_str(), 0500) == 0);
        s.tap(19, 2);
        assert(::chmod(folder.c_str(), 0700) == 0);
    } else {
        struct rlimit oldLimit{}, limit{};
        assert(getrlimit(RLIMIT_FSIZE, &oldLimit) == 0);
        limit = oldLimit;
        limit.rlim_cur = 512;
        const auto oldSignal = std::signal(SIGXFSZ, SIG_IGN);
        assert(oldSignal != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0);
        s.tap(19, 2);
        assert(setrlimit(RLIMIT_FSIZE, &oldLimit) == 0);
        assert(std::signal(SIGXFSZ, oldSignal) != SIG_ERR);
        assert(!std::filesystem::exists(tmp));
    }
    assert_save_failed(s);
    assert(file_bytes(s.wav) == original);
    assert(s.host.project().instruments[instrument].sliceMarkers == std::vector<int64_t>{8});
    std::cout << "Write " << (opening ? "open refused" : length < 1024 ? "buffered file-size limit"
                                                                       : "large file-size limit")
              << ": target byte-identical, editor still open\n";
}

std::string hookTarget, hookTemp;
constexpr const char* uriTarget = "test://input.wav";
constexpr const char* uriTemp = "test://input.wav.tmp";
constexpr const char* uriBak = "test://input.wav.ptbak";
std::string hookBak;
bool originalRemoved = false;
bool tempComplete = false;
int renameAttempts = 0;

int file_open(const char* path, const char* mode) {
    const bool write = std::strcmp(mode, "wb") == 0;
    const char* local = std::strcmp(path, uriTemp) == 0 ? hookTemp.c_str()
                      : std::strcmp(path, uriBak) == 0  ? hookBak.c_str() : hookTarget.c_str();
    return ::open(local, write ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0600);
}

int file_remove(const char* path) {
    if (std::strcmp(path, uriTemp) == 0) return std::remove(hookTemp.c_str());
    if (std::strcmp(path, uriBak) == 0) return std::remove(hookBak.c_str());
    assert(std::strcmp(path, uriTarget) == 0);
    const int rc = std::remove(hookTarget.c_str());
    originalRemoved = rc == 0;
    return rc;
}

int refuse_rename(const char* from, const char* to) {
    // Moving the original aside and back works; only publishing the temp fails.
    if (std::strcmp(from, uriTarget) == 0 && std::strcmp(to, uriBak) == 0)
        return std::rename(hookTarget.c_str(), hookBak.c_str());
    if (std::strcmp(from, uriBak) == 0 && std::strcmp(to, uriTarget) == 0)
        return std::rename(hookBak.c_str(), hookTarget.c_str());
    assert(std::strcmp(from, uriTemp) == 0 && std::strcmp(to, uriTarget) == 0);
    ++renameAttempts;
    tempComplete = std::filesystem::file_size(hookTemp) > 44 &&
                   songcore::read_cue_points(hookTemp) == std::vector<int>{4};
    errno = EIO;
    return -1;
}

void rename_failure(const std::filesystem::path& root) {
    Sample s(root, true, 48000, frames, 1);
    hookTarget = s.wav.string();
    hookTemp = hookTarget + ".tmp";
    hookBak = hookTarget + ".ptbak";
    const auto before = file_bytes(s.wav);
    assert(!before.empty());
    auto& se = s.state.sampleEditor;
    se.sampleFilePath = uriTarget;
    s.host.edit_project().instruments[instrument].sampleFilePath = uriTarget;
    se.pitchSemitones = 12;
    const PtFileHooks previous = pt_file_hooks();
    pt_set_file_hooks({file_open, file_remove, refuse_rename});
    s.tap(19, 2);
    pt_set_file_hooks(previous);
    assert_save_failed(s);
    const auto& ins = s.host.project().instruments[instrument];
    assert(ins.sampleFilePath == uriTarget && ins.sliceMarkers == std::vector<int64_t>{8});
    assert(renameAttempts == 1 && tempComplete && !originalRemoved);
    assert(file_bytes(s.wav) == before && !std::filesystem::exists(hookTemp) && !std::filesystem::exists(hookBak));
    assert(se.totalFrames == frames / 2 && se.fileMarkers == std::vector<int>{4} && se.isModified);
    std::cout << "Rename refused after complete temporary WAV: original back in place, byte-identical\n";
}

void successful_save_as(const std::filesystem::path& root) {
    Sample s(root, true, 48000, frames, 17);
    auto& se = s.state.sampleEditor;
    const auto original = file_bytes(s.wav);
    se.sampleName = "NEW WAV";
    se.pitchSemitones = 12;
    s.tap(19, 1);
    const auto& ins = s.host.project().instruments[instrument];
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR && !se.isModified);
    assert(ins.sampleFilePath && *ins.sampleFilePath == se.sampleFilePath && se.sampleFilePath != s.wav.string());
    assert(std::filesystem::path(se.sampleFilePath).filename() == "NEW WAV.wav");
    assert(se.sampleName == "NEW WAV" && s.host.sample_length(s.slot) == 96000);
    const auto cues = songcore::read_cue_points(se.sampleFilePath);
    assert(cues.size() == 17 && cues.front() == 4);
    assert(ins.sliceMarkers == std::vector<int64_t>(cues.begin(), cues.end()));
    assert(file_bytes(s.wav) == original);
    std::cout << "SAVE with new name: WAV, project path, editor name and 17 scaled cues agree\n";
}

void mono_reload_failure(const std::filesystem::path& root, bool refuseDecodeBlock) {
    constexpr int largeFrames = 384000;
    Sample s(root, true, 48000, largeFrames, 1);
    auto& se = s.state.sampleEditor;
    const auto before = file_bytes(s.wav);
    se.sourceMode = 1; // RIGHT -> mono
    se.isModified = true;
    if (refuseDecodeBlock) {
        trackedArrayBytes = largeFrames * sizeof(float);
        failBytes = 16384 * sizeof(int16_t); // loader's decode block, after allocating the mono PCM
    } else {
        assert(setenv("PT_LOAD_BUDGET_MB", "1", 1) == 0);
        assert(pt::load_budget_bytes() == 1048576);
    }
    std::vector<float> rightBefore(largeFrames);
    s.engine->getSampleDataRight(s.slot, rightBefore.data());
    const auto generation = s.host.sample_generation(s.slot);
    s.tap(19, 2);
    unsetenv("PT_LOAD_BUDGET_MB");
    const auto written = file_bytes(s.wav);
    assert(written != before && written[22] == 1 && written[23] == 0);
    assert(s.host.project().instruments[instrument].sliceMarkers == std::vector<int64_t>{8});
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR && !se.isModified);
    // The slot follows the mono file (the written channel, in place), and the status says why.
    assert(!s.host.has_stereo_data(s.slot) && s.host.sample_length(s.slot) == largeFrames);
    std::vector<float> slotNow(largeFrames);
    s.engine->getSampleData(s.slot, slotNow.data());
    assert(slotNow == rightBefore);   // exactly the RIGHT channel that was written
    assert(s.state.statusMessage == "SAVED; RELOAD NOT DONE: NOT ENOUGH MEMORY");
    std::cout << "Mono reload " << (refuseDecodeBlock ? "decode allocation" : "budget refusal")
              << ": file mono, slot mono, generation +" << s.host.sample_generation(s.slot) - generation
              << ", status [" << s.state.statusMessage << "]";
    if (refuseDecodeBlock) {
        assert(failBytes == 0 && trackedArray == nullptr);   // the loader freed its PCM
        trackedArrayBytes = 0;
    } else {
        assert(s.host.last_load_ran_out_of_memory());
    }
    std::cout << '\n';
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-save-io.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    write_failure(root / "temp-open", 192000, true);
    write_failure(root / "flush", 256, false);
    write_failure(root / "write", 192000, false);
    rename_failure(root / "rename");
    successful_save_as(root / "save-as");
    mono_reload_failure(root / "mono-budget", false);
    mono_reload_failure(root / "mono-allocation", true);
    std::filesystem::remove_all(root);
    std::puts("PASS: refused temp open, size limits and a refused rename keep the original WAV byte-identical; SAVE AS agrees everywhere; a failed mono reload (budget or decode buffer) is reported, frees its PCM and leaves the slot on the written channel");
}
