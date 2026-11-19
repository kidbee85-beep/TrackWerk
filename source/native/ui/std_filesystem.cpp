#include "ui/std_filesystem.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
// The only platform header in pt-ui: "where is Documents" has no portable answer.
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace pt::ui {

namespace {

// `file_time_type` → Unix ms. C++17 cannot name that clock's epoch, so the offset is measured now.
int64_t to_unix_millis(fs::file_time_type t) {
    using namespace std::chrono;
    const auto shifted = time_point_cast<system_clock::duration>(
        t - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<milliseconds>(shifted.time_since_epoch()).count();
}

// Forward slashes always — the browser draws the path.
std::string generic(const fs::path& p) { return p.generic_string(); }

const char* env_or_null(const char* key) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : nullptr;
}

#if defined(_WIN32)

// UTF-16 → the narrow encoding. ⚠️ `CP_ACP`, not `CP_UTF8`: MSVC decodes narrow paths with the active
// code page, and UTF-8 bytes there make non-ASCII paths silently not exist.
std::string narrow(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    const int n = ::WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};                       // the terminator alone
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_ACP, 0, wide, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// Documents as Explorer resolves it — under OneDrive backup that is not `%USERPROFILE%\Documents`.
std::string documents_directory() {
    PWSTR         wide = nullptr;
    const HRESULT hr   = ::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &wide);
    std::string   out;
    if (SUCCEEDED(hr)) out = narrow(wide);
    ::CoTaskMemFree(wide);   // required even on failure (where it is handed nullptr)
    return out;
}

#endif  // _WIN32

}  // namespace

// ─── Path helpers ────────────────────────────────────────────────────────────────────────────────

std::string path_name(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string path_extension(const std::string& path) {
    const std::string name = path_name(path);
    const size_t      dot  = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : name.substr(dot + 1);
}

std::string path_stem(const std::string& path) {
    const std::string name = path_name(path);
    const size_t      dot  = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string path_sanitize(const std::string& name, bool allow_dot) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || (allow_dot && c == '.');
        out.push_back(ok ? c : '_');
    }
    return out;
}

bool path_is_within(const std::string& path, const std::string& ancestor) {
    const auto norm = [](std::string p) {
        for (char& c : p) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        return p;
    };
    const std::string p = norm(path), a = norm(ancestor);
    if (a.empty()) return false;
    return p == a || (p.size() > a.size() && p.compare(0, a.size(), a) == 0 && p[a.size()] == '/');
}

std::string to_lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

// Move an unconfigured desktop home only when the new destination is absent. A denied rename
// retains the old root, so an update cannot hide the owner's projects. Saved absolute media and
// config paths re-root through media_path.h's current and legacy anchors.
static std::string canonical_desktop_root(const fs::path& parent) {
    const fs::path current = parent / "TrackWerk";
    const fs::path previous = parent / "PocketTracker";
    std::error_code ec;
    if (fs::exists(current, ec) && !fs::is_directory(current, ec) && fs::is_directory(previous, ec))
        return generic(previous);  // retain access when an unrelated file owns the new name
    if (!fs::exists(current, ec) && fs::is_directory(previous, ec)) {
        if (fs::is_symlink(previous, ec)) return generic(previous);
        fs::rename(previous, current, ec);
        if (ec) return generic(previous);
    }
    return generic(current);
}

std::string default_app_root() {
    if (const char* home = env_or_null("TRACKWERK_HOME")) return home;
    if (const char* home = env_or_null("POCKETTRACKER_HOME")) return home;
#if defined(_WIN32)
    if (const std::string docs = documents_directory(); !docs.empty())
        return canonical_desktop_root(fs::path(docs));
    if (const char* profile = env_or_null("USERPROFILE"))
        return canonical_desktop_root(fs::path(profile) / "Documents");
#elif defined(__APPLE__)
    if (const char* home = env_or_null("HOME")) return canonical_desktop_root(fs::path(home) / "Documents");
#else
    if (const char* xdg = env_or_null("XDG_DATA_HOME")) return canonical_desktop_root(fs::path(xdg));
    if (const char* home = env_or_null("HOME"))
        return canonical_desktop_root(fs::path(home) / ".local/share");
#endif
    return "TrackWerk";
}

// ─── StdFileSystem ───────────────────────────────────────────────────────────────────────────────

std::string StdFileSystem::ensure_dir(const char* sub) {
    const fs::path dir = fs::path(root_) / sub;
    std::error_code ec;
    fs::create_directories(dir, ec);   // a failure shows as an empty listing
    return generic(dir);
}

bool StdFileSystem::read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::vector<FileInfo> StdFileSystem::list_files(const std::string& directory) {
    std::vector<FileInfo> out;
    std::error_code ec;

    // Non-throwing throughout: a card pulled mid-walk gives a short listing, never an exception.
    fs::directory_iterator it(directory, ec);
    if (ec) return out;

    for (const fs::directory_entry& e : it) {
        std::error_code ec2;
        const bool dir = e.is_directory(ec2);
        if (ec2) continue;

        FileInfo info;
        info.path        = generic(e.path());
        info.name        = path_name(info.path);
        info.extension   = dir ? std::string() : path_extension(info.name);
        info.isDirectory = dir;
        info.size        = dir ? 0 : static_cast<int64_t>(fs::file_size(e.path(), ec2));
        if (ec2) info.size = 0;
        info.lastModified = to_unix_millis(fs::last_write_time(e.path(), ec2));
        if (ec2) info.lastModified = 0;

        out.push_back(std::move(info));
    }
    return out;
}

bool StdFileSystem::file_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

bool StdFileSystem::is_directory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

std::string StdFileSystem::parent_path(const std::string& path) {
    const fs::path p      = fs::path(path);
    const fs::path parent = p.parent_path();
    // A filesystem root is its own parent.
    if (parent.empty() || parent == p) return "";
    return generic(parent);
}

bool StdFileSystem::write_file(const std::string& path, const std::string& content) {
    return write_bytes(path, content.data(), content.size());
}

#if !defined(_WIN32)
namespace {
// The temp file's bytes written AND forced to the medium before the rename puts it in place. Without
// the sync a rename can reach a card first (FAT and exFAT keep no order, ext4 only a heuristic): a
// power cut, or a card pulled right after SAVE, then leaves the new name on an empty or short file.
// Only a real write failure (EIO, a full card or quota) fails the save; a filesystem that cannot sync
// at all (EINVAL, ENOTSUP) must not turn every save into a failure.
bool write_synced(const std::string& path, const void* data, size_t len, bool sync) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    const char* p = static_cast<const char*>(data);
    size_t left = len;
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    bool ok = true;
#if defined(__APPLE__) && defined(F_FULLFSYNC)
    if (sync && ::fcntl(fd, F_FULLFSYNC) != 0)   // the drive's own cache too; fall back to fsync below
#else
    if (sync)
#endif
    {
        int r;
        do { r = ::fsync(fd); } while (r < 0 && errno == EINTR);
        if (r < 0 && (errno == EIO || errno == ENOSPC
#ifdef EDQUOT
                      || errno == EDQUOT
#endif
                      )) ok = false;
    }
    if (::close(fd) != 0) ok = false;
    return ok;
}
// Best effort: the rename itself reaches the medium. Errors are ignored (not every filesystem can).
void sync_directory(const std::string& file) {
    const fs::path parent = fs::path(file).parent_path();
    const int fd = ::open((parent.empty() ? fs::path(".") : parent).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    int r;
    do { r = ::fsync(fd); } while (r < 0 && errno == EINTR);
    ::close(fd);
}
}  // namespace
#endif

bool StdFileSystem::write_file_relaxed(const std::string& path, const std::string& content) {
    return write_bytes_impl(path, content.data(), content.size(), /*sync=*/false);
}

bool StdFileSystem::write_bytes(const std::string& path, const void* data, size_t len) {
    return write_bytes_impl(path, data, len, /*sync=*/true);
}

bool StdFileSystem::write_bytes_impl(const std::string& path, const void* data, size_t len, bool sync) {
    (void)sync;   // Windows keeps its flush-and-close write
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    // Through `<path>.tmp` and a rename, so power loss mid-save leaves the old file whole.
    const std::string tmp = path + ".tmp";
#if !defined(_WIN32)
    if (!write_synced(tmp, data, len, sync)) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
#else
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        if (len > 0) f.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));

        // ⚠️ Closed and checked here: a full disk shows up in this flush, and the destructor would
        // swallow it — then the rename would put an empty file where the project was.
        f.close();
        if (!f) {
            std::error_code ignored;
            fs::remove(tmp, ignored);
            return false;
        }
    }
#endif

    fs::rename(tmp, path, ec);
    if (ec) {
        // rename(2) fails across filesystems: copy, then drop the temp.
        ec.clear();
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
        std::error_code ignored;
        fs::remove(tmp, ignored);
        if (ec) return false;
    }
#if !defined(_WIN32)
    if (sync) sync_directory(path);
#endif
    return true;
}

bool StdFileSystem::delete_path(const std::string& path) {
    std::error_code ec;
    return fs::remove_all(path, ec) > 0 && !ec;
}

bool StdFileSystem::rename_file(const std::string& path, const std::string& new_base_name) {
    std::error_code ec;
    const bool dir = is_directory(path);
    const std::string ext = dir ? std::string() : path_extension(path);

    std::string safe = path_sanitize(new_base_name, /*allow_dot=*/true);
    if (safe.empty()) return false;

    // Keep the original extension, unless the typed name already ends in it.
    std::string final_name = safe;
    if (!ext.empty()) {
        const std::string suffix = "." + ext;
        // Any case: "Snare.wav" for a .WAV keeps its own spelling, never "Snare.wav.WAV".
        const bool has_suffix = safe.size() > suffix.size() &&
                                to_lower(safe.substr(safe.size() - suffix.size())) == to_lower(suffix);
        if (!has_suffix) final_name = safe + suffix;
    }

    const fs::path target = fs::path(path).parent_path() / final_name;
    if (target.filename() == fs::path(path).filename()) return true;   // nothing to do
    if (fs::exists(target, ec)) {
        // A case-insensitive volume (FAT card, Windows, macOS) finds the file itself under its new
        // spelling: "song" → "SONG". Through a free temporary name, as the volume will not rename a
        // file onto itself. Anything else there is another file: never overwrite.
        ec.clear();
        if (!fs::equivalent(path, target, ec) || ec) return false;
        const fs::path step = fs::path(path).parent_path() / (final_name + ".renaming");
        if (fs::exists(step, ec)) return false;
        ec.clear();
        fs::rename(path, step, ec);
        if (ec) return false;
        fs::rename(step, target, ec);
        if (ec) { std::error_code back; fs::rename(step, path, back); return false; }
        return true;
    }

    ec.clear();
    fs::rename(path, target, ec);
    return !ec;
}

std::string StdFileSystem::create_folder(const std::string& parent, const std::string& folder_name) {
    const std::string safe = path_sanitize(folder_name, /*allow_dot=*/false);
    if (safe.empty()) return "";

    const fs::path target = fs::path(parent) / safe;
    std::error_code ec;
    if (fs::exists(target, ec)) return "";

    ec.clear();
    if (!fs::create_directories(target, ec) || ec) return "";
    return generic(target);
}

bool StdFileSystem::move_file(const std::string& from, const std::string& to) {
    if (path_is_within(to, from)) return false;   // into itself: the copy fallback would never end
    std::error_code ec;
    fs::create_directories(fs::path(to).parent_path(), ec);

    ec.clear();
    fs::rename(from, to, ec);
    if (!ec) return true;

    // Across filesystems: copy, then drop the source.
    ec.clear();
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) return false;

    std::error_code ignored;
    fs::remove_all(from, ignored);
    return true;
}

bool StdFileSystem::copy_file(const std::string& from, const std::string& to) {
    if (path_is_within(to, from)) return false;   // into itself: the recursive copy would never end
    std::error_code ec;
    fs::create_directories(fs::path(to).parent_path(), ec);

    ec.clear();
    // An existing target fails rather than merges.
    fs::copy(from, to, fs::copy_options::recursive, ec);
    return !ec;
}

}  // namespace pt::ui
