#include "ui/fm_library.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "studio/synth.h"
#include "ui/filesystem.h"

namespace pt::ui {

namespace {
bool is_syx(const FileInfo& f) {
    if (f.isDirectory || f.isAction || f.isRoot) return false;
    std::string ext = f.extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == "syx";
}
}  // namespace

void fm_library_scan(FmLibrary& library, FileSystem& fs) {
    const std::string dir = fs.make_app_folder("SysEx");
    if (dir.empty()) return;
    auto entries = fs.list_files(dir);
    std::vector<FileInfo> files;
    for (auto& e : entries)
        if (is_syx(e)) files.push_back(std::move(e));
    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
    FmLibrary next;
    next.scanned = true;
    const size_t room = size_t(studio::kFmMaxBanks - studio::kFmFactoryBanks);
    for (const auto& f : files) {
        if (next.files.size() >= room) break;
        std::string content;
        if (f.size > int64_t(studio::fm_sysex::kMaxFileSize) || !fs.read_file(f.path, content) ||
            content.size() > studio::fm_sysex::kMaxFileSize) {
            ++next.skipped;
            continue;
        }
        FmLibraryBank bank;
        bank.bank = studio::fm_sysex::parse(reinterpret_cast<const uint8_t*>(content.data()), content.size());
        if (bank.bank.count == 0) {
            ++next.skipped;
            continue;
        }
        if (!bank.bank.checksumOk) ++next.badChecksums;
        bank.name = f.name_without_extension();
        next.files.push_back(std::move(bank));
    }
    library = std::move(next);
}

int fm_library_bank_count(const FmLibrary& library) {
    return studio::kFmFactoryBanks + int(library.files.size());
}

std::string fm_library_bank_label(const FmLibrary& library, int bank) {
    if (bank >= 0 && bank < studio::kFmFactoryBanks) return std::string("Built-in ") + char('A' + bank);
    const int file = bank - studio::kFmFactoryBanks;
    if (file >= 0 && file < int(library.files.size())) return library.files[size_t(file)].name;
    return "Bank " + std::to_string(bank - studio::kFmFactoryBanks + 1) + " (missing)";
}

int fm_library_voice_count(const FmLibrary& library, int bank) {
    if (bank >= 0 && bank < studio::kFmFactoryBanks) return studio::fm_sysex::kVoicesPerBank;
    const int file = bank - studio::kFmFactoryBanks;
    if (file >= 0 && file < int(library.files.size())) return library.files[size_t(file)].bank.count;
    return 0;
}

bool fm_library_voice(const FmLibrary& library, int bank, int voice, studio::fm_sysex::PackedVoice& out) {
    if (voice < 0 || voice >= fm_library_voice_count(library, bank)) return false;
    if (bank < studio::kFmFactoryBanks) {
        studio::fm_builtin_voice(bank, voice, out.data());
        return true;
    }
    out = library.files[size_t(bank - studio::kFmFactoryBanks)].bank.voices[size_t(voice)];
    return true;
}

}  // namespace pt::ui
