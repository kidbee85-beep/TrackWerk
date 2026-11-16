// ptsources-ux-check: Sources stage 2, part A (TrackWerk 1.2) — orientation and safe use, against
// Codex' STUFE2-SOURCES-PRUEFBARE-GRENZEN:
//  - four phases FIND / LISTEN / MARK / USE label the sections and the header strip; no row is locked
//    behind a phase, nothing asks to confirm a phase
//  - Provider and Genre open a list: A opens, Up / Down choose, A takes it, B (or Left) keeps the old
//    value; unavailable providers cannot be chosen; YouTube keeps its A A opt-in on the row
//  - the take's slot is shown with whether it is in use; an occupied slot is never replaced by an
//    automatic import and only by a second A on Open sample editor; an open unsaved sample edit stays
//  - SELECT help never stops a playing radio or file preview; Random / Discover stay reachable
// A fake backend records the commands; no network.
// usage: ptsources-ux-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/canvas.h"
#include "ui/source_sampler.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include "ui/studio_help.h"
#include "ui/theme.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace pt::ui;
using songcore::json;
namespace fsys = std::filesystem;

namespace {

struct FakeBackend : SourceBackend {
    json state;
    std::vector<std::pair<std::string, json>> commands;
    bool command(const std::string& action, const std::string& args) override {
        commands.emplace_back(action, json::parse(args));
        return true;
    }
    std::string state_json() override { return state.dump(); }
    bool sent(const std::string& action) const {
        for (const auto& c : commands) if (c.first == action) return true;
        return false;
    }
};

json snapshot(bool playing, bool live) {
    return {{"status", "ready"}, {"provider", live ? "radio_browser" : "archive"}, {"genre", "jazz"}, {"query", "Jazz test"},
            {"providers", json::array({{{"id", "archive"}, {"available", true}}, {{"id", "commons"}, {"available", true}},
                                       {{"id", "youtube"}, {"available", false}}, {{"id", "radio_browser"}, {"available", true}, {"live", true}},
                                       {{"id", "somafm"}, {"available", true}, {"live", true}}})},
            {"capabilities", {{"live_mp3", true}, {"live_history", true}, {"favorites", true}, {"preview_gain", true}, {"take_request_id", true}}},
            {"results", json::array({{{"id", "album-a"}, {"title", "Archive test album A"}, {"creator", "Test creator"}, {"kind", "album"},
                                      {"url", "https://archive.org/details/test-a"}, {"license", "https://creativecommons.org/licenses/by/4.0/"}}})},
            {"tracks", json::array({{{"id", "track-a"}, {"title", "Test track one"}, {"duration_ms", 120000}}})},
            {"selected_index", 0}, {"track_index", 0}, {"title", "Test track one"}, {"creator", "Test creator"},
            {"source_url", "https://archive.org/details/test-a"}, {"license_url", "https://creativecommons.org/licenses/by/4.0/"},
            {"playing", playing}, {"live", live}, {"recording", false}, {"rendering", false}, {"busy", false},
            {"position_ms", 2500}, {"duration_ms", live ? 0 : 120000}, {"in_ms", 1000}, {"out_ms", 4000},
            {"clip_ready", false}, {"clip_path", ""}, {"clip_frames", 0}, {"clip_rate", 0}, {"clip_channels", 0}};
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    FakeBackend backend;
    AppState s;
    uint64_t now = 1000;
    Rig() {
        assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
        s.project = &host.edit_project();
        s.theme = theme_studio();
        s.studioOpen = true;
        s.studioTab = 7;
        s.sourceSampler.backend = &backend;
        backend.state = snapshot(false, false);
        poll();
    }
    void poll() { now += 200; source_sampler_poll(s.sourceSampler, now); }
    void send(Button b, ButtonAction a, ButtonMods m = {}) {
        s.sourceSampler.currentSlot = s.currentInstrument;
        source_sampler_button({b, a, m}, s.sourceSampler, host, *engine);
    }
    void tap(Button b) { send(b, ButtonAction::PRESSED); send(b, ButtonAction::RELEASED); }
    void a() { tap(Button::A); }
    void a_with(Button b) {
        send(Button::A, ButtonAction::PRESSED);
        send(b, ButtonAction::PRESSED, {true});
        send(Button::A, ButtonAction::RELEASED);
    }
    void row(int r) { s.sourceSampler.row = r; }
    std::string bytes() const { return songcore::serialize_project(host.project()); }
};

void shot(const fsys::path& path, const AppState& app) {
    Canvas canvas;
    draw_studio(canvas, app);
    std::ofstream out(path, std::ios::binary);
    out << "P6\n640 480\n255\n";
    for (int i = 0; i < 640 * 480; ++i) {
        const auto p = canvas.pixels()[i];
        const char rgb[] = {char(p >> 16), char(p >> 8), char(p)};
        out.write(rgb, 3);
    }
}

void wave(const fsys::path& path) {
    std::ofstream f(path, std::ios::binary);
    auto u16 = [&](unsigned v) { char b[] = {char(v), char(v >> 8)}; f.write(b, 2); };
    auto u32 = [&](unsigned v) { u16(v); u16(v >> 16); };
    f.write("RIFF", 4); u32(36 + 1024); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(48000); u32(96000); u16(2); u16(16);
    f.write("data", 4); u32(1024);
    for (int i = 0; i < 512; ++i) u16(unsigned(int(std::sin(i * .08) * 10000)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || fsys::exists(argv[1])) {
        std::cerr << "usage: ptsources-ux-check <new run directory>\n";
        return 2;
    }
    const fsys::path dir = fsys::absolute(argv[1]);
    fsys::create_directories(dir / "images");
    Rig r;
    auto& v = r.s.sourceSampler;

    // ── Four phases: every row in exactly one, in screen order FIND, LISTEN, MARK, USE ──
    assert(source_sampler_row_phase(0) == 0 && source_sampler_row_phase(2) == 0 && source_sampler_row_phase(3) == 0);
    assert(source_sampler_row_phase(5) == 1 && source_sampler_row_phase(6) == 1 && source_sampler_row_phase(14) == 1);
    assert(source_sampler_row_phase(7) == 2 && source_sampler_row_phase(8) == 2 && source_sampler_row_phase(SOURCE_BARS_ROW) == 2);
    assert(source_sampler_row_phase(10) == 3 && source_sampler_row_phase(11) == 3 && source_sampler_row_phase(12) == 3);
    // No phase gate: Down walks every visible row from the top to USE and wraps, with nothing loaded.
    {
        r.row(SOURCE_COLLECTION_ROW);
        int phase = 0, steps = 0;
        bool sawUse = false;
        while (steps++ < 40) {
            r.tap(Button::DPAD_DOWN);
            const int p = source_sampler_row_phase(v.row);
            if (p < phase) break;   // wrapped to the top
            phase = p;
            sawUse = sawUse || p == 3;
        }
        assert(sawUse && steps < 40);
    }

    // ── Provider / Genre as lists ──
    r.row(0);
    v.provider = 0;
    r.a();
    assert(v.choiceList == 1 && v.choiceCursor == 0);
    r.s.studioOpen = true;
    shot(dir / "images/1-provider-list.ppm", r.s);
    r.tap(Button::DPAD_DOWN);
    r.tap(Button::DPAD_DOWN);                       // YouTube: unavailable on this backend
    r.a();
    assert(v.choiceList == 1 && v.provider == 0 && v.status.find("unavailable") != std::string::npos);
    r.tap(Button::DPAD_DOWN);                       // Radio Browser
    r.tap(Button::B);                               // keep
    assert(v.choiceList == 0 && v.provider == 0);
    r.a();
    r.tap(Button::DPAD_DOWN);
    r.tap(Button::DPAD_DOWN);
    r.tap(Button::DPAD_DOWN);
    r.a();                                          // take Radio Browser
    assert(v.choiceList == 0 && v.provider == 3);
    r.a();
    r.tap(Button::DPAD_LEFT);                       // Left keeps too
    assert(v.choiceList == 0 && v.provider == 3);
    // Left / Right on the row still change it at once, as before.
    r.tap(Button::DPAD_LEFT);
    assert(v.provider == 1);
    // Genre: 17 entries, bounded cursor, the old value kept on B.
    r.row(1);
    v.genre = 0;
    r.a();
    assert(v.choiceList == 2);
    for (int i = 0; i < 30; ++i) r.tap(Button::DPAD_DOWN);
    assert(v.choiceCursor == 16);
    shot(dir / "images/2-genre-list.ppm", r.s);
    r.tap(Button::B);
    assert(v.genre == 0);
    r.a();
    r.tap(Button::DPAD_DOWN);
    r.tap(Button::DPAD_DOWN);
    r.tap(Button::DPAD_DOWN);
    r.a();
    assert(v.genre == 3 && v.choiceList == 0);
    // Nothing in the list sent a command or touched the project.
    assert(!r.backend.sent("search") && !r.backend.sent("stop"));

    // ── Random / Discover stay on Search ──
    r.row(2);
    v.provider = 0;
    v.col = 0;
    r.tap(Button::DPAD_RIGHT);
    assert(v.col == 1);
    r.a();
    assert(r.backend.sent("discover"));

    // ── SELECT help never stops playback: a live station plays on through help ──
    r.backend.state = snapshot(true, true);
    r.poll();
    assert(v.playing && v.live);
    r.backend.commands.clear();
    r.tap(Button::SELECT);
    assert(v.showHelp);
    r.poll();
    r.tap(Button::SELECT);
    assert(!v.showHelp && v.playing && !r.backend.sent("stop") && !r.backend.sent("pause"));
    // …and from inside the provider list too.
    r.row(1);
    r.a();
    r.tap(Button::SELECT);
    assert(v.showHelp && v.choiceList == 2);
    r.tap(Button::B);
    assert(!v.showHelp && v.choiceList == 2 && !r.backend.sent("stop"));
    r.tap(Button::B);
    assert(v.choiceList == 0 && !r.backend.sent("stop"));
    r.backend.state = snapshot(false, false);
    r.poll();

    // ── The phase strip and the Target slot line ──
    r.row(7);
    shot(dir / "images/3-mark-phase.ppm", r.s);
    r.row(11);
    v.targetInstrument = -1;
    shot(dir / "images/4-target-current-empty.ppm", r.s);

    // ── An occupied slot is never replaced in passing ──
    {
        wave(dir / "take.wav");
        wave(dir / "old.wav");
        assert(r.host.load_sample(3, (dir / "old.wav").string()));
        r.host.set_instrument_type(3, songcore::InstrumentType::SAMPLER);
        const std::string before = r.bytes();
        StdFileSystem fs((dir / "home").string());
        // The take is ready in the backend.
        auto ready = snapshot(false, false);
        ready["clip_ready"] = true;
        ready["clip_path"] = (dir / "take.wav").string();
        ready["clip_frames"] = 512;
        ready["clip_rate"] = 48000;
        ready["clip_channels"] = 1;
        r.backend.state = ready;
        r.poll();
        assert(v.clipReady);
        v.targetInstrument = 3;
        shot(dir / "images/5-target-in-use.ppm", r.s);
        // Automatic import (after Render): refused, take kept, slot untouched.
        v.importPath = v.clipPath;
        v.importRequested = true;
        v.importIsAutomatic = true;
        v.importContextBound = true;
        v.importProject = r.s.project;
        v.importProjectVersion = r.s.projectVersion;
        v.importInstrument = 3;
        assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs));
        assert(r.bytes() == before && v.status.find("in use") != std::string::npos && v.clipReady);
        // Open sample editor: the first A asks, another button disarms, A A replaces.
        r.row(12);
        r.a();
        assert(v.importRequested);
        assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs));
        assert(r.bytes() == before && v.replaceArmed && v.status.find("A again replaces") != std::string::npos);
        shot(dir / "images/6-replace-asks.ppm", r.s);
        r.tap(Button::DPAD_DOWN);                   // anything else: keep
        assert(!v.replaceArmed);
        r.row(12);
        r.a();
        assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs) && v.replaceArmed && r.bytes() == before);   // asks again
        r.a();
        assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs));
        assert(r.bytes() != before);                // replaced on purpose
        // Codex, STUFE2A-BEFUND-HILFE-BESTAETIGUNG: the question never outlives help, another take,
        // another slot, a project change, Studio closing or a focus loss. Each time the next single A
        // asks again and leaves the project as it was; only the second deliberate A replaces.
        {
            wave(dir / "take2.wav");
            assert(r.host.load_sample(4, (dir / "old.wav").string()));
            r.host.set_instrument_type(4, songcore::InstrumentType::SAMPLER);
            r.s.studioOpen = true;
            r.s.studioTab = 7;
            r.backend.state = ready;
            r.poll();
            v.targetInstrument = 4;
            auto through_studio = [&](Button b, ButtonAction a) { studio_button({b, a, {}}, r.s, r.host, *r.engine, fs); };
            auto ask = [&] {
                r.row(12);
                r.a();
                const std::string kept = r.bytes();
                const bool handled = source_sampler_apply_import(r.s, r.host, *r.engine, fs);
                assert(handled && v.replaceArmed && r.bytes() == kept);
            };
            auto still_asks = [&](const char* after) {
                const std::string kept = r.bytes();
                r.row(12);
                r.a();
                assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs));
                if (r.bytes() != kept || !v.replaceArmed) {
                    std::cerr << "one A replaced the slot after " << after << "\n";
                    assert(false);
                }
                r.tap(Button::DPAD_UP);   // leave the new question unanswered: any other button drops it
                r.row(12);
                assert(!v.replaceArmed);
            };
            r.backend.commands.clear();
            ask();
            through_studio(Button::SELECT, ButtonAction::PRESSED);    // help, through the real Studio path
            through_studio(Button::SELECT, ButtonAction::RELEASED);
            assert(r.s.studioHelp && !v.replaceArmed);
            through_studio(Button::SELECT, ButtonAction::PRESSED);
            through_studio(Button::SELECT, ButtonAction::RELEASED);
            r.s.studioHelp = false;
            assert(!r.backend.sent("stop") && !r.backend.sent("pause"));
            still_asks("help");
            v.targetInstrument = 3;                                   // another slot
            still_asks("a slot change");
            v.targetInstrument = 4;
            ask();
            v.clipPath = (dir / "take2.wav").string();                // another take
            still_asks("a new take");
            ask();
            ++r.s.projectVersion;                                     // the project changed meanwhile
            still_asks("a project change");
            ask();
            source_sampler_disarm(v);                                 // focus / controller loss (shell)
            still_asks("a focus loss");
            ask();
            r.s.studioOpen = false;                                   // Studio closed and reopened
            through_studio(Button::DPAD_DOWN, ButtonAction::PRESSED);
            r.s.studioOpen = true;
            still_asks("Studio closing");
            ask();
            r.s.studioTab = 3;                                        // another Studio section
            through_studio(Button::DPAD_DOWN, ButtonAction::PRESSED);
            r.s.studioTab = 7;
            still_asks("another section");
            // The deliberate second A still works.
            ask();
            const std::string kept = r.bytes();
            r.row(12);
            r.a();
            assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs) && r.bytes() != kept);
        }

        // An empty slot imports at once, no question.
        r.s.studioOpen = true;
        r.s.studioTab = 7;
        r.backend.state = ready;
        r.poll();
        v.targetInstrument = 9;
        const std::string emptyBefore = r.bytes();
        r.row(12);
        r.a();
        assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs));
        assert(r.bytes() != emptyBefore && !v.replaceArmed);
        // An unsaved sample edit stays: the import refuses (existing guard).
        r.s.studioOpen = true;
        r.s.studioTab = 7;
        r.backend.state = ready;
        r.poll();
        v.targetInstrument = 10;
        r.s.currentScreen = ScreenType::SAMPLE_EDITOR;
        r.s.sampleEditor.isModified = true;
        const std::string editing = r.bytes();
        r.row(12);
        r.a();
        assert(source_sampler_apply_import(r.s, r.host, *r.engine, fs) && r.bytes() == editing);
        r.s.sampleEditor.isModified = false;
        r.s.currentScreen = ScreenType::SONG;
    }

    // ── Part B in the UI: a downloading MP3 shows what is loaded; a backend without the capability
    // (Android MediaPlayer, older builds) is never shown as progressive, whatever fields it sends ──
    {
        r.s.studioOpen = true;
        r.s.studioTab = 7;
        r.s.studioHelp = false;
        auto loading = snapshot(true, false);
        loading["duration_ms"] = 12000;
        loading["position_ms"] = 4000;
        loading["downloading"] = true;
        loading["loaded_ms"] = 12000;
        loading["total_ms"] = 40000;
        auto legacy = loading;
        r.backend.state = legacy;   // no progressive_mp3 capability
        r.poll();
        assert(!v.downloading && v.totalMs == 0);
        loading["capabilities"]["progressive_mp3"] = true;
        r.backend.state = loading;
        r.poll();
        assert(v.downloading && v.totalMs == 40000 && v.durationMs == 12000);
        r.row(5);
        shot(dir / "images/7-mp3-loading.ppm", r.s);
        // The help follows: the still-downloading page, then the provider list page, then Workflow.
        r.s.studioHelp = true;
        r.s.studioHelpTopic = 7;
        r.s.studioHelpPage = STUDIO_HELP_QUICK;
        assert(studio_help_quick_content(r.s).title == "Sources / Still downloading");
        shot(dir / "images/8-help-downloading.ppm", r.s);
        r.backend.state = snapshot(false, false);
        r.poll();
        assert(!v.downloading);
        assert(studio_help_quick_content(r.s).lines[0].find("FIND > LISTEN > MARK > USE") == 0);
        shot(dir / "images/9-help-sources.ppm", r.s);
        v.choiceList = 1;
        assert(studio_help_quick_content(r.s).title == "Sources / Provider");
        v.choiceList = 0;
        r.s.studioHelpPage = 0;   // Workflow
        shot(dir / "images/10-help-workflow.ppm", r.s);
        r.s.studioHelp = false;
    }

    std::cout << "ptsources-ux-check: four phases FIND / LISTEN / MARK / USE label every row without a gate; Provider and "
                 "Genre open lists (A open, Up / Down, A take, B or Left keep; unavailable refused; Left / Right quick change "
                 "kept); Random stays; SELECT help leaves a playing station alone; Target slot shows where a take lands and "
                 "if it is in use; an occupied slot is refused by the automatic import and replaced only by A A; an empty "
                 "slot imports at once; an unsaved sample edit is kept; the question never outlives help, another slot, "
                 "another take, a project change, a focus loss, Studio closing or another section; LOADED only with the "
                 "progressive capability; help pages for lists, phases, slots and downloads; 10 native images\n";
    return 0;
}
