#pragma once
// FM SYSEX's bank list for the Studio (UI thread). Banks 0…2 are Plaits' built-in ones; after them the
// .syx files of the app's "SysEx" folder, by name, as many as fit (studio::kFmMaxBanks in all). A file
// is read only up to fm_sysex::kMaxFileSize; one that holds no bank or voice message is left out and
// counted, never half-loaded. The chosen voice's bytes go into the instrument (songcore Instrument::
// fmVoice), so nothing here is needed to play a song back.
#include <array>
#include <string>
#include <vector>

#include "studio/fm_sysex.h"

namespace pt::ui {

class FileSystem;

struct FmLibraryBank {
    std::string name;                    // file name without ".syx"
    studio::fm_sysex::Bank bank;
};

struct FmLibrary {
    std::vector<FmLibraryBank> files;
    bool scanned = false;
    int skipped = 0;                     // .syx files that held nothing usable (or were too large)
    int badChecksums = 0;                // loaded, but their checksum did not match
};

// Rescan the folder. Leaves `library` as it was if listing fails outright.
void fm_library_scan(FmLibrary& library, FileSystem& fs);

int fm_library_bank_count(const FmLibrary& library);
// "Built-in A" … or the file's name.
std::string fm_library_bank_label(const FmLibrary& library, int bank);
// How many voices bank `bank` holds (32, or 1 for a single-voice file; 0 if out of range).
int fm_library_voice_count(const FmLibrary& library, int bank);
// The voice's bytes; false if out of range.
bool fm_library_voice(const FmLibrary& library, int bank, int voice, studio::fm_sysex::PackedVoice& out);

}  // namespace pt::ui
