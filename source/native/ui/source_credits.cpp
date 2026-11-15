#include "ui/source_credits.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "songcore/project_io.h"
#include "ui/filesystem.h"

namespace pt::ui {
namespace {

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// The licence code in a creativecommons.org/licenses/<code>/… link, split at '-': by, sa, nc, nd.
SourceLicense cc_code(const std::string& code) {
    bool nc = false, nd = false, sa = false;
    size_t start = 0;
    while (start <= code.size()) {
        const size_t end = std::min(code.find('-', start), code.size());
        const std::string part = code.substr(start, end - start);
        nc |= part == "nc";
        nd |= part == "nd";
        sa |= part == "sa";
        start = end + 1;
    }
    if (nd) return SourceLicense::NO_DERIVATIVES;   // worse than NC for sampling: no use at all
    if (nc) return SourceLicense::NONCOMMERCIAL;
    return sa ? SourceLicense::CC_BY_SA : SourceLicense::CC_BY;
}

std::string text(const songcore::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>().substr(0, 600) : std::string{};
}

long long number(const songcore::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<long long>() : -1;
}

std::string clock(long long ms) {
    if (ms < 0) return "?";
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%lld:%02lld.%03lld", ms / 60000, ms / 1000 % 60, ms % 1000);
    return buffer;
}

}  // namespace

SourceLicense source_license_kind(const std::string& license, bool live) {
    if (live) return SourceLicense::BROADCAST;
    const std::string l = lower(license);
    if (l.find_first_not_of(" \t\r\n") == std::string::npos) return SourceLicense::UNKNOWN;
    if (contains(l, "creativecommons.org/publicdomain/zero") || contains(l, "cc0")) return SourceLicense::CC0;
    if (contains(l, "publicdomain") || contains(l, "public domain") || contains(l, "public-domain"))
        return SourceLicense::PUBLIC_DOMAIN;
    if (const size_t at = l.find("creativecommons.org/licenses/"); at != std::string::npos) {
        const size_t from = at + std::char_traits<char>::length("creativecommons.org/licenses/");
        const std::string code = l.substr(from, l.find('/', from) - from);
        if (code.rfind("by", 0) == 0) return cc_code(code);
        return SourceLicense::UNKNOWN;   // sampling+, devnations …: retired, check the page
    }
    // Text forms, YouTube's included.
    if (contains(l, "creative commons")) {
        if (contains(l, "noderivative") || contains(l, "no derivative")) return SourceLicense::NO_DERIVATIVES;
        if (contains(l, "noncommercial") || contains(l, "non-commercial")) return SourceLicense::NONCOMMERCIAL;
        if (contains(l, "sharealike") || contains(l, "share alike") || contains(l, "share-alike"))
            return SourceLicense::CC_BY_SA;
        if (contains(l, "attribution")) return SourceLicense::CC_BY;
        return SourceLicense::UNKNOWN;
    }
    // Copyleft licences that allow commercial use with credit and the same licence.
    if (contains(l, "artlibre.org") || contains(l, "gnu.org/licenses/gpl") || contains(l, "gnu.org/licenses/fdl") ||
        contains(l, "gnu.org/copyleft"))
        return SourceLicense::CC_BY_SA;
    return SourceLicense::UNKNOWN;
}

const char* source_license_badge(SourceLicense kind) {
    switch (kind) {
        case SourceLicense::PUBLIC_DOMAIN:  return "PD";
        case SourceLicense::CC0:            return "CC0";
        case SourceLicense::CC_BY:          return "BY";
        case SourceLicense::CC_BY_SA:       return "BY-SA";
        case SourceLicense::NONCOMMERCIAL:  return "NC";
        case SourceLicense::NO_DERIVATIVES: return "ND";
        case SourceLicense::BROADCAST:      return "LIVE";
        case SourceLicense::UNKNOWN:        break;
    }
    return "?";
}

const char* source_license_name(SourceLicense kind) {
    switch (kind) {
        case SourceLicense::PUBLIC_DOMAIN:  return "Public domain";
        case SourceLicense::CC0:            return "CC0 (no credit required)";
        case SourceLicense::CC_BY:          return "CC BY (credit the creator)";
        case SourceLicense::CC_BY_SA:       return "CC BY-SA or copyleft (credit, share alike)";
        case SourceLicense::NONCOMMERCIAL:  return "Non-commercial (NC): no commercial release";
        case SourceLicense::NO_DERIVATIVES: return "No derivatives (ND): sampling not allowed";
        case SourceLicense::BROADCAST:      return "Radio recording: broadcast rights do not cover sampling";
        case SourceLicense::UNKNOWN:        break;
    }
    return "Licence unknown: ask the rights holder";
}

bool source_license_release_safe(SourceLicense kind) {
    return kind == SourceLicense::PUBLIC_DOMAIN || kind == SourceLicense::CC0 || kind == SourceLicense::CC_BY ||
           kind == SourceLicense::CC_BY_SA;
}

const char* source_license_filter_name(SourceLicenseFilter filter) {
    switch (filter) {
        case SourceLicenseFilter::RELEASE_SAFE: return "Release-safe";
        case SourceLicenseFilter::NO_CREDIT:    return "No credit needed";
        default:                                return "All licenses";
    }
}

bool source_license_allowed(SourceLicense kind, SourceLicenseFilter filter) {
    switch (filter) {
        case SourceLicenseFilter::RELEASE_SAFE: return source_license_release_safe(kind);
        case SourceLicenseFilter::NO_CREDIT:
            return kind == SourceLicense::PUBLIC_DOMAIN || kind == SourceLicense::CC0;
        default: return true;
    }
}

std::vector<SourceCredit> source_credits_collect(const songcore::Project& project, const songcore::MediaRoots& roots,
                                                 FileSystem& fs) {
    std::vector<SourceCredit> credits;
    for (size_t slot = 0; slot < project.instruments.size(); ++slot) {
        const auto& ins = project.instruments[slot];
        if (ins.instrumentType == songcore::InstrumentType::SOUNDFONT || !ins.sampleFilePath) continue;
        const std::string& stored = *ins.sampleFilePath;
        if (stored.empty() || stored.rfind("/@mutable/", 0) == 0) continue;
        const std::string sample = songcore::resolve_media_path(stored, roots);
        const auto known = std::find_if(credits.begin(), credits.end(),
                                        [&](const SourceCredit& c) { return c.sample == sample; });
        if (known != credits.end()) {
            known->instruments.push_back(static_cast<int>(slot));
            continue;
        }
        std::string raw;
        if (!fs.file_exists(sample + ".source.json") || !fs.read_file(sample + ".source.json", raw) ||
            raw.size() > 64 * 1024)
            continue;
        const auto j = songcore::json::parse(raw, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        SourceCredit c;
        c.sample = sample;
        c.title = text(j, "title");
        c.creator = text(j, "creator");
        c.sourceUrl = text(j, "source_url");
        c.licenseUrl = text(j, "license_url");
        c.live = j.contains("live") && j["live"].is_boolean() && j["live"].get<bool>();
        c.license = source_license_kind(c.licenseUrl, c.live);
        c.inMs = number(j, "in_ms");
        c.outMs = number(j, "out_ms");
        c.instruments.push_back(static_cast<int>(slot));
        credits.push_back(std::move(c));
    }
    return credits;
}

std::string source_credits_text(const songcore::Project& project, const std::vector<SourceCredit>& credits) {
    std::string out = "Credits for \"" + (project.name.empty() ? std::string("Untitled") : project.name) + "\"\n";
    out += "Found sounds used in this song, written by TrackWerk.\n";
    out += "Check each licence page before a release; this list is a reading aid, not legal advice.\n\n";
    const auto slot = [&](int i) {
        char id[8];
        std::snprintf(id, sizeof id, "%02X", i);
        const auto& name = project.instruments[static_cast<size_t>(i)].name;
        return std::string(id) + (name.empty() ? "" : " " + name);
    };
    int number = 0;
    for (const auto& c : credits) {
        out += std::to_string(++number) + ". " + (c.title.empty() ? "Untitled" : c.title);
        out += " — " + (c.creator.empty() ? std::string("creator not supplied") : c.creator) + "\n";
        out += "   Licence: " + std::string(source_license_name(c.license)) + "\n";
        if (!c.licenseUrl.empty()) out += "   Licence link: " + c.licenseUrl + "\n";
        if (!c.sourceUrl.empty()) out += "   Source: " + c.sourceUrl + "\n";
        if (!c.live && c.inMs >= 0 && c.outMs > c.inMs) out += "   Excerpt: " + clock(c.inMs) + " – " + clock(c.outMs) + "\n";
        out += "   Instrument:";
        for (size_t i = 0; i < c.instruments.size(); ++i) out += (i ? ", " : " ") + slot(c.instruments[i]);
        out += "\n\n";
    }
    std::string check;
    for (size_t i = 0; i < credits.size(); ++i)
        if (!source_license_release_safe(credits[i].license))
            check += "   " + std::to_string(i + 1) + ". " + (credits[i].title.empty() ? "Untitled" : credits[i].title) + " — " +
                     source_license_badge(credits[i].license) + "\n";
    out += check.empty() ? "All listed sounds allow a release with the credits above.\n"
                         : "Not cleared for a commercial release:\n" + check;
    return out;
}

int source_credits_write(const songcore::Project& project, const songcore::MediaRoots& roots, FileSystem& fs,
                         const std::string& path) {
    const auto credits = source_credits_collect(project, roots, fs);
    if (credits.empty()) return 0;
    return fs.write_file(path, source_credits_text(project, credits)) ? static_cast<int>(credits.size()) : -1;
}

}  // namespace pt::ui
