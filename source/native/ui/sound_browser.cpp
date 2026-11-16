#include "ui/sound_browser.h"

#include "ui/filesystem.h"

#include <algorithm>
#include <cctype>

namespace pt::ui {
namespace {

// The pack folders behind each category tab. A folder appears in exactly one tab, so ALL lists each
// sound once. Leads have their own tab: they are neither keys nor pads.
struct Folder { SoundTab tab; const char* path; const char* group; };
constexpr Folder kFolders[] = {
    {SoundTab::BASS, "Synths/01_Bass", "BASS"},
    {SoundTab::LEAD, "Synths/02_Leads", "LEAD"},
    {SoundTab::PAD, "Synths/03_Pads", "PAD"},
    {SoundTab::KEYS, "Synths/04_Keys", "KEYS"},
    {SoundTab::KEYS, "Synths/05_Plucks", "PLUCK"},
    {SoundTab::KEYS, "Synths/06_Bells-Mallets", "BELL"},
    {SoundTab::PAD, "Synths/07_Strings-Bowed", "STRING"},
    {SoundTab::CHORD, "Synths/08_Chords-Organs", "CHORD"},
    {SoundTab::TEXTURE, "Synths/09_Textures-Drones", "TEXTURE"},
    {SoundTab::FX, "Synths/10_FX-Noise", "FX"},
    {SoundTab::DRUM, "Drums", ""},   // sixteen drum folders, each its own group
};

std::string spaced(std::string s) {
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}

// "05_Open-Hats" → "OPEN-HATS"
std::string group_of(const std::string& folder) {
    const auto u = folder.find('_');
    std::string g = u == std::string::npos ? folder : folder.substr(u + 1);
    for (char& c : g) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return g;
}

std::vector<std::string> split(const std::string& key) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= key.size(); ++i)
        if (i == key.size() || key[i] == '/') { parts.push_back(key.substr(start, i - start)); start = i + 1; }
    return parts;
}

std::vector<FileInfo> sorted(std::vector<FileInfo> v) {
    std::sort(v.begin(), v.end(), [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
    return v;
}

bool is_pti(const FileInfo& f) {
    if (f.isDirectory || f.name.size() < 5) return false;
    std::string ext = f.extension;
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "pti" || ext == ".pti";
}

// Every .pti of a sound-family folder layout: <dir>/<family>/<file>.pti, keys relative to the pack.
void scan_folder(FileSystem& fs, const std::string& root, const std::string& rel, std::vector<SoundEntry>& out) {
    for (const FileInfo& family : sorted(fs.list_files(root + "/" + rel))) {
        if (!family.isDirectory || family.name.empty() || family.name[0] == '.') continue;
        for (const FileInfo& file : sorted(fs.list_files(family.path))) {
            if (!is_pti(file)) continue;
            const std::string key = rel + "/" + family.name + "/" + file.name;
            if (sound_key_valid(key)) out.push_back(sound_entry_from_key(key));
        }
    }
}

void scan(SoundLibrary& lib, FileSystem& fs, SoundTab tab) {
    auto& out = lib.entries[static_cast<size_t>(tab)];
    out.clear();
    if (tab == SoundTab::ALL) {
        for (int t = static_cast<int>(SoundTab::BASS); t < kSoundTabCount; ++t) {
            const auto ct = static_cast<SoundTab>(t);
            if (!lib.scanned[static_cast<size_t>(t)]) scan(lib, fs, ct);
            const auto& v = lib.entries[static_cast<size_t>(t)];
            out.insert(out.end(), v.begin(), v.end());
        }
    } else {
        for (const Folder& f : kFolders) {
            if (f.tab != tab) continue;
            if (tab == SoundTab::DRUM) {
                for (const FileInfo& d : sorted(fs.list_files(lib.root + "/" + f.path)))
                    if (d.isDirectory && !d.name.empty() && d.name[0] != '.')
                        scan_folder(fs, lib.root, std::string(f.path) + "/" + d.name, out);
            } else {
                scan_folder(fs, lib.root, f.path, out);
            }
        }
    }
    lib.scanned[static_cast<size_t>(tab)] = true;
}

}  // namespace

const char* sound_tab_name(SoundTab tab) {
    static const char* names[kSoundTabCount] = {"MODEL", "FAV", "RECENT", "ALL", "BASS", "LEAD",
                                                "KEYS", "CHORD", "PAD", "TEXTURE", "FX", "DRUM"};
    const int i = static_cast<int>(tab);
    return i >= 0 && i < kSoundTabCount ? names[i] : "";
}

bool sound_tab_is_category(SoundTab tab) {
    return static_cast<int>(tab) >= static_cast<int>(SoundTab::ALL) && static_cast<int>(tab) < kSoundTabCount;
}

bool sound_key_valid(const std::string& key) {
    if (key.size() < 12 || key.size() > 160) return false;
    if (key.rfind("Synths/", 0) != 0 && key.rfind("Drums/", 0) != 0) return false;
    if (key.compare(key.size() - 4, 4, ".pti") != 0) return false;
    if (key.find("..") != std::string::npos || key.find("//") != std::string::npos) return false;
    for (const char c : key)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/')) return false;
    const auto parts = split(key);
    // Synths/<category>/<family>/<file>, Drums/<category>/<family>/<file>
    return parts.size() == 4 && !parts[1].empty() && !parts[2].empty() && parts[3].size() > 4;
}

SoundEntry sound_entry_from_key(const std::string& key) {
    SoundEntry e;
    e.key = key;
    const auto parts = split(key);
    if (parts.size() != 4) { e.family = key; return e; }
    const std::string& familyFolder = parts[2];
    std::string stem = parts[3].substr(0, parts[3].size() - 4);
    // The file repeats its family: "Acid_Motion_Bite_Open" → "Bite Open".
    if (stem.rfind(familyFolder + "_", 0) == 0) stem.erase(0, familyFolder.size() + 1);
    e.family = spaced(familyFolder);
    e.variant = spaced(stem);
    for (const Folder& f : kFolders)
        if (parts[0] + "/" + parts[1] == f.path && *f.group) e.group = f.group;
    if (e.group.empty()) e.group = group_of(parts[1]);
    return e;
}

std::string sound_pack_root(FileSystem& fs) {
    return fs.instruments_directory() + "/" + kSoundPackFolder;
}

void sound_library_sync(SoundLibrary& lib, FileSystem& fs) {
    const std::string root = sound_pack_root(fs);
    if (root != lib.root) {
        lib = SoundLibrary{};
        lib.root = root;
        lib.found = fs.is_directory(root);
        return;
    }
    if (!lib.found && fs.is_directory(root)) {
        lib.found = true;
        lib.scanned.fill(false);   // tabs read while it was missing are empty: read them again
        for (auto& v : lib.entries) v.clear();
    }
}

const std::vector<SoundEntry>& sound_tab_entries(SoundLibrary& lib, FileSystem& fs, SoundTab tab,
                                                 const std::vector<std::string>& favorites,
                                                 const std::vector<std::string>& recent,
                                                 std::vector<SoundEntry>& scratch) {
    if (tab == SoundTab::FAV || tab == SoundTab::RECENT) {
        scratch.clear();
        for (const auto& key : tab == SoundTab::FAV ? favorites : recent)
            if (sound_key_valid(key)) scratch.push_back(sound_entry_from_key(key));
        return scratch;
    }
    if (!sound_tab_is_category(tab)) { scratch.clear(); return scratch; }
    sound_library_sync(lib, fs);
    if (!lib.scanned[static_cast<size_t>(tab)]) {
        if (lib.found) scan(lib, fs, tab);
        else lib.scanned[static_cast<size_t>(tab)] = true;
    }
    return lib.entries[static_cast<size_t>(tab)];
}

bool sound_is_favorite(const std::vector<std::string>& favorites, const std::string& key) {
    return std::find(favorites.begin(), favorites.end(), key) != favorites.end();
}

bool sound_toggle_favorite(std::vector<std::string>& favorites, const std::string& key, bool& nowStarred) {
    const auto it = std::find(favorites.begin(), favorites.end(), key);
    if (it != favorites.end()) { favorites.erase(it); nowStarred = false; return true; }
    if (static_cast<int>(favorites.size()) >= kSoundFavoritesMax) { nowStarred = false; return false; }
    favorites.push_back(key);
    nowStarred = true;
    return true;
}

void sound_push_recent(std::vector<std::string>& recent, const std::string& key) {
    recent.erase(std::remove(recent.begin(), recent.end(), key), recent.end());
    recent.insert(recent.begin(), key);
    if (static_cast<int>(recent.size()) > kSoundRecentMax) recent.resize(kSoundRecentMax);
}

}  // namespace pt::ui
