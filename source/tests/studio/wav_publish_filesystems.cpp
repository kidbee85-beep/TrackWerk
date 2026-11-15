// ptwav-publish-filesystems-check: Codex's review-25 probe (reproduktionen/patch50-wav-publish-filesystems.cpp),
// adapted for patch 0051 (Claude Code): numbered exports publish without hard links too, the collision status belongs to its own save, and only representable pids count as temps.
// Local macOS host probe. The default run models a filesystem that refuses hard links.
// An optional existing writable test-volume directory exercises its real link operation.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop

#include "ui/project_actions.h"
#include <cstdint>
#include <limits>
#include <sys/stat.h>
#include <sys/wait.h>

namespace {
namespace fs = std::filesystem;
bool noHardLinks = false;
int refusedLinks = 0;
int observedLinkErrno = 0;
}

extern "C" int link(const char* from, const char* to) {
    if (noHardLinks) {
        ++refusedLinks;
        errno = ENOTSUP;
        observedLinkErrno = errno;
        return -1;
    }
    const int rc = ::linkat(AT_FDCWD, from, AT_FDCWD, to, 0);
    observedLinkErrno = rc == -1 ? errno : 0;
    return rc;
}

namespace {

void configure_song(Sample& s) {
    auto& project = s.host.edit_project();
    project.name = "Filesystem";
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    s.host.push_params();
}

void no_link_exports(const fs::path& root, bool simulate) {
    Sample s(root, true, 48000, 4096, 1);
    configure_song(s);
    const fs::path renders = s.fs.renders_directory();
    const fs::path resampled = s.fs.resampled_directory();
    noHardLinks = simulate;
    const auto mix = pt::ui::render_mix(s.host, s.fs, s.state, {}, {});
    const int mixErrno = observedLinkErrno;
    assert(mix.ok && mix.message == "EXPORTED!" && songcore::is_wav_file((renders / "Filesystem_0001.wav").string()));
    std::string out;
    const auto automatic = pt::ui::render_resample(s.host, s.fs, 0, 0, {0}, "", out, {});
    assert(automatic.ok && out == (resampled / "Resample_0001.wav").string() && songcore::is_wav_file(out));
    if (simulate) assert(refusedLinks == 2 && mixErrno == ENOTSUP);
    // A number taken before the save is still never replaced on such a filesystem.
    const auto before = file_bytes(renders / "Filesystem_0001.wav");
    const auto second = pt::ui::render_mix(s.host, s.fs, s.state, {}, {});
    assert(second.ok && file_bytes(renders / "Filesystem_0001.wav") == before &&
           songcore::is_wav_file((renders / "Filesystem_0002.wav").string()));
    const auto named = pt::ui::render_resample(s.host, s.fs, 0, 0, {0}, "Named", out, {});
    noHardLinks = false;
    assert(named.ok && out == (resampled / "Named.wav").string());
    assert(songcore::is_wav_file(out) && pt::ui::create_resampled_instrument(s.host, out) >= 0);
    std::cout << (simulate ? "Simulated" : "Native") << " filesystem without hard links: MIX, automatic "
              << "and named RESAMPLE publish; an existing number is kept\n";
}

void stale_number_status(const fs::path& root) {
    Sample s(root, true, 48000, 4096, 1);
    configure_song(s);
    const fs::path renders = s.fs.renders_directory();
    const auto taken = renders / "Filesystem_0001.wav";
    // An independent filesystem actor occupies the chosen destination during the first render.
    bool placed = false;
    const auto collision = pt::ui::render_mix(s.host, s.fs, s.state, {}, [&](float) {
        if (placed) return;
        placed = true;
        std::ofstream(taken) << "keep the first output";
    });
    assert(placed && !collision.ok && collision.message == "EXPORT FAILED: NUMBER TAKEN MEANWHILE");
    const auto original = file_bytes(taken);
    assert(::chmod(renders.c_str(), 0500) == 0);
    const auto failure = pt::ui::render_mix(s.host, s.fs, s.state, {}, {});
    assert(::chmod(renders.c_str(), 0700) == 0);
    assert(!failure.ok && failure.message == "EXPORT FAILED");   // its own reason, not the old one
    assert(!fs::exists(renders / "Filesystem_0002.wav") && file_bytes(taken) == original);
    assert(std::distance(fs::directory_iterator(renders), fs::directory_iterator{}) == 1);
    std::cout << "After a collision, a temp-open failure on a read-only render folder reports its own "
                 "EXPORT FAILED\n";
}

void impossible_pid_cleanup(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) ::_exit(0);
    int status = 0;
    assert(::waitpid(child, &status, 0) == child && WIFEXITED(status));
    assert(::kill(child, 0) == -1 && errno == ESRCH);

    static_assert(sizeof(pid_t) == 4 && sizeof(long) > sizeof(pid_t));
    const auto impossible = (std::uint64_t{1} << 32) + static_cast<std::uint64_t>(child);
    const auto foreign = root / (".ptw-" + std::to_string(impossible) + "-0.tmp");
    std::ofstream(foreign) << "independent file with PID outside pid_t";
    assert(fs::file_size(foreign) > 0);
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    assert(!s.state.sampleEditor.isModified && fs::exists(foreign));
    std::cout << "Prepared temp-looking file with out-of-range PID: not a temp of ours, left in place\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-publish-filesystems.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    no_link_exports(root / "simulated", true);
    stale_number_status(root / "status");
    impossible_pid_cleanup(root / "invalid-pid");
    if (argc == 3) {
        const auto native = fs::path(argv[2]) / ("pt-qa-" + std::to_string(::getpid()));
        assert(fs::create_directory(native));
        no_link_exports(native, false);
    }
    fs::remove_all(root);
    std::puts("PASS: without hard links numbered exports and resamples still publish and keep a taken number; a failed export reports its own reason; temp names with an unrepresentable pid are never cleaned up");
}
