#pragma once
// Licences of found sounds and the credits a song owes them.
//
// Every take the Source Sampler renders or records carries `<sample>.source.json` (title, creator,
// source_url, license_url, in/out, live). Here, on every platform alike:
//   - the licence link is sorted into a few kinds a musician can act on (source_license_kind),
//   - the Sources screen's filter hides what a release cannot use (source_license_allowed),
//   - EXPORT and COLLECT write a credits file listing every found sound the song plays and
//     flagging those not cleared for a release (source_credits_*).
// This is a reading aid, not legal advice: the licence page linked in the file is what counts.
#include <string>
#include <vector>

#include "songcore/media_path.h"
#include "songcore/model.h"

namespace pt::ui {

class FileSystem;

enum class SourceLicense {
    PUBLIC_DOMAIN,    // no rights left (public domain mark, …/publicdomain/…)
    CC0,              // waived; no credit needed
    CC_BY,            // credit the creator
    CC_BY_SA,         // credit, share the result under the same licence (also FAL, GPL/FDL)
    NONCOMMERCIAL,    // …-nc-…: no commercial release
    NO_DERIVATIVES,   // …-nd-…: no sampling at all
    BROADCAST,        // a radio recording: the station's licence does not cover sampling
    UNKNOWN,          // no or unreadable licence: ask the rights holder
};

// From a licence link or text (Internet Archive `licenseurl`, Commons `LicenseUrl`, YouTube's
// "Creative Commons Attribution license (reuse allowed)"). `live` marks a radio stream.
SourceLicense source_license_kind(const std::string& license, bool live = false);
// "PD", "CC0", "BY", "BY-SA", "NC", "ND", "LIVE", "?" — fits a list row.
const char* source_license_badge(SourceLicense);
// "Public domain", "CC BY (credit the creator)", … — for the credits file.
const char* source_license_name(SourceLicense);
// True when a song using it can be released commercially (with the credit the licence asks for).
bool source_license_release_safe(SourceLicense);

// The Sources screen's licence filter.
enum class SourceLicenseFilter { ALL, RELEASE_SAFE, NO_CREDIT, COUNT };
const char* source_license_filter_name(SourceLicenseFilter);
bool source_license_allowed(SourceLicense, SourceLicenseFilter);

// One found sound and the instruments that play it.
struct SourceCredit {
    std::string title, creator, sourceUrl, licenseUrl, sample;
    SourceLicense license = SourceLicense::UNKNOWN;
    long long inMs = -1, outMs = -1;
    bool live = false;
    std::vector<int> instruments;   // slots, ascending
};

// Every sample instrument whose file has a readable `.source.json`; one entry per file.
std::vector<SourceCredit> source_credits_collect(const songcore::Project&, const songcore::MediaRoots&,
                                                 FileSystem&);
// The text of the credits file: each sound with creator, licence and links, then the ones to clear.
std::string source_credits_text(const songcore::Project&, const std::vector<SourceCredit>&);
// Writes the credits file to `path` when the song plays at least one found sound. Returns the
// number of sounds credited (0: nothing found, nothing written), −1 when writing failed.
int source_credits_write(const songcore::Project&, const songcore::MediaRoots&, FileSystem&,
                         const std::string& path);

}  // namespace pt::ui
