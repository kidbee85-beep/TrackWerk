// ptwav-save-limits-check: Codex's review-24 probe (reproduktionen/patch49-wav-save-limits.cpp),
// adapted for patch 0050 (Claude Code): short temp names, numbered exports never publish over a newcomer, strict temp pattern.
// macOS host reproduction. Pass a new directory for synthetic files.
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

void long_sample_name(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const long nameLimit = ::pathconf(root.c_str(), _PC_NAME_MAX);
    assert(nameLimit > 16);
    const auto target = root / (std::string(static_cast<std::size_t>(nameLimit) - 8, 'a') + ".wav");
    fs::rename(s.wav, target);
    const auto original = file_bytes(target);
    assert(s.host.load_sample(instrument, target.string()));
    s.state.currentScreen = pt::ui::ScreenType::INSTRUMENT;
    s.dispatch.open_imported_sample_editor();
    assert(s.state.sampleEditor.sampleFilePath == target.string());
    const auto temp = songcore::detail::temp_path_for(target.string());
    assert(fs::path(temp).filename().string().size() < 40);   // short, whatever the target's name

    s.state.sampleEditor.pitchSemitones = 12;
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    assert(!s.state.sampleEditor.isModified && file_bytes(target) != original && songcore::is_wav_file(target.string()));
    std::cout << "OVERWRITE: existing WAV name " << target.filename().string().size()
              << " bytes, filesystem limit " << nameLimit << ": short temp, saved\n";
}

void configure_song(Sample& s, songcore::Note note) {
    auto& project = s.host.edit_project();
    project.name = "Parallel";
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = note;
    project.phrases[0].steps[0].instrument = instrument;
    s.host.push_params();
}

void parallel_numbered_exports(const fs::path& root) {
    Sample first(root / "shared", true, 48000, 4096, 1);
    configure_song(first, songcore::Note::C4());
    const fs::path renders = first.fs.renders_directory();
    const auto firstPath = renders / "Parallel_0001.wav";
    const auto secondPath = renders / "Parallel_0002.wav";
    const fs::path secondCopy = root / "second-output.wav";

    int startSecond[2];
    assert(::pipe(startSecond) == 0);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        ::close(startSecond[1]);
        char token = 0;
        assert(::read(startSecond[0], &token, 1) == 1);
        Sample second(root / "second-input", true, 48000, 4096, 1);
        configure_song(second, songcore::Note::C4());
        second.host.edit_project().phrases[0].steps[0].note = songcore::Note::EMPTY();
        pt::ui::StdFileSystem shared((root / "shared").string());
        assert(pt::ui::render_mix(second.host, shared, second.state, {}, {}).ok);
        assert(songcore::is_wav_file(firstPath.string()));
        assert(fs::copy_file(firstPath, secondCopy));
        ::_exit(0);
    }
    ::close(startSecond[0]);

    bool interleaved = false;
    const auto result = pt::ui::render_mix(first.host, first.fs, first.state, {}, [&](float) {
        if (interleaved) return;
        interleaved = true;
        // The first action has reserved no destination name while its private temp is open.
        assert(!fs::exists(firstPath));
        assert(::write(startSecond[1], "s", 1) == 1);
        int status = 0;
        assert(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        assert(file_bytes(firstPath) == file_bytes(secondCopy));
    });
    assert(::close(startSecond[1]) == 0);
    // The number was taken while the first rendered: it reports so and leaves the finished mix.
    assert(interleaved && !result.ok && result.message == "EXPORT FAILED: NUMBER TAKEN MEANWHILE");
    assert(file_bytes(firstPath) == file_bytes(secondCopy) && !fs::exists(secondPath));
    std::cout << "Two numbered EXPORT MIX actions on one number: the second-finished reports "
                 "NUMBER TAKEN MEANWHILE; the published mix stays\n";
}

void prepared_malformed_temp_name(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) ::_exit(0);
    int status = 0;
    assert(::waitpid(child, &status, 0) == child && WIFEXITED(status));
    assert(::kill(child, 0) == -1 && errno == ESRCH);
    const auto foreign = s.wav.string() + "." + std::to_string(child) + "-.tmp";
    std::ofstream(foreign) << "outside the writer's naming pattern";
    assert(fs::file_size(foreign) > 0);
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    assert(!s.state.sampleEditor.isModified && fs::exists(foreign));
    // And in the current temp form: a missing counter is not ours either.
    const auto foreignNew = s.wav.parent_path() / (".ptw-" + std::to_string(child) + "-.tmp");
    std::ofstream(foreignNew) << "outside the writer's naming pattern";
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    assert(fs::exists(foreignNew));
    std::cout << "Prepared foreign files with a missing counter: left in place by orphan cleanup\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-save-limits.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    long_sample_name(root / "long-name");
    parallel_numbered_exports(root / "parallel-export");
    prepared_malformed_temp_name(root / "malformed-temp");
    fs::remove_all(root);
    std::puts("PASS: a 251-byte WAV name saves (short temp); a numbered export whose number was taken meanwhile reports it and keeps the published mix; foreign files outside the temp pattern are never cleaned up");
}
