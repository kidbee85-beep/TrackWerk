// ptsource-credits-check: licence kinds of found sounds, the Sources screen's licence filter, and the
// credits file EXPORT (mix, stems) and COLLECT write beside the song (source_credits.h).
// usage: ptsource-credits-check <new run directory>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop
#include "ui/project_actions.h"
#include "ui/source_credits.h"
#include "ui/source_sampler.h"

namespace {
namespace fs = std::filesystem;
using pt::ui::SourceLicense;
using pt::ui::SourceLicenseFilter;

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void kinds() {
    const struct { const char* license; bool live; SourceLicense kind; } cases[] = {
        {"https://creativecommons.org/publicdomain/zero/1.0/", false, SourceLicense::CC0},
        {"http://creativecommons.org/publicdomain/mark/1.0/", false, SourceLicense::PUBLIC_DOMAIN},
        {"https://creativecommons.org/licenses/by/4.0/", false, SourceLicense::CC_BY},
        {"http://creativecommons.org/licenses/by-sa/3.0/", false, SourceLicense::CC_BY_SA},
        {"https://creativecommons.org/licenses/by-nc-sa/4.0/", false, SourceLicense::NONCOMMERCIAL},
        {"https://creativecommons.org/licenses/by-nc-nd/3.0/us/", false, SourceLicense::NO_DERIVATIVES},
        {"https://creativecommons.org/licenses/by-nd/2.5", false, SourceLicense::NO_DERIVATIVES},
        {"HTTPS://CreativeCommons.org/Licenses/BY/2.0/", false, SourceLicense::CC_BY},
        {"https://creativecommons.org/licenses/sampling+/1.0/", false, SourceLicense::UNKNOWN},
        {"Creative Commons Attribution license (reuse allowed)", false, SourceLicense::CC_BY},
        {"https://www.gnu.org/licenses/gpl-3.0.html", false, SourceLicense::CC_BY_SA},
        {"http://artlibre.org/licence/lal/en/", false, SourceLicense::CC_BY_SA},
        {"Public domain", false, SourceLicense::PUBLIC_DOMAIN},
        {"", false, SourceLicense::UNKNOWN},
        {"  ", false, SourceLicense::UNKNOWN},
        {"https://example.org/terms", false, SourceLicense::UNKNOWN},
        {"https://creativecommons.org/licenses/by/4.0/", true, SourceLicense::BROADCAST},
    };
    for (const auto& c : cases) {
        const auto kind = pt::ui::source_license_kind(c.license, c.live);
        if (kind != c.kind) std::cerr << "kind of '" << c.license << "' is " << pt::ui::source_license_badge(kind) << "\n";
        assert(kind == c.kind);
    }
    using F = SourceLicenseFilter;
    for (auto k : {SourceLicense::PUBLIC_DOMAIN, SourceLicense::CC0})
        assert(pt::ui::source_license_allowed(k, F::NO_CREDIT) && pt::ui::source_license_allowed(k, F::RELEASE_SAFE));
    for (auto k : {SourceLicense::CC_BY, SourceLicense::CC_BY_SA})
        assert(!pt::ui::source_license_allowed(k, F::NO_CREDIT) && pt::ui::source_license_allowed(k, F::RELEASE_SAFE));
    for (auto k : {SourceLicense::NONCOMMERCIAL, SourceLicense::NO_DERIVATIVES, SourceLicense::BROADCAST, SourceLicense::UNKNOWN}) {
        assert(!pt::ui::source_license_allowed(k, F::RELEASE_SAFE) && pt::ui::source_license_allowed(k, F::ALL));
        assert(!pt::ui::source_license_release_safe(k));
    }
    std::cout << "17 licence links and texts sorted; filters: all / release-safe / no credit needed\n";
}

// A backend that answers with a fixed result list and records the commands it gets.
struct FixedBackend : pt::ui::SourceBackend {
    std::string snapshot;
    std::vector<std::string> commands;
    bool command(const std::string& action, const std::string& args) override {
        commands.push_back(action + " " + args);
        return true;
    }
    std::string state_json() override { return snapshot; }
};

pt::ui::ButtonEvent press(pt::ui::Button b, pt::ui::ButtonAction a = pt::ui::ButtonAction::PRESSED) {
    pt::ui::ButtonEvent e;
    e.button = b;
    e.action = a;
    return e;
}

void filter_screen(const fs::path& root) {
    Sample sample(root / "screen", true, 48000, 256, 1);
    FixedBackend backend;
    const songcore::json results = songcore::json::array({
        {{"id", "a"}, {"title", "Noncommercial"}, {"license", "https://creativecommons.org/licenses/by-nc/4.0/"}},
        {{"id", "b"}, {"title", "Attribution"}, {"license", "https://creativecommons.org/licenses/by/4.0/"}},
        {{"id", "c"}, {"title", "Unknown"}, {"license", ""}},
        {{"id", "d"}, {"title", "Zero"}, {"license", "https://creativecommons.org/publicdomain/zero/1.0/"}},
        {{"id", "e"}, {"title", "No derivatives"}, {"license", "https://creativecommons.org/licenses/by-nd/4.0/"}},
    });
    backend.snapshot = songcore::json{{"status", "idle"}, {"results", results}, {"selected_index", 0}}.dump();
    auto& s = sample.state.sourceSampler;
    s.backend = &backend;
    assert(pt::ui::source_sampler_poll(s, 1000));
    assert(s.results.size() == 5 && s.selectedResult == 0);

    // A on the licence row: All → Release-safe; the cursor moves to the first shown result.
    s.row = pt::ui::SOURCE_LICENSE_ROW;
    pt::ui::source_sampler_button(press(pt::ui::Button::A), s, sample.host, *sample.engine);
    pt::ui::source_sampler_button(press(pt::ui::Button::A, pt::ui::ButtonAction::RELEASED), s, sample.host, *sample.engine);
    assert(s.licenseFilter == static_cast<int>(SourceLicenseFilter::RELEASE_SAFE));
    assert(s.selectedResult == 1);
    assert(s.status.find("3 results hidden") != std::string::npos);

    // In the list, Down skips the hidden ones: Attribution → Zero → stays on Zero.
    s.row = 3;
    s.listFocused = true;
    pt::ui::source_sampler_button(press(pt::ui::Button::DPAD_DOWN), s, sample.host, *sample.engine);
    assert(s.selectedResult == 3);
    pt::ui::source_sampler_button(press(pt::ui::Button::DPAD_DOWN), s, sample.host, *sample.engine);
    assert(s.selectedResult == 3);
    pt::ui::source_sampler_button(press(pt::ui::Button::DPAD_UP), s, sample.host, *sample.engine);
    assert(s.selectedResult == 1);

    // No credit needed: only Zero; a new snapshot keeps the cursor on a shown result.
    s.row = pt::ui::SOURCE_LICENSE_ROW;
    s.listFocused = false;
    pt::ui::source_sampler_button(press(pt::ui::Button::DPAD_RIGHT), s, sample.host, *sample.engine);
    assert(s.licenseFilter == static_cast<int>(SourceLicenseFilter::NO_CREDIT) && s.selectedResult == 3);
    backend.snapshot = songcore::json{{"status", "idle"}, {"results", results}, {"selected_index", 4}, {"busy", false}}.dump();
    s.hasPolled = false;
    assert(pt::ui::source_sampler_poll(s, 2000));
    assert(s.selectedResult == 3);

    // A in the list opens the backend index of the shown result.
    s.row = 3;
    s.listFocused = true;
    backend.commands.clear();
    pt::ui::source_sampler_button(press(pt::ui::Button::A), s, sample.host, *sample.engine);
    pt::ui::source_sampler_button(press(pt::ui::Button::A, pt::ui::ButtonAction::RELEASED), s, sample.host, *sample.engine);
    assert(!backend.commands.empty() && backend.commands.back().find("open") == 0 &&
           backend.commands.back().find("\"index\":3") != std::string::npos);

    // A list with nothing that passes: no cursor, and A says why.
    backend.snapshot = songcore::json{{"status", "idle"}, {"results", songcore::json::array({results[0], results[4]})}}.dump();
    s.hasPolled = false;
    assert(pt::ui::source_sampler_poll(s, 3000));
    s.busy = false;
    assert(s.selectedResult == -1);
    pt::ui::source_sampler_button(press(pt::ui::Button::A), s, sample.host, *sample.engine);
    pt::ui::source_sampler_button(press(pt::ui::Button::A, pt::ui::ButtonAction::RELEASED), s, sample.host, *sample.engine);
    assert(s.status == "No result passes the license filter.");
    s.backend = nullptr;
    std::cout << "Licence filter: hidden results are skipped, the cursor and A use the backend index\n";
}

void write_sidecar(const fs::path& wav, const songcore::json& j) { std::ofstream(wav.string() + ".source.json") << j.dump(2); }

void credits(const fs::path& root) {
    Sample sample(root / "credits", true, 48000, 256, 1);
    auto& project = sample.host.edit_project();
    project.name = "Crate";
    // Instrument 1 (the fixture's) and 2 play the same found sound; 3 a radio take; 4 has no sidecar.
    write_sidecar(sample.wav, {{"title", "Blue Hour"}, {"creator", "Ann Example"},
                               {"source_url", "https://archive.org/details/bluehour"},
                               {"license_url", "https://creativecommons.org/licenses/by/4.0/"},
                               {"in_ms", 12000}, {"out_ms", 20500}});
    const auto radio = root / "credits" / "radio.wav";
    fs::copy_file(sample.wav, radio);
    write_sidecar(radio, {{"title", "Night Station"}, {"creator", "Some FM"}, {"source_url", "https://somafm.com/"},
                          {"license_url", ""}, {"live", true}});
    const auto plain = root / "credits" / "plain.wav";
    fs::copy_file(sample.wav, plain);
    project.instruments[2].sampleFilePath = sample.wav.string();
    project.instruments[2].name = "BLUE";
    project.instruments[3].sampleFilePath = radio.string();
    project.instruments[4].sampleFilePath = plain.string();

    const auto list = pt::ui::source_credits_collect(project, sample.host.media_roots(), sample.fs);
    assert(list.size() == 2);
    assert(list[0].instruments == std::vector<int>({1, 2}) && list[0].license == SourceLicense::CC_BY);
    assert(list[1].instruments == std::vector<int>({3}) && list[1].license == SourceLicense::BROADCAST);
    const auto text = pt::ui::source_credits_text(project, list);
    for (const char* part : {"Credits for \"Crate\"", "1. Blue Hour — Ann Example", "CC BY (credit the creator)",
                             "https://creativecommons.org/licenses/by/4.0/", "https://archive.org/details/bluehour",
                             "Excerpt: 0:12.000 – 0:20.500", "Instrument: 01 INST01, 02 BLUE", "2. Night Station — Some FM",
                             "Not cleared for a commercial release:", "2. Night Station — LIVE"})
        if (text.find(part) == std::string::npos) {
            std::cerr << text << "\nmissing: " << part << "\n";
            assert(false);
        }

    // EXPORT → MIX writes Crate_0001-CREDITS.txt beside the WAV.
    project.tracks[0].chainRefs = {0};
    project.chains[0].phraseRefs[0] = 0;
    project.phrases[0].steps[0].note = songcore::Note::C4();
    project.phrases[0].steps[0].instrument = instrument;
    const auto mix = pt::ui::render_mix(sample.host, sample.fs, sample.state, {}, {});
    assert(mix.ok && mix.message == "EXPORTED + CREDITS");
    const fs::path renders = sample.fs.renders_directory();
    assert(fs::exists(renders / "Crate_0001.wav") && read_text(renders / "Crate_0001-CREDITS.txt") == text);

    // EXPORT → STEMS writes one beside the stems.
    const auto stems = pt::ui::render_stems(sample.host, sample.fs, sample.state, {}, {});
    assert(stems.ok && stems.message == "STEMS + CREDITS");
    assert(read_text(renders / "Crate" / "Crate-CREDITS.txt") == text);

    // COLLECT copies each sidecar with its sample and writes CREDITS.txt into the song folder.
    const auto collected = pt::ui::collect_project(sample.host, sample.fs, "Crate", {});
    assert(collected.ok);
    const fs::path songDir = fs::path(sample.fs.songs_directory()) / "Crate";
    assert(read_text(songDir / "CREDITS.txt") == text);
    int sidecars = 0;
    for (const auto& entry : fs::recursive_directory_iterator(songDir))
        sidecars += entry.path().filename().string().size() > 12 &&
                    entry.path().string().rfind(".source.json") == entry.path().string().size() - 12;
    assert(sidecars == 2);

    // A song without found sounds keeps the old messages and writes no file.
    for (int i = 1; i <= 4; ++i) project.instruments[i].sampleFilePath = plain.string();
    const auto bare = pt::ui::render_mix(sample.host, sample.fs, sample.state, {}, {});
    assert(bare.ok && bare.message == "EXPORTED!" && !fs::exists(renders / "Crate_0002-CREDITS.txt"));
    std::cout << "Credits: 2 found sounds (one shared by 2 instruments) in MIX, STEMS and COLLECT; none without\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ptsource-credits-check <new run directory>\n";
        return 2;
    }
    const fs::path root = argv[1];
    if (fs::exists(root)) {
        std::cerr << "run directory must be new\n";
        return 2;
    }
    kinds();
    filter_screen(root);
    credits(root);
    std::cout << "ptsource-credits-check: all checks passed\n";
    return 0;
}
