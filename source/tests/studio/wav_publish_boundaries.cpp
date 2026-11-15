// ptwav-publish-boundaries-check: Codex's review-20 probe (reproduktionen/patch45-wav-boundaries.cpp),
// adapted for patch 0046 (Claude Code): expects the behaviour KORREKTURAUFTRAG-CLAUDE-20 asks for.
// Local macOS host probes. All files and permission changes are confined to a new fixture directory.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop
#include "ui/project_actions.h"

#include <sys/stat.h>

namespace {
namespace fs = std::filesystem;

void project_recovery(const fs::path& root) {
    const auto app = root / "application";
    Sample s(app / "Samples", true, 48000, 256, 1);
    s.host.set_app_root(app.string());
    const auto openProject = [&](const fs::path& project, const fs::path& wav) {
        s.host.edit_project().instruments[instrument].sampleFilePath = wav.string();
        std::ofstream(project) << songcore::serialize_project(s.host.project());
        assert(s.host.load_project_file(project.string(), project.parent_path().string()));
        return s.host.last_media_load();
    };
    const auto original = file_bytes(s.wav);
    const fs::path localBackup = s.wav.string() + ".ptbak";
    fs::rename(s.wav, localBackup);
    auto result = openProject(root / "local.ptp", s.wav);
    assert(result.failed == 0 && result.loaded == 1);
    assert(file_bytes(s.wav) == original && !fs::exists(localBackup));

    const auto external = root / "external" / "Samples" / "take.wav";
    fs::create_directories(external.parent_path());
    const fs::path externalBackup = external.string() + ".ptbak";
    fs::copy_file(s.wav, externalBackup);
    result = openProject(root / "external.ptp", external);
    assert(result.failed == 0 && result.loaded == 1);
    assert(file_bytes(external) == original && !fs::exists(externalBackup));
    std::cout << "Project reopen: backups in the app root and at an external Samples/ path both restored\n";
}

void export_orphan(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    auto& project = s.host.edit_project();
    project.name = "Fixture";
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    const fs::path renderDir = s.fs.renders_directory();
    const auto firstMix = renderDir / "Fixture_0001.wav";
    const auto secondMix = renderDir / "Fixture_0002.wav";

    assert(pt::ui::render_mix(s.host, s.fs, s.state, {}, {}).ok);
    const auto original = file_bytes(firstMix);
    const fs::path backup = firstMix.string() + ".ptbak";
    fs::rename(firstMix, backup); // state after the old WAV has been moved aside
    const auto strandedTmp = firstMix.string() + ".tmp";
    std::ofstream(strandedTmp) << "unfinished replacement";
    project.phrases[0].steps[0].note = songcore::Note::EMPTY();
    const auto result = pt::ui::render_mix(s.host, s.fs, s.state, {}, {});
    assert(result.ok && result.message == "EXPORTED!");
    assert(fs::exists(secondMix) && !fs::exists(firstMix) && file_bytes(backup) == original);
    std::cout << "Numbered EXPORT MIX skips _0001 (kept only as .ptbak) and writes _0002; the old mix stays\n";
}

void unreadable_target(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto original = file_bytes(s.wav);
    const fs::path backup = s.wav.string() + ".ptbak";
    const std::vector<float> pcm(256, 0.25f);
    assert(::chmod(s.wav.c_str(), 0200) == 0);
    assert(std::fopen(s.wav.c_str(), "rb") == nullptr && errno == EACCES);
    s.state.sampleEditor.pitchSemitones = 12;
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    assert(::chmod(s.wav.c_str(), 0600) == 0);
    assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR && s.state.sampleEditor.isModified);
    assert(s.state.statusMessage == "SAVE FAILED");
    assert(file_bytes(s.wav) == original && !fs::exists(backup) && !fs::exists(s.wav.string() + ".tmp"));
    std::cout << "OVERWRITE on existing unreadable WAV: refused, original kept, edits unsaved\n";

    const auto existing = file_bytes(s.wav);
    assert(songcore::write_wav(backup.string(), pcm, {}, 24000, {}, 1));
    const auto recovered = file_bytes(backup);
    assert(recovered != existing && ::chmod(s.wav.c_str(), 0200) == 0);
    const bool loaded = s.host.load_sample(instrument, s.wav.string());
    assert(::chmod(s.wav.c_str(), 0600) == 0);
    assert(!loaded && file_bytes(s.wav) == existing && file_bytes(backup) == recovered);
    std::cout << "Recovery beside an unreadable existing WAV: nothing replaced, both files kept\n";
}

int reject_publish_and_restore(const char* from, const char* to) {
    if (std::strcmp(from, uriTarget) == 0 && std::strcmp(to, uriBak) == 0)
        return std::rename(hookTarget.c_str(), hookBak.c_str());
    errno = EIO;
    return -1;
}

void stale_status(const fs::path& root) {
    Sample a(root / "a", true, 48000, 256, 1);
    Sample b(root / "b", true, 48000, 256, 1);
    hookTarget = a.wav.string();
    hookTemp = hookTarget + ".tmp";
    hookBak = hookTarget + ".ptbak";
    a.state.sampleEditor.sampleFilePath = uriTarget;
    a.host.edit_project().instruments[instrument].sampleFilePath = uriTarget;
    a.state.sampleEditor.isModified = true;
    pt_set_file_hooks({file_open, file_remove, reject_publish_and_restore});
    a.tap(19, 2);
    pt_set_file_hooks({});
    assert(a.state.statusMessage == "SAVE FAILED: ORIGINAL KEPT AS .PTBAK");

    const auto original = file_bytes(b.wav);
    // The temp cannot be created (read-only folder): a failure before publication.
    const auto bFolder = b.wav.parent_path();
    assert(::chmod(bFolder.c_str(), 0500) == 0);
    b.state.sampleEditor.isModified = true;
    b.tap(19, 2);
    assert(::chmod(bFolder.c_str(), 0700) == 0);
    assert(b.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR);
    assert(b.state.statusMessage == "SAVE FAILED");
    assert(file_bytes(b.wav) == original && !fs::exists(b.wav.string() + ".ptbak"));
    std::cout << "Next save fails before publication: its own reason, no stale .PTBAK status\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-publish-boundaries.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    project_recovery(root / "project");
    export_orphan(root / "export");
    unreadable_target(root / "permissions");
    stale_status(root / "status");
    fs::remove_all(root);
    std::puts("PASS: backups restored at the stored path before re-rooting; numbered exports skip a number kept as .ptbak; an unreadable target is never replaced or restored over; a failed save reports its own reason");
}
