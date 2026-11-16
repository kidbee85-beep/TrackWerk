#pragma once

// The sound browser (TrackWerk 1.2, stage 1): the factory instrument presets the app already ships
// (Presets/Instruments/PocketStudio_MegaPack, 2048 .pti files in sound-type folders) found by
// category, starred, and listed by recent use — inside the Studio's existing PRESET picker, beside
// its MODEL tab. No new preset universe: an entry IS a factory .pti, and its key is the file's path
// below the pack folder, stable across updates and devices.
//
// Nothing here touches the project. Applying and previewing live in studio_synth.cpp.

#include <array>
#include <string>
#include <vector>

namespace pt::ui {

class FileSystem;

inline constexpr const char* kSoundPackFolder = "PocketStudio_MegaPack";
inline constexpr int kSoundFavoritesMax = 64;
inline constexpr int kSoundRecentMax = 24;
inline constexpr int kSoundPreviewLevelDefault = 100;   // percent, preview only
inline constexpr int kSoundPreviewLevelMin = 10;
inline constexpr int kSoundPreviewLevelMax = 200;

// The picker's tabs, left to right. MODEL is the old picker (the current model's sixteen presets).
enum class SoundTab : int { MODEL, FAV, RECENT, ALL, BASS, LEAD, KEYS, CHORD, PAD, TEXTURE, FX, DRUM, COUNT };
inline constexpr int kSoundTabCount = static_cast<int>(SoundTab::COUNT);
const char* sound_tab_name(SoundTab tab);
// A category tab (ALL … DRUM) lists pack folders; FAV and RECENT list settings; MODEL lists neither.
bool sound_tab_is_category(SoundTab tab);

struct SoundEntry {
    std::string key;       // "Synths/01_Bass/Acid_Motion/Acid_Motion_Bite_Open.pti"
    std::string family;    // "Acid Motion"
    std::string variant;   // "Bite Open"
    std::string group;     // "BASS", "PLUCKS", "KICKS" — the pack folder, short
};

// A key is safe when it names a .pti below Synths/ or Drums/ with plain characters and no "..".
// Settings drop anything else (damaged or hand-edited files).
bool sound_key_valid(const std::string& key);
// The entry a key describes, from the key alone (no disk access).
SoundEntry sound_entry_from_key(const std::string& key);

// Each category tab's entries, read from disk the first time the tab is shown and kept for the
// session. The pack is bounded (2048 files in ~130 folders); the player's own library is never walked.
struct SoundLibrary {
    std::string root;                                   // the pack folder; "" until first use
    bool found = false;                                 // the pack folder exists
    std::array<bool, kSoundTabCount> scanned{};
    std::array<std::vector<SoundEntry>, kSoundTabCount> entries;
};

// The pack folder below the app's instrument folder.
std::string sound_pack_root(FileSystem& fs);
// Keep the catalog on the pack the app loads from now (Codex, STUFE1-BEFUND-ORDNERWECHSEL):
// - another root (Android SET_HOME / FORGET_ROOT, a new instruments folder) drops everything read,
//   so the next tab reads the new pack; stars and recent are relative keys and stay;
// - a pack that was missing is looked for again (one directory test, never a rescan), so a pack
//   copied in or a folder that became reachable appears without a restart.
// A changed pack under the same root reads anew after a root change or the next start. Cheap: a
// path compare and, only while missing, one stat. Called on every browser button and when it opens.
void sound_library_sync(SoundLibrary& lib, FileSystem& fs);
// The entries of `tab` (scanning it on first use); FAV and RECENT come from `keys`.
const std::vector<SoundEntry>& sound_tab_entries(SoundLibrary& lib, FileSystem& fs, SoundTab tab,
                                                 const std::vector<std::string>& favorites,
                                                 const std::vector<std::string>& recent,
                                                 std::vector<SoundEntry>& scratch);

// Settings lists: no duplicates, newest recent first, bounded. False when the favorites are full.
bool sound_toggle_favorite(std::vector<std::string>& favorites, const std::string& key, bool& nowStarred);
void sound_push_recent(std::vector<std::string>& recent, const std::string& key);
bool sound_is_favorite(const std::vector<std::string>& favorites, const std::string& key);

}  // namespace pt::ui
