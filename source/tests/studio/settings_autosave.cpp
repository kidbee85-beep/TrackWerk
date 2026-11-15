// SettingsAutosave: a Scene/Typeface choice reaches settings.json without a clean quit, once the
// choices stop changing, never while playback defers it, and never more than once per change.
#include "ui/demo_font_style.h"
#include "ui/scene_presets.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"
#include "ui/theme_io.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace pt::ui;

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto dir = std::filesystem::absolute(argv[1]);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    StdFileSystem fs((dir / "data").string());
    auto on_disk = [&] { std::string blob; return fs.read_file(fs.settings_path(), blob) ? blob : std::string(); };
    auto restart = [&](SettingsValues& v, Theme& t) { v = SettingsValues{}; t = Theme{}; return load_settings(fs, v, t); };

    SettingsValues values;
    Theme theme = theme_default();
    using W = SettingsWrite;
    SettingsAutosave autosave;

    // No file yet: written once the values have been quiet for kQuietForMs.
    assert(autosave.poll(fs, values, theme, 0, false) == W::UNCHANGED);
    assert(autosave.poll(fs, values, theme, 1000, false) == W::UNCHANGED);
    assert(autosave.poll(fs, values, theme, 2000, false) == W::SAVED);
    const std::string first = on_disk();
    assert(!first.empty());
    for (uint64_t t = 3000; t <= 6000; t += 1000) assert(autosave.poll(fs, values, theme, t, false) == W::UNCHANGED);

    // Throttled: a poll before the next check neither reads nor writes.
    theme = scene_preset(12, theme.visualizerType);   // TERM / GREEN PHOSPHOR, layout D
    values.fontStyle = 15;
    assert(autosave.poll(fs, values, theme, 6500, false) == W::UNCHANGED);
    assert(on_disk() == first);

    // A held button keeps changing the choice every check: nothing is written until it settles.
    uint64_t now = 7000;
    for (int i = 0; i < 5; ++i, now += 1000) {
        theme.fontOverride = i % kDemoFontStyleCount;
        assert(autosave.poll(fs, values, theme, now, false) == W::UNCHANGED);
    }
    assert(on_disk() == first);
    const int chosenFont = theme.fontOverride;
    assert(autosave.poll(fs, values, theme, now, false) == W::UNCHANGED); now += 1000;
    assert(autosave.poll(fs, values, theme, now, false) == W::SAVED); now += 1000;

    // "Switched off" here: a fresh load sees the scene, its edited typeface and the user's font.
    {
        SettingsValues v; Theme t;
        assert(restart(v, t));
        assert(t.sceneChrome == 13 && t.fontOverride == chosenFont && v.fontStyle == 15);
        assert(serialize_theme(t) == serialize_theme(theme));
    }

    // Playback defers the write, however long; it lands on the first check after it stops.
    values.fontStyle = 3;
    const std::string beforePlay = on_disk();
    for (int i = 0; i < 6; ++i, now += 1000) assert(autosave.poll(fs, values, theme, now, true) == W::UNCHANGED);
    assert(on_disk() == beforePlay);
    assert(autosave.poll(fs, values, theme, now, false) == W::SAVED);
    {
        SettingsValues v; Theme t;
        assert(restart(v, t) && v.fontStyle == 3);
    }

    // A second autosave primed from an up-to-date file writes nothing.
    SettingsAutosave fresh;
    for (uint64_t t = 0; t <= 5000; t += 1000) assert(fresh.poll(fs, values, theme, t, false) == W::UNCHANGED);

    std::puts("PASS: settings autosave writes after 2 s of quiet, throttles to 1 s, waits out held buttons and playback, survives a restart");
    return 0;
}
