// ptwav-interruption-check: Codex's review-23 probe (reproduktionen/patch48-wav-interruption.cpp),
// adapted for patch 0049 (Claude Code): every local save has a temp of its own name, a foreign backup moves aside only after the publish, and a backup must be a whole WAV.
// macOS host reproduction. Pass a new directory for synthetic WAVs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop

#include "ui/project_actions.h"
#include <sys/wait.h>

namespace {
namespace fs = std::filesystem;
const char* stopBeforeRename = nullptr;   // a prefix: "<path>." — any of that path's own temps

// The temps beside a target: "<dir>/.ptw-<pid>-<n>.tmp" (since patch 0050; one save at a time here).
std::vector<fs::path> temps_of(const fs::path& target) {
    std::vector<fs::path> found;
    const std::string prefix = ".ptw-";
    for (const auto& e : fs::directory_iterator(target.parent_path())) {
        const std::string n = e.path().filename().string();
        if (n.rfind(prefix, 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".tmp") == 0) found.push_back(e.path());
    }
    return found;
}
} // namespace

// The child exits just before the selected local rename. Every other rename is real.
extern "C" int rename(const char* from, const char* to) {
    if (stopBeforeRename && std::strncmp(from, stopBeforeRename, std::strlen(stopBeforeRename)) == 0 &&
        std::strlen(from) > 4 && std::strcmp(from + std::strlen(from) - 4, ".tmp") == 0) ::_exit(25);
    return ::renameat(AT_FDCWD, from, AT_FDCWD, to);
}

namespace {

void single_writer_interrupted(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto old = file_bytes(s.wav);
    const auto tmp = s.wav.string() + ".tmp";
    const auto bak = s.wav.string() + ".ptbak";
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        songcore::WavStreamWriter writer(s.wav.string(), 48000);
        assert(writer.is_open());
        const std::vector<float> audio(8192, 0.25f);
        writer.append_interleaved(audio.data(), 4096);
        ::_exit(23); // terminate after an actual write, without a destructor or publication
    }
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 23);
    (void)tmp;
    const auto stale = temps_of(s.wav);
    assert(stale.size() == 1 && fs::file_size(stale.front()) > 44 && !fs::exists(bak) && file_bytes(s.wav) == old);
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    assert(!s.state.sampleEditor.isModified);
    assert(temps_of(s.wav).empty() && !fs::exists(bak) && file_bytes(s.wav) != old);
    std::cout << "Single writer process exits during write: old WAV byte-identical; next OVERWRITE "
                 "removes the dead process's temp and succeeds without .ptbak\n";
}

void configure_song(Sample& s) {
    auto& project = s.host.edit_project();
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    s.host.push_params();
}

void two_named_resamples(const fs::path& root) {
    Sample first(root / "shared", true, 48000, 256, 1);
    configure_song(first);
    const fs::path dir = first.fs.resampled_directory();
    const auto target = dir / "Shared.wav";
    const auto temp = target.string() + ".tmp";
    assert(songcore::write_wav(target.string(), std::vector<float>(512, 0.25f), {}, 24000, {}, 1));
    const auto old = file_bytes(target);

    int startSecond[2], secondWriting[2], stopSecond[2];
    assert(::pipe(startSecond) == 0 && ::pipe(secondWriting) == 0 && ::pipe(stopSecond) == 0);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        ::close(startSecond[1]);
        ::close(secondWriting[0]);
        ::close(stopSecond[1]);
        char token = 0;
        assert(::read(startSecond[0], &token, 1) == 1);
        Sample second(root / "second-input", true, 48000, 256, 1);
        configure_song(second);
        pt::ui::StdFileSystem shared((root / "shared").string());
        std::string out;
        pt::ui::render_resample(second.host, shared, 0, 0, {0}, "Shared", out, [&](float) {
            assert(::write(secondWriting[1], "w", 1) == 1);
            assert(::read(stopSecond[0], &token, 1) == 1);
            ::_exit(23); // abort the second render after the first has returned
        });
        ::_exit(24);
    }
    ::close(startSecond[0]);
    ::close(secondWriting[1]);
    ::close(stopSecond[0]);

    bool interleaved = false;
    std::string out;
    const auto result = pt::ui::render_resample(first.host, first.fs, 0, 0, {0}, "Shared", out, [&](float) {
        if (interleaved) return;
        (void)temp;
        const auto own = temps_of(target);
        assert(own.size() == 1 && fs::file_size(own.front()) > 44 && file_bytes(target) == old);
        assert(::write(startSecond[1], "s", 1) == 1);
        char token = 0;
        assert(::read(secondWriting[0], &token, 1) == 1);
        interleaved = true;
    });
    assert(::write(stopSecond[1], "q", 1) == 1);
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 23);
    for (int fd : {startSecond[1], secondWriting[0], stopSecond[1]}) assert(::close(fd) == 0);
    assert(interleaved && result.ok && out == target.string());
    assert(!fs::exists(target.string() + ".ptbak"));
    const auto published = file_bytes(target);
    assert(published != old && published.size() > 44);
    assert(!(published[40] == 0 && published[41] == 0 && published[42] == 0 && published[43] == 0));
    assert(pt::ui::create_resampled_instrument(first.host, out) >= 0);
    std::cout << "Two processes resample to the same name: each has its own temp; the first publishes "
                 "its own complete WAV, which loads\n";
}

void interrupt_between_rescue_and_publish(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto original = file_bytes(s.wav);
    const fs::path backup = s.wav.string() + ".ptbak";
    const fs::path rescue = root / "input_ptbak.wav";
    const auto temporary = s.wav.string() + ".tmp";
    fs::rename(s.wav, backup);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        static const std::string prefix = s.wav.parent_path().string() + "/.ptw-";
        stopBeforeRename = prefix.c_str();
        s.state.sampleEditor.isModified = true;
        s.tap(19, 2);
        ::_exit(26);
    }
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 25);
    (void)temporary;
    // The backup is moved aside only after the publish: interrupted before it, it is still where
    // the load finds it.
    assert(!fs::exists(s.wav) && file_bytes(backup) == original && !fs::exists(rescue));
    const auto project = root / "Test.ptp";
    std::ofstream(project) << songcore::serialize_project(s.host.project());
    assert(s.host.load_project_file(project.string(), root.string()));
    assert(s.host.last_media_load().loaded == 1 && file_bytes(s.wav) == original && !fs::exists(backup));
    std::cout << "Process exits before publication with only a backup there: project load restores "
                 "the original\n";
}

void false_wav_signature(const fs::path& root) {
    const auto app = root / "app";
    Sample s(app / "Samples", true, 48000, 256, 1);
    s.host.set_app_root(app.string());
    const auto portable = root / "portable";
    assert(fs::create_directories(portable));
    const auto target = portable / "take.wav";
    const auto projectFile = portable / "Portable.ptp";
    const auto wrongPath = app / "Samples" / "take.wav";
    const fs::path backup = wrongPath.string() + ".ptbak";
    assert(songcore::write_wav(target.string(), std::vector<float>(512, 0.25f), {}, 24000, {}, 1));
    const auto original = file_bytes(target);
    s.host.edit_project().instruments[instrument].sampleFilePath = "take.wav";
    std::ofstream(projectFile) << songcore::serialize_project(s.host.project());
    assert(s.host.load_project_file(projectFile.string(), (app / "Samples").string()));
    assert(s.host.last_media_load().loaded == 1 && s.host.sample_length(s.slot) == 512);
    const char header[] = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'};
    std::ofstream(backup, std::ios::binary).write(header, sizeof(header));
    const auto invalid = file_bytes(backup);
    assert(s.host.load_project_file(projectFile.string(), (app / "Samples").string()));
    assert(s.host.last_media_load().loaded == 1 && s.host.sample_length(s.slot) == 512);
    assert(file_bytes(backup) == invalid && !fs::exists(wrongPath) && file_bytes(target) == original);
    std::cout << "Prepared 12-byte .ptbak with RIFF/WAVE but no chunks: left in place, the portable "
                 "WAV loads\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-interruption.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    single_writer_interrupted(root / "single");
    two_named_resamples(root / "parallel");
    interrupt_between_rescue_and_publish(root / "legacy");
    false_wav_signature(root / "bad-header");
    fs::remove_all(root);
    std::puts("PASS: a dead writer's temp is removed by the next save; two processes saving one name each publish only their own complete WAV; interrupted before publish, a lone backup is still restored on load; a header-only .ptbak is ignored");
}
