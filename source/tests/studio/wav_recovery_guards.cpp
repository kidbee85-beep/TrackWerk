// ptwav-recovery-guards-check: Codex's review-22 probe (reproduktionen/patch47-wav-recovery-guards.cpp),
// adapted for patch 0048 (Claude Code): local atomic replace, fresh exclusive temp, only usable WAV backups count, foreign backups kept visibly.
// macOS host reproduction. All files are synthetic and confined to a fresh test directory.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop

namespace {
namespace fs = std::filesystem;

bool browser_has_file(const pt::ui::AppState& state, const fs::path& path) {
    const auto& items = state.fileBrowser.items;
    return std::any_of(items.begin(), items.end(), [&](const auto& item) {
        return item.kind == pt::ui::BrowserItem::Kind::FILE && item.path == path.string();
    });
}

void invalid_backup_shadows_project_wav(const fs::path& root) {
    const auto app = root / "application";
    Sample s(app / "Samples", true, 48000, 256, 1);
    s.host.set_app_root(app.string());
    const auto portable = root / "portable";
    assert(fs::create_directories(portable));
    const auto projectFile = portable / "Portable.ptp";
    const auto projectWav = portable / "take.wav";
    const auto appWav = app / "Samples" / "take.wav";
    const fs::path backup = appWav.string() + ".ptbak";
    assert(songcore::write_wav(projectWav.string(), std::vector<float>(512, 0.25f),
                               {}, 24000, {16}, 1));
    const auto original = file_bytes(projectWav);
    s.host.edit_project().instruments[instrument].sampleFilePath = "take.wav";
    std::ofstream(projectFile) << songcore::serialize_project(s.host.project());

    const auto load = [&] {
        assert(s.host.load_project_file(projectFile.string(), (app / "Samples").string()));
        return s.host.last_media_load();
    };
    assert(load().loaded == 1 && s.host.sample_length(s.slot) == 512);

    assert(fs::create_directory(backup));
    std::ofstream(backup / "sentinel.txt") << "independent directory";
    assert(load().loaded == 1 && s.host.sample_length(s.slot) == 512);
    assert(fs::is_directory(backup) && fs::exists(backup / "sentinel.txt") && !fs::exists(appWav));
    std::cout << "Project load: a .ptbak folder in Samples is ignored; the WAV beside the project loads\n";

    fs::rename(backup, root / "retained-directory");
    std::ofstream(backup) << "independent non-WAV file";
    const auto foreign = file_bytes(backup);
    assert(load().loaded == 1 && s.host.sample_length(s.slot) == 512);
    assert(file_bytes(backup) == foreign && !fs::exists(appWav) && file_bytes(projectWav) == original);
    std::cout << "Project load: a foreign non-WAV .ptbak stays put; the WAV beside the project loads\n";
}

void backup_after_successful_publish(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto active = file_bytes(s.wav);
    const fs::path backup = s.wav.string() + ".ptbak";
    // File state after the new WAV is published and before the old copy is removed.
    assert(songcore::write_wav(backup.string(), std::vector<float>(512, 0.25f), {}, 24000, {}, 1));
    const auto old = file_bytes(backup);
    assert(s.host.load_sample(instrument, s.wav.string()));
    assert(s.host.sample_length(s.slot) == 256);
    s.state.settings.rememberFolder = false;
    s.dispatch.open_file_browser(pt::ui::AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR,
                                 root.string(), {"wav"});
    assert(browser_has_file(s.state, s.wav) && !browser_has_file(s.state, backup));
    s.dispatch.on_button_b();
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    const fs::path rescued = s.wav.parent_path() / (s.wav.stem().string() + "_ptbak.wav");
    assert(s.state.statusMessage == "SAVED; OLD BACKUP KEPT AS _PTBAK.WAV");
    // (The probe reached the editor through the file browser, so "back" after the save is the editor.)
    assert(!s.state.sampleEditor.isModified && file_bytes(s.wav) != active);
    assert(!fs::exists(backup) && file_bytes(rescued) == old && !fs::exists(s.wav.string() + ".tmp"));
    s.dispatch.open_file_browser(pt::ui::AppState::BrowserPurpose::LOAD_SAMPLE_EDITOR,
                                 root.string(), {"wav"});
    assert(browser_has_file(s.state, rescued));
    std::cout << "Both versions after an interrupted cleanup: OVERWRITE saves, the old one is kept as "
                 "_ptbak.wav and listed in the WAV browser\n";
}

void temp_link_targets_backup(const fs::path& root) {
    Sample s(root, true, 48000, 256, 1);
    const auto original = file_bytes(s.wav);
    const fs::path backup = s.wav.string() + ".ptbak";
    const fs::path temp = s.wav.string() + ".tmp";
    assert(songcore::write_wav(backup.string(), std::vector<float>(512, 0.25f), {}, 24000, {16}, 1));
    const auto old = file_bytes(backup);
    fs::create_symlink(backup.filename(), temp);
    s.state.sampleEditor.isModified = true;
    s.tap(19, 2);
    const fs::path rescued = s.wav.parent_path() / (s.wav.stem().string() + "_ptbak.wav");
    // The link is removed (not followed): the foreign backup is never written; it is kept visibly.
    // Since patch 0049 the save writes a temp of its own name: the link is not touched at all.
    assert(fs::is_symlink(temp) && file_bytes(rescued) == old);
    assert(file_bytes(s.wav) != original && !s.state.sampleEditor.isModified);
    std::cout << "OVERWRITE with a .tmp symlink to a foreign .ptbak: link removed, backup untouched and "
                 "kept as _ptbak.wav, save done\n";
    // The streamed writer: a .tmp link to the kept file is removed too, never written through.
    fs::remove(temp);
    fs::create_symlink(rescued.filename(), temp);
    songcore::WavStreamWriter writer(s.wav.string(), 48000);
    assert(writer.is_open());
    const float frame[] = {0.2f, -0.2f};
    writer.append_interleaved(frame, 1);
    assert(writer.finish());
    assert(fs::is_symlink(temp) && file_bytes(rescued) == old && fs::file_size(s.wav) == 48);
    std::cout << "Streamed writer with a .tmp link: link removed, linked file untouched, save done\n";
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    fs::create_directories(argv[1]);
    std::string tmpl = (fs::path(argv[1]) / "ptwav-recovery-guards.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const fs::path root = tmpl;
    invalid_backup_shadows_project_wav(root / "invalid");
    backup_after_successful_publish(root / "cleanup");
    temp_link_targets_backup(root / "temp-link");
    fs::remove_all(root);
    std::puts("PASS: a folder or non-WAV .ptbak never shadows or replaces a WAV; a foreign backup is kept visibly as _ptbak.wav and OVERWRITE works; a .tmp link is removed, never written through (both writers)");
}
