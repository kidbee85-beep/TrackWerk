// ptwav-publish-recovery-check: Codex's review-19 probe (reproduktionen/patch44-wav-recovery.cpp),
// adapted for patch 0045 (Claude Code): writer-owned .ptbak, prebuilt paths, cue reader closes, orphan recovery on load, directories refused.
// macOS host probe for patch 0044. Use a fresh directory for synthetic files.
// Reuse the round's editor fixture and one-shot allocation injector.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main adapted_patch43_save_io_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop
#include "ui/project_actions.h"

namespace {
namespace fs = std::filesystem;

constexpr const char* probeTarget = "test://samples/fixture/input.wav";
constexpr const char* probeTemp = "test://samples/fixture/input.wav.tmp";
constexpr const char* probeBak = "test://samples/fixture/input.wav.ptbak";

const char* local_path(const char* path) {
    if (std::strcmp(path, probeTarget) == 0) return hookTarget.c_str();
    if (std::strcmp(path, probeTemp) == 0) return hookTemp.c_str();
    assert(std::strcmp(path, probeBak) == 0);
    return hookBak.c_str();
}

int probe_remove(const char* path) {
    return std::remove(local_path(path));
}

bool armPublishOom = false;
bool armCueOom = false;
bool refuseRestore = false;
bool refusePublish = false;
int targetReads = 0;
int cueFd = -1;
int restoreAttempts = 0;
int cueReadIndex = 4;

std::size_t backup_path_allocation(const std::string& path) {
    // Mirror path + ".ptbak" to get libc++'s allocated capacity for this path length.
    return (path + ".ptbak").capacity() + 1;
}

int probe_open(const char* path, const char* mode) {
    const bool write = std::strcmp(mode, "wb") == 0;
    const int fd = ::open(local_path(path), write ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0600);
    if (fd < 0) return fd;
    if (armPublishOom && std::strcmp(path, probeTemp) == 0 && std::strcmp(mode, "wb") == 0)
        failBytes = backup_path_allocation(probeTarget);
    if (armCueOom && std::strcmp(path, probeTarget) == 0 && std::strcmp(mode, "rb") == 0) {
        // Publish probe, recovery probe, PCM load, then the cue chunk reader.
        if (++targetReads == cueReadIndex) {
            cueFd = fd;
            failBytes = sizeof(int); // first cue in read_cue_points()
        }
    }
    return fd;
}

int probe_rename(const char* from, const char* to) {
    if (std::strcmp(from, probeTarget) == 0 && std::strcmp(to, probeBak) == 0)
        return std::rename(hookTarget.c_str(), hookBak.c_str());
    if (std::strcmp(from, probeBak) == 0 && std::strcmp(to, probeTarget) == 0) {
        ++restoreAttempts;
        if (!refuseRestore) return std::rename(hookBak.c_str(), hookTarget.c_str());
    } else {
        assert(std::strcmp(from, probeTemp) == 0 && std::strcmp(to, probeTarget) == 0);
        if (!refusePublish) return std::rename(hookTemp.c_str(), hookTarget.c_str());
    }
    errno = EIO;
    return -1;
}

void use_hook(Sample& s) {
    hookTarget = s.wav.string();
    hookTemp = hookTarget + ".tmp";
    hookBak = hookTarget + ".ptbak";
    s.state.sampleEditor.sampleFilePath = probeTarget;
    s.host.edit_project().instruments[instrument].sampleFilePath = probeTarget;
    pt_set_file_hooks({probe_open, probe_remove, probe_rename});
}

void publish_oom(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto original = file_bytes(s.wav);
    s.state.sampleEditor.isModified = true;
    use_hook(s);
    armPublishOom = true;
    s.tap(19, 2);
    armPublishOom = false;
    pt_set_file_hooks({});
    // The backup path is built before the temp is written: nothing of that size is asked for after
    // it, so the injector never fires there — and no .tmp or .ptbak is left either way.
    failBytes = 0;
    assert(!fs::exists(hookTemp) && !fs::exists(hookBak));
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR || file_bytes(s.wav) == original);
    std::cout << "SAVE with the backup-path injector armed: no .tmp, no .ptbak, status ["
              << s.state.statusMessage << "]\n";

    auto& project = s.host.edit_project();
    project.name = "OOM";
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    const auto render = s.fs.renders_directory() + "/OOM_0001.wav";
    const auto renderTemp = render + ".tmp";
    const auto allocationSize = backup_path_allocation(render);
    bool threw = false;
    bool armed = false;
    try {
        pt::ui::render_mix(s.host, s.fs, s.state, {}, [&](float progress) {
            if (!armed && progress == 1.0f) {
                failBytes = allocationSize;
                armed = true;
            }
        });
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    failBytes = 0;
    if (threw) s.host.finish_render();
    assert(!threw && !fs::exists(renderTemp) && !fs::exists(render + ".ptbak"));
    std::cout << "EXPORT MIX with the backup-path injector armed: no exception, no .tmp, target "
              << (fs::exists(render) ? "written" : "absent") << "\n";
}

void cue_oom(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    s.state.sampleEditor.sourceMode = 1;
    s.state.sampleEditor.isModified = true;
    use_hook(s);
    armCueOom = true;
    s.tap(19, 2);
    armCueOom = false;
    pt_set_file_hooks({});
    assert(failBytes == 0 && targetReads >= cueReadIndex && cueFd >= 0);
    assert(s.state.statusMessage == "SAVED; RELOAD NOT DONE: NOT ENOUGH MEMORY");
    assert(!s.host.has_stereo_data(s.slot));
    assert(s.host.project().instruments[instrument].sliceMarkers == std::vector<int64_t>{8});
    assert(songcore::read_cue_points(s.wav.string()) == std::vector<int>{8});
    assert(fcntl(cueFd, F_GETFD) == -1);   // closed on the way out of the refused cue read
    std::cout << "Mono reload cue OOM: WAV/slot mono, cue intact, WAV descriptor closed\n";
}

void interrupted_save(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto original = file_bytes(s.wav);
    s.state.sampleEditor.isModified = true;
    use_hook(s);
    refusePublish = true;
    refuseRestore = true;
    s.tap(19, 2);
    assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR && s.state.sampleEditor.isModified);
    assert(s.state.statusMessage == "SAVE FAILED: ORIGINAL KEPT AS .PTBAK");
    assert(restoreAttempts == 1 && !fs::exists(s.wav) && !fs::exists(hookTemp));
    assert(file_bytes(hookBak) == original);
    // Once the I/O error clears, the remaining files match interruption between renames
    // after temp cleanup. Recreate the complete temp as an interruption would leave it.
    pt_set_file_hooks({});
    fs::copy_file(hookBak, hookTemp);
    // The next load of that path recognises its own backup with nothing at the target, and puts it
    // back; a leftover temp is not taken for the original.
    assert(s.host.load_sample(instrument, s.wav.string()));
    assert(file_bytes(s.wav) == original && !fs::exists(hookBak) && fs::exists(hookTemp));
    std::cout << "Publish and restore refused: reported with .PTBAK; the next load restores the original\n";
    // An orphan .ptbak is never deleted by a save (since patch 0047). Since patch 0048 a usable one
    // is moved to the visible "<name>_ptbak.wav" and the save goes through.
    fs::rename(s.wav, hookBak);
    const std::vector<float> pcm(256, 0.25f);
    assert(songcore::write_wav(s.wav.string(), pcm, {}, 48000, {}, 1));
    assert(songcore::detail::last_publish_rescued_backup());
    const fs::path rescued = s.wav.parent_path() / (s.wav.stem().string() + "_ptbak.wav");
    // (The leftover "<path>.tmp" is not this save's temp: since patch 0049 a local save never touches it.)
    assert(!fs::exists(hookBak) && file_bytes(rescued) == original && fs::exists(s.wav));
    std::cout << "Save over an orphan backup: backup kept visibly as _ptbak.wav, save done, no .tmp\n";
}

void stream_rollback(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto original = file_bytes(s.wav);
    use_hook(s);
    refusePublish = true;
    refuseRestore = false;
    restoreAttempts = 0;
    songcore::WavStreamWriter writer(probeTarget, 48000);
    assert(writer.is_open());
    const float frame[] = {0.5f, -0.5f};
    writer.append_interleaved(frame, 1);
    assert(!writer.finish());
    pt_set_file_hooks({});
    assert(restoreAttempts == 1 && file_bytes(s.wav) == original);
    assert(!fs::exists(hookBak) && !fs::exists(hookTemp));
    std::cout << "Streamed publish refused, restore succeeds: original byte-identical, no sidecars\n";
}

void occupied_backup_path(const fs::path& root) {
    assert(fs::create_directories(root));
    const auto wav = root / "input.wav";
    const auto bak = wav.string() + ".bak";
    const std::vector<float> pcm(256, 0.25f);
    assert(songcore::write_wav(wav.string(), pcm, {}, 48000, {}, 1));
    std::ofstream(bak) << "independent file";
    assert(songcore::write_wav(wav.string(), pcm, {}, 48000, {}, 1));
    assert(fs::exists(bak) && file_bytes(bak).size() == 16 && !fs::exists(wav.string() + ".ptbak"));
    std::cout << "Independent .bak beside the WAV: left untouched by an overwrite\n";
}

void directory_target(const fs::path& root) {
    const auto wav = root / "directory.wav";
    assert(fs::create_directories(wav));
    std::ofstream(wav / "sentinel") << "original folder";
    const std::vector<float> pcm(256, 0.25f);
    assert(!songcore::write_wav(wav.string(), pcm, {}, 48000, {}, 1));
    assert(fs::is_directory(wav) && file_bytes(wav / "sentinel").size() == 15);
    assert(!fs::exists(wav.string() + ".ptbak") && !fs::exists(wav.string() + ".tmp"));
    std::cout << "Directory at WAV target: refused, folder and content untouched\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-publish-recovery.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    publish_oom(root / "publish-oom");
    cue_oom(root / "cue-oom");
    interrupted_save(root / "interrupted");
    stream_rollback(root / "stream-rollback");
    occupied_backup_path(root / "collision");
    directory_target(root / "directory");
    fs::remove_all(root);
    std::puts("PASS: WAV publish builds its paths before writing (no .tmp left, no exception from export), the cue reader closes its file on a refused allocation, a kept .ptbak is reported and restored by the next load, independent .bak files and directories at the target are left alone");
}
