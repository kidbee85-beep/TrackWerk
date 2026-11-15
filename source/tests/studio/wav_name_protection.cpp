// ptwav-name-protection-check: Codex's review-21 probe (reproduktionen/patch46-wav-name-protection.cpp),
// adapted for patch 0047 (Claude Code): a .ptbak counts as a taken name everywhere, nothing deletes it, and the load finds it where the file belongs.
// macOS host reproduction. Pass a fresh directory for synthetic files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop

namespace {
namespace fs = std::filesystem;

void sample_save(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const fs::path target = fs::path(s.fs.samples_directory()) / "Take.wav";
    const fs::path backup = target.string() + ".ptbak";
    assert(songcore::write_wav(target.string(), std::vector<float>(512, 0.25f), {}, 24000, {}, 1));
    const auto oldTake = file_bytes(target);
    fs::rename(target, backup);

    s.state.sampleEditor.sampleName = "Take";
    s.state.sampleEditor.isModified = true;
    s.tap(19, 1); // SAVE, not OVERWRITE
    // "Take" is taken by its backup: SAVE asks for a name, proposing the next free one.
    assert(s.state.qwerty.isOpen && s.state.qwerty.text == "Take_0001");
    assert(s.state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR && s.state.sampleEditor.isModified);
    assert(!fs::exists(target) && file_bytes(backup) == oldTake);
    std::cout << "Sample Editor SAVE: Take taken by its .ptbak, the prompt proposes Take_0001, backup kept\n";
}

void resample_accepts_suggested_name(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    auto& project = s.host.edit_project();
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    s.host.push_params();
    const fs::path dir = s.fs.resampled_directory();
    const auto target = dir / "Resample_0001.wav";
    const auto next = dir / "Resample_0002.wav";
    const fs::path backup = target.string() + ".ptbak";
    assert(songcore::write_wav(target.string(), std::vector<float>(512, 0.25f), {}, 24000, {}, 1));
    const auto oldTake = file_bytes(target);
    fs::rename(target, backup);

    s.state.currentScreen = pt::ui::ScreenType::SONG;
    s.state.selection.handle_select_b(1000, 0, 1, 8, 255);
    s.dispatch.on_a_a();
    assert(s.state.qwerty.isOpen && s.state.qwerty.text == "Resample_0002");
    s.dispatch.on_start(); // APPLY without editing the proposed name
    assert(!s.state.qwerty.isOpen && !s.state.isRendering);
    assert(s.state.statusMessage.rfind("RESAMPLED -> INST ", 0) == 0);
    assert(fs::exists(next) && !fs::exists(target) && file_bytes(backup) == oldTake);
    std::cout << "Song RESAMPLE: proposes Resample_0002 past the _0001 backup; APPLY keeps the backup\n";

    // A typed name that is taken only by a backup: the backup moves to the visible
    // "Resample_0001_ptbak.wav" and stays; the new take is written (since patch 0048).
    s.dispatch.on_a_a();
    assert(s.state.qwerty.isOpen);
    s.state.qwerty.text = "Resample_0001";
    s.dispatch.on_start();
    assert(s.state.statusMessage.rfind("RESAMPLED -> INST ", 0) == 0);
    assert(fs::exists(target) && !fs::exists(backup) && file_bytes(dir / "Resample_0001_ptbak.wav") == oldTake);
    std::cout << "Song RESAMPLE: a typed name held by a .ptbak keeps it as _ptbak.wav and writes the take\n";
}

void numbered_export_exhausted(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    auto& project = s.host.edit_project();
    project.name = "Fixture";
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    s.host.push_params();
    const fs::path dir = s.fs.renders_directory();
    for (int index = 1; index < 9999; ++index) {
        char name[32];
        std::snprintf(name, sizeof(name), "Fixture_%04d.wav", index);
        std::ofstream(dir / name);
    }
    const auto last = dir / "Fixture_9999.wav";
    const fs::path backup = last.string() + ".ptbak";
    assert(songcore::write_wav(last.string(), std::vector<float>(512, 0.25f), {}, 24000, {}, 1));
    const auto oldMix = file_bytes(last);
    fs::rename(last, backup);
    const auto result = pt::ui::render_mix(s.host, s.fs, s.state, {}, {});
    assert(!result.ok && result.message == "EXPORT FAILED: NO FREE NUMBER");
    assert(!fs::exists(last) && file_bytes(backup) == oldMix);
    std::cout << "Numbered EXPORT MIX with all 9999 numbers taken: refused, _9999 backup kept\n";
}
void project_relative_backup(const fs::path& root) {
    const auto app = root / "application";
    Sample s(app / "Samples", true, 48000, 256, 1);
    s.host.set_app_root(app.string());
    const auto songDir = root / "portable";
    assert(fs::create_directories(songDir));
    const auto target = songDir / "portable.wav";
    const fs::path backup = target.string() + ".ptbak";
    const auto project = songDir / "Portable.ptp";
    assert(songcore::write_wav(target.string(), std::vector<float>(512, 0.25f), {}, 24000, {16}, 1));
    const auto original = file_bytes(target);
    s.host.edit_project().instruments[instrument].sampleFilePath = "portable.wav";
    std::ofstream(project) << songcore::serialize_project(s.host.project());

    const auto open = [&] {
        s.dispatch.open_file_browser(pt::ui::AppState::BrowserPurpose::LOAD_PROJECT,
                                     songDir.string(), {"ptp"});
        auto& browser = s.state.fileBrowser;
        const auto item = std::find_if(browser.items.begin(), browser.items.end(),
                                      [&](const auto& entry) { return entry.path == project.string(); });
        assert(item != browser.items.end());
        browser.cursor = static_cast<int>(item - browser.items.begin());
        s.dispatch.on_button_a();
        assert(s.state.projectPath == project.string());
    };
    open();
    assert(s.host.last_media_load().loaded == 1 && s.host.sample_length(s.slot) == 512);
    fs::rename(target, backup);
    open();
    assert(s.host.last_media_load().loaded == 1 && s.host.sample_length(s.slot) == 512);
    assert(file_bytes(target) == original && !fs::exists(backup));
    std::cout << "Project LOAD: a relative WAV left only as .ptbak beside the .ptp is restored and loaded\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-name-protection.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    sample_save(root / "save");
    resample_accepts_suggested_name(root / "resample");
    numbered_export_exhausted(root / "number-limit");
    project_relative_backup(root / "relative");
    fs::remove_all(root);
    std::puts("PASS: a name held by a .ptbak is taken for SAVE, resample and numbered exports (none left: refused); a typed name over a backup is refused; a relative WAV left only as .ptbak beside the project is restored on load");
}
