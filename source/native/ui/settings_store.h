#pragma once

// settings.json. Missing file, key or parse: the default stays.
// ⚠️ Every row a platform has must be persisted here, or it resets every launch — only a save → load
// round trip catches the omission.

#include <cstdint>
#include <string>

#include "ui/filesystem.h"
#include "ui/modules/settings_editor.h"
#include "ui/theme.h"

namespace pt::ui {

// `theme` gets its full palette (an older file's theme name still loads by name).
// ⚠️ False both with no file and with one that will not parse — the next save then discards it. A
// caller that cares asks the filesystem itself (`app.cpp` does).
bool load_settings(FileSystem& fs, SettingsValues& values, Theme& theme);

bool save_settings(FileSystem& fs, const SettingsValues& values, const Theme& theme);

enum class SettingsWrite { UNCHANGED, SAVED, FAILED };

// Write only if the bytes would differ — the shell's call on exit, instead of a dirty flag (the theme
// editor writes `theme` directly, and a flag lost its palette).
SettingsWrite save_settings_if_changed(FileSystem& fs, const SettingsValues& values, const Theme& theme);

// Saves settings once they have stopped changing for a moment, so a handheld switched off without
// quitting keeps its Scene, Typeface and other choices. The exit and background saves stay; this
// only closes the gap before them. Throttled: one serialisation per second, one write per change.
class SettingsAutosave {
public:
    static constexpr uint64_t kCheckEveryMs = 1000;   // how often the settings are serialised
    static constexpr uint64_t kQuietForMs   = 2000;   // a held A+UP repeats every 100 ms
    // UNCHANGED unless this call wrote. `deferWrite` (e.g. while playing) postpones the write only.
    SettingsWrite poll(FileSystem& fs, const SettingsValues& values, const Theme& theme,
                       uint64_t nowMs, bool deferWrite);
    // The immediate save (going to the background, quitting). Compares with the file itself, not the
    // cache, writes if they differ, and leaves the cache holding what the file holds afterwards, so
    // a later poll compares with the truth. Every writer of settings.json goes through one object.
    SettingsWrite flush(FileSystem& fs, const SettingsValues& values, const Theme& theme);
private:
    std::string onDisk_;     // what the file is known to hold
    std::string seen_;       // the latest serialisation
    uint64_t    seenAtMs_ = 0, nextCheckMs_ = 0;
    bool        primed_ = false;
};

}  // namespace pt::ui
