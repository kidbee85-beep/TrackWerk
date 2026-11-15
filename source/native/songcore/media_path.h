#ifndef POCKETTRACKER_SONGCORE_MEDIA_PATH_H
#define POCKETTRACKER_SONGCORE_MEDIA_PATH_H

// A stored path made to mean something on THIS device: relative to the project, absolute under
// another install's root, or differing in case from a case-sensitive disk.
// ⚠️ Media paths and config.json folders must both re-root through `app_root_relative_tail`.

#include <filesystem>   // resolve_case_insensitive only
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../common/byte_source.h"  // pt_fopen

namespace songcore {

// An open probe. ⚠️ Through pt_fopen: a wrong "no" re-roots a correct path, and the load fails silently.
inline bool path_exists(const std::string& path) {
    if (path.empty()) return false;
    FILE* f = pt_fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// The app-root-relative tail ("Samples/Pads/kick.wav") of a path from ANOTHER install, or empty.
// Run only on a path that failed to open; a `pt://` URI is included (re-granting changes its id).
inline std::string app_root_relative_tail(const std::string& path) {
    // 1) Everything after the LAST "/PocketTracker/" (the phone's root). rfind, so a user sub-folder
    //    with that name loses to the real root above it.
    static const std::string kPtAnchor = "/PocketTracker/";
    const size_t pt = path.rfind(kPtAnchor);
    if (pt != std::string::npos) return path.substr(pt + kPtAnchor.size());

    // 2) A root not named PocketTracker: anchor on the media sub-tree, keeping it in the tail.
    static const std::string kSubtrees[] = { "/Samples/", "/Soundfonts/", "/Renders/" };
    size_t best = std::string::npos;
    for (const std::string& sub : kSubtrees) {
        const size_t at = path.rfind(sub);
        if (at != std::string::npos && (best == std::string::npos || at > best)) best = at;
    }
    if (best == std::string::npos) return "";
    return path.substr(best + 1);   // drop the leading '/', keep "Samples/…"
}

inline std::string to_lower_ascii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

// A path whose stored CASE may not match the disk: the longest existing prefix, then case-insensitive
// matches. The ORIGINAL path when none, so the failure names it. A URI is returned untouched.
inline std::string resolve_case_insensitive(const std::string& path) {
    if (path.empty() || pt_path_is_uri(path.c_str())) return path;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(fs::path(path), ec)) return path;   // exact hit — the common case

    // Split by hand rather than via fs::path: device roots like "//mnt/SDCARD/…" start with "//",
    // which fs::path treats in an implementation-defined way.
    const bool absolute = path[0] == '/' || path[0] == '\\';
    std::vector<std::string> parts;
    std::string cur;
    for (const char c : path) {
        if (c == '/' || c == '\\') { if (!cur.empty()) { parts.push_back(cur); cur.clear(); } }
        else                       { cur.push_back(c); }
    }
    if (!cur.empty()) parts.push_back(cur);

    fs::path have = absolute ? fs::path("/") : fs::path(".");
    size_t start = 0;
    if (!absolute && !parts.empty() && parts[0].size() == 2 && parts[0][1] == ':') {
        have = fs::path(parts[0] + "/");   // a Windows drive ("C:") anchors the walk
        start = 1;
    }
    if (!fs::exists(have, ec)) return path;   // nothing to anchor the walk on

    for (size_t i = start; i < parts.size(); ++i) {
        const fs::path exact = have / parts[i];
        if (fs::exists(exact, ec)) { have = exact; continue; }
        bool matched = false;
        if (fs::is_directory(have, ec)) {
            const std::string target = to_lower_ascii(parts[i]);
            for (const auto& entry : fs::directory_iterator(have, ec)) {
                if (to_lower_ascii(entry.path().filename().string()) == target) {
                    have = entry.path();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) return path;   // give up; let the failure name the intended path
    }
    return have.string();
}

// True for a path that names a place on its own. ⚠️ A `pt://` URI is absolute: read as relative, it
// would be joined onto a base dir into a string that resolves nowhere.
inline bool path_is_absolute(const std::string& path) {
    if (path.empty()) return false;
    return path[0] == '/' || path[0] == '\\' ||
           (path.size() > 1 && path[1] == ':') ||   // C:\… on Windows
           pt_path_is_uri(path.c_str());
}

// A readable WAV with a "fmt " chunk and a non-empty "data" chunk inside the file. A backup must be
// one before it may stand in for a file, or be moved anywhere: a folder, a foreign file or a broken
// WAV under that name stays put.
inline bool is_wav_file(const std::string& path) {
    std::FILE* f = pt_fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = false, fmt = false;
    unsigned char h[12] = {};
    long fileLen = -1;
    if (std::fseek(f, 0, SEEK_END) == 0) fileLen = std::ftell(f);
    if (fileLen >= 44 && std::fseek(f, 0, SEEK_SET) == 0 && std::fread(h, 1, 12, f) == 12 &&
        std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WAVE", 4) == 0) {
        for (int chunks = 0; chunks < 64; ++chunks) {
            unsigned char c[8];
            if (std::fread(c, 1, 8, f) != 8) break;
            const unsigned long size = c[4] | (c[5] << 8) | (c[6] << 16) | (static_cast<unsigned long>(c[7]) << 24);
            const long at = std::ftell(f);
            if (at < 0 || size > static_cast<unsigned long>(fileLen - at)) break;   // runs past the end
            if (std::memcmp(c, "fmt ", 4) == 0) fmt = size >= 16;
            if (std::memcmp(c, "data", 4) == 0) { ok = fmt && size > 0; break; }
            if (std::fseek(f, static_cast<long>(size + (size & 1)), SEEK_CUR) != 0) break;
        }
    }
    std::fclose(f);
    return ok;
}

// A media file is there — or recoverably there: an interrupted save can leave the original only at
// "<path>.ptbak", which the load puts back (recover_interrupted_save). The resolution must find it
// where it would have found the file, not look elsewhere — but only a usable WAV counts.
inline bool media_present(const std::string& path) {
    return path_exists(path) || is_wav_file(path + ".ptbak");
}

// Absolute wins; relative resolves against base_dir. A missing absolute path from another install is
// re-rooted onto `app_root` (empty: never). The stored string is never rewritten.
inline std::string resolve_media_path(const std::string& path, const std::string& base_dir,
                                      const std::string& app_root) {
    if (path.empty() || path.rfind("/@mutable/",0)==0) return path;
    const bool absolute = path_is_absolute(path);

    std::string resolved = (!absolute && !base_dir.empty()) ? base_dir + "/" + path : path;

    // Absolute wins — unless it points nowhere here and has an app-tree tail.
    if (absolute && !app_root.empty() && !media_present(resolved)) {
        const std::string tail = app_root_relative_tail(resolved);
        if (!tail.empty()) resolved = app_root + "/" + tail;
    }

    // Last: fix any case drift. A no-op when the path exists exactly.
    return resolve_case_insensitive(resolved);
}

// The app folder's sub-folder COLLECT writes each song's folder into.
inline constexpr const char* SONGS_FOLDER = "Songs";

// Where COLLECT puts a media file inside a song's folder: its place under the app folder
// ("Samples/Drums/kick.wav"), or Samples/ or Soundfonts/ and the bare name for a file from outside it.
// ⚠️ COLLECT and the lookup below must both derive it here, or a collected copy is never found.
inline std::string collected_media_path(const std::string& path) {
    std::string tail = path_is_absolute(path) ? app_root_relative_tail(path) : path;
    // A file from another collected song lands where that song kept it, not under Songs/ again.
    if (tail.rfind("Songs/", 0) == 0) {
        const size_t song_end = tail.find('/', 6);
        tail = (song_end == std::string::npos) ? std::string() : tail.substr(song_end + 1);
    }
    if (!tail.empty()) return tail;

    const size_t slash = path.find_last_of("/\\");
    const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const std::string ext = to_lower_ascii(name.substr(name.find_last_of('.') + 1));
    return (ext == "sf2" || ext == "sf3" ? "Soundfonts/" : "Samples/") + name;
}

// The folders a stored media path resolves against. All empty ⇒ paths are used as written.
struct MediaRoots {
    std::string baseDir;        // a relative path joins onto it first
    std::string appRoot;        // this install's app folder — where a foreign absolute path is re-rooted
    std::string projectDir;     // the opened .ptp's folder — a collected song's own copies
    std::string collectedDir;   // Songs/<project name> — COLLECT's copies, when the originals are gone
};

// As above; a file found nowhere there is looked for where a collected song keeps it — beside the
// project, under the app folder, then in the song's Songs/ folder.
inline std::string resolve_media_path(const std::string& path, const MediaRoots& roots) {
    const std::string first = resolve_media_path(path, roots.baseDir, roots.appRoot);
    if (path.empty() || media_present(first)) return first;

    const std::string rel = collected_media_path(path);
    std::vector<std::string> candidates;
    if (!path_is_absolute(path)) {
        if (!roots.projectDir.empty()) candidates.push_back(roots.projectDir + "/" + path);
        if (!roots.appRoot.empty())    candidates.push_back(roots.appRoot + "/" + path);
    }
    if (!roots.collectedDir.empty()) candidates.push_back(roots.collectedDir + "/" + rel);

    for (const std::string& c : candidates) {
        const std::string found = resolve_case_insensitive(c);
        if (media_present(found)) return found;
    }
    return first;   // the failure names the path as stored
}

}  // namespace songcore

#endif  // POCKETTRACKER_SONGCORE_MEDIA_PATH_H
