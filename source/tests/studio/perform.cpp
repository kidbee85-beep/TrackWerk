// PERFORM (ui/perform.h): the layout in the song, the tiles' actions through the real buttons, the
// master filter in the engine, the CPU per track, and the way there on the screen grid.
// Run directory: argv[1].
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/synth.h"
#include "ui/button_mapper.h"
#include "ui/help_text.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/navigation.h"
#include "ui/perform.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace pt::ui;
using songcore::PerformSlot;

namespace {
struct Input {
    AppState& state;
    songcore::SongcoreHost& host;
    AudioEngine& engine;
    FileSystem& fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now = 1000;
    Input(AppState& s, songcore::SongcoreHost& h, AudioEngine& e, FileSystem& f) : state(s), host(h), engine(e), fs(f), dispatch(s, h, f) {}
    void event(Button button, ButtonAction action, ButtonMods mods = {}) {
        now += 20;
        dispatch.set_now(static_cast<long long>(now));
        ButtonEvent e{button, action, mods};
        if (!studio_button(e, state, host, engine, fs)) handle_button(e, dispatch, mapper, now);
    }
    // A pause first, so a tap right after another A is a new press and not a double tap (A,A).
    void tap(Button b) { now += 600; ButtonMods m; if (b == Button::A) m.a = true; event(b, ButtonAction::PRESSED, m); event(b, ButtonAction::RELEASED); }
    // Hold `held`, press `b` once, let go of both.
    void combo(Button held, Button b) {
        now += 600;
        ButtonMods m;
        if (held == Button::A) m.a = true;
        if (held == Button::B) m.b = true;
        if (held == Button::L_SHIFT) m.l = true;
        if (held == Button::R_SHIFT) m.r = true;
        event(held, ButtonAction::PRESSED, m);
        event(b, ButtonAction::PRESSED, m);
        event(b, ButtonAction::RELEASED, m);
        event(held, ButtonAction::RELEASED, {});
    }
};
void snapshot(const std::filesystem::path& path, AppState& state) {
    Canvas c;
    TrackerLayout layout;
    layout.draw(c, state);
    std::ofstream f(path, std::ios::binary);
    f << "P6\n640 480\n255\n";
    for (int i = 0; i < 640 * 480; ++i) {
        const auto p = c.pixels()[i];
        const char rgb[] = {char(p >> 16), char(p >> 8), char(p)};
        f.write(rgb, 3);
    }
}
songcore::Project song(int heavyModel = -1) {
    auto p = songcore::make_default_project();
    for (int t = 0; t < 3; ++t) {
        p.tracks[size_t(t)].chainRefs = {t};
        p.chains[size_t(t)].phraseRefs[0] = t;
        for (int step = 0; step < 16; step += 4) {
            p.phrases[size_t(t)].steps[size_t(step)].note = songcore::Note::C4();
            p.phrases[size_t(t)].steps[size_t(step)].instrument = t;
        }
    }
    // 0: SP MORPH (a synth with Morph/Motion/Warp), 1: a light Studio DSP saw, 2: heavy or light.
    p.instruments[0].sampleFilePath = studio::custom_uri(studio::kSpectralMorphModel, 100, 0, 128);
    p.instruments[1].sampleFilePath = studio::custom_uri(102, 128, 128, 128);
    p.instruments[2].sampleFilePath = studio::custom_uri(heavyModel >= 0 ? heavyModel : 102, 128, 128, 128);
    for (int i = 0; i < 3; ++i) { p.instruments[size_t(i)].loopMode = "fwd"; p.instruments[size_t(i)].volume = 150; }
    return p;
}
std::vector<float> render(AudioEngine& engine, int blocks) {
    std::vector<float> out;
    float data[512];
    for (int i = 0; i < blocks; ++i) {
        engine.processLiveBlock(data, 256, 2, 48000);
        out.insert(out.end(), data, data + 512);
    }
    return out;
}
// Energy in a crude band: high = sample-to-sample differences, low = a running mean.
double high_energy(const std::vector<float>& v) { double e = 0; for (size_t i = 2; i < v.size(); i += 2) { const double d = v[i] - v[i - 2]; e += d * d; } return e; }
double low_energy(const std::vector<float>& v) {
    double e = 0, m = 0;
    for (size_t i = 0; i < v.size(); i += 2) { m += .002 * (v[i] - m); e += m * m; }
    return e;
}
}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path out = argv[1];
    std::filesystem::remove_all(out);
    std::filesystem::create_directories(out);

    // ── The layout travels with the song; defaults are not written ──────────────────────────────
    {
        auto p = songcore::make_default_project();
        assert(p.perform == songcore::default_perform_slots());
        assert(songcore::serialize_project(p).find("\"perform\"") == std::string::npos);
        p.perform[0] = {PerformSlot::Type::MUTE, 0x81, -1, 0};
        p.perform[4] = {PerformSlot::Type::MACRO, 0, 12, 2};
        p.perform[7] = {PerformSlot::Type::EMPTY, 0, -1, 0};
        const auto back = songcore::parse_project(songcore::json::parse(songcore::serialize_project(p)));
        assert(back.perform == p.perform);
        // A malformed slot keeps its default; the others still load.
        auto j = songcore::json::parse(songcore::serialize_project(p));
        j["perform"][1] = "nonsense";
        j["perform"][4]["instrument"] = 999;
        const auto mended = songcore::parse_project(j);
        assert(mended.perform[0] == p.perform[0] && mended.perform[1] == songcore::default_perform_slots()[1]);
        assert(mended.perform[4].type == PerformSlot::Type::MACRO && mended.perform[4].instrument == -1);
    }

    StdFileSystem fs((out / "app-data").string());
    auto engine = std::make_unique<AudioEngine>();
    engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost host(engine.get(), 48000);
    assert(host.push_project(songcore::serialize_project(song())));
    assert(host.load_media("").failed == 0);
    host.push_params();

    // ── The way there: R+UP from PROJECT (columns 0, 1, 4), R+DOWN back ─────────────────────────
    assert(SCREEN_GRID[0][0] == ScreenType::PERFORM && SCREEN_GRID[0][1] == ScreenType::PERFORM && SCREEN_GRID[0][4] == ScreenType::PERFORM);
    assert(std::string(screen_label(ScreenType::PERFORM)) == "PERFORM" && help_screen_topic(ScreenType::PERFORM) == HelpTopic::SCREEN_PERFORM);
    AppState s;
    s.project = &host.edit_project();
    s.currentScreen = ScreenType::SONG;
    Input input(s, host, *engine, fs);
    input.combo(Button::R_SHIFT, Button::DPAD_UP);
    assert(s.currentScreen == ScreenType::PROJECT);
    input.combo(Button::R_SHIFT, Button::DPAD_UP);
    assert(s.currentScreen == ScreenType::PERFORM);
    input.combo(Button::R_SHIFT, Button::DPAD_DOWN);
    assert(s.currentScreen == ScreenType::PROJECT);
    input.combo(Button::R_SHIFT, Button::DPAD_UP);
    assert(s.currentScreen == ScreenType::PERFORM && s.perform.cursor == 0);

    // ── MUTE: tile 1 is tracks 1-2 ──────────────────────────────────────────────────────────────
    assert(perform_slot_title(host.project(), 0) == "MUTE 1-2" && perform_slot_detail(host.project(), 128, 0) == "playing");
    input.tap(Button::A);
    assert(host.project().tracks[0].mute && host.project().tracks[1].mute && !host.project().tracks[2].mute);
    assert(perform_group_muted(host.project(), 0) && perform_slot_detail(host.project(), 128, 0) == "MUTED");
    input.combo(Button::A, Button::DPAD_DOWN);   // A+↓ unmutes
    assert(!host.project().tracks[0].mute && !host.project().tracks[1].mute);
    // Setup: L+→ moves the track cursor, L+A adds track 3 to the group.
    input.combo(Button::L_SHIFT, Button::DPAD_RIGHT);
    input.combo(Button::L_SHIFT, Button::DPAD_RIGHT);
    assert(s.perform.trackCursor == 2);
    input.combo(Button::L_SHIFT, Button::A);
    assert(host.project().perform[0].tracks == 0x07 && perform_slot_title(host.project(), 0) == "MUTE 1-3");

    // ── FILTER: tile 4 ──────────────────────────────────────────────────────────────────────────
    input.tap(Button::DPAD_RIGHT); input.tap(Button::DPAD_RIGHT); input.tap(Button::DPAD_RIGHT);
    assert(s.perform.cursor == 3 && perform_slot_title(host.project(), 3) == "FILTER");
    input.combo(Button::A, Button::DPAD_UP);
    assert(engine->performFilterValue() == 144 && perform_slot_detail(host.project(), 144, 3) == "HP 12 %");
    input.combo(Button::A, Button::DPAD_DOWN);
    assert(engine->performFilterValue() == 128);
    // Crossing the centre stops there once: from HP 4 % a step of 16 down lands on OFF, not LP.
    input.combo(Button::A, Button::DPAD_RIGHT);
    assert(engine->performFilterValue() == 132);
    input.combo(Button::A, Button::DPAD_DOWN);
    assert(engine->performFilterValue() == 128);
    input.combo(Button::A, Button::DPAD_DOWN);
    assert(engine->performFilterValue() == 112 && perform_slot_detail(host.project(), 112, 3) == "LP 12 %");
    input.tap(Button::A);
    assert(engine->performFilterValue() == 128 && perform_slot_detail(host.project(), 128, 3) == "OFF");

    // ── MACRO: tile 5 picks the SP MORPH and moves its Morph ────────────────────────────────────
    input.tap(Button::DPAD_DOWN);
    assert(s.perform.cursor == 7);
    input.tap(Button::DPAD_LEFT); input.tap(Button::DPAD_LEFT); input.tap(Button::DPAD_LEFT);
    assert(s.perform.cursor == 4 && perform_slot_title(host.project(), 4) == "MACRO");
    input.combo(Button::B, Button::DPAD_UP);   // next synth: instrument 0
    assert(host.project().perform[4].instrument == 0 && perform_slot_title(host.project(), 4) == "MORPH");
    const int before = perform_slot_value(host.project(), 128, 4);
    input.combo(Button::A, Button::DPAD_UP);
    const int after = perform_slot_value(host.project(), 128, 4);
    std::printf("Morph on tile 5: %d → %d (%s)\n", before, after, host.project().instruments[0].sampleFilePath->c_str());
    assert(after >= before + 14 && after <= before + 16);
    input.combo(Button::L_SHIFT, Button::DPAD_RIGHT);   // macro B
    assert(perform_slot_title(host.project(), 4) == "MOTION");
    // B+→ changes the kind: MACRO → EMPTY, and back B+← → MACRO with the first synth again.
    input.combo(Button::B, Button::DPAD_RIGHT);
    assert(host.project().perform[4].type == PerformSlot::Type::EMPTY && perform_slot_title(host.project(), 4) == "EMPTY");
    input.combo(Button::B, Button::DPAD_LEFT);
    assert(host.project().perform[4].type == PerformSlot::Type::MACRO && host.project().perform[4].instrument == 0);

    // A snapshot of the screen while playing, with the CPU figures filled in.
    host.play_song(0);
    host.poll();
    render(*engine, 220);
    s.perform.polledMs = 0;
    s.perform.lateAtOpen = -1;
    {
        const auto timing = engine->getBlockTiming();
        const auto lanes = engine->getLaneLoads();
        for (int i = 0; i < 10; ++i) s.perform.laneLoad[i] = lanes.load[i];
        s.perform.meanLoad = timing.meanLoad;
        s.perform.worstLoad = timing.worstLoad;
        s.perform.lateBlocks = s.perform.lateAtOpen = timing.lateBlocks;
    }
    snapshot(out / "perform.ppm", s);
    host.stop();

    // ── The master filter in the engine ────────────────────────────────────────────────────────
    {
        auto run = [&](int filter, bool touchedBefore) {
            auto e = std::make_unique<AudioEngine>();
            e->setDeviceSampleRate(48000);
            songcore::SongcoreHost h(e.get(), 48000);
            assert(h.push_project(songcore::serialize_project(song())) && h.load_media("").failed == 0);
            h.push_params();
            h.play_song(0);
            h.poll();
            if (touchedBefore) e->setPerformFilter(20);
            render(*e, 40);   // the same song time in both runs
            e->setPerformFilter(filter);
            render(*e, 40);   // the glide settles
            return render(*e, 60);
        };
        const auto plain = run(128, false), returned = run(128, true);
        assert(plain == returned);   // back at the centre: bypassed, bit for bit
        const auto low = run(10, false), high = run(240, false);
        const double hp = high_energy(plain), hl = high_energy(low), lp = low_energy(plain), lh = low_energy(high);
        std::printf("Filter: highs %.3g open, %.3g at LP 92 %%; lows %.3g open, %.3g at HP 88 %%\n", hp, hl, lp, lh);
        assert(hl < hp / 10 && lh < lp / 10);
    }

    // ── CPU per track: a heavy engine on track 3 shows on lane 3 ───────────────────────────────
    {
        auto e = std::make_unique<AudioEngine>();
        e->setDeviceSampleRate(48000);
        songcore::SongcoreHost h(e.get(), 48000);
        assert(h.push_project(songcore::serialize_project(song(81))) && h.load_media("").failed == 0);   // E OMINOUS
        h.push_params();
        h.play_song(0);
        h.poll();
        render(*e, 200);   // a little over a second: one publication
        const auto lanes = e->getLaneLoads();
        std::printf("Lane loads (tenths of %%): 1=%d 2=%d 3=%d 4=%d … mix=%d\n", lanes.load[0], lanes.load[1], lanes.load[2],
                    lanes.load[3], lanes.load[9]);
        assert(lanes.load[2] > lanes.load[1] && lanes.load[2] > 0);
        for (int lane = 3; lane < 9; ++lane) assert(lanes.load[lane] == 0);
        assert(lanes.load[9] > 0);
        assert(perform_load_level(650) == 0 && perform_load_level(700) == 1 && perform_load_level(950) == 2);
    }

    std::puts("PASS: PERFORM — layout saved with the song (defaults not written, malformed slots mended), R+UP from "
              "PROJECT and back, MUTE group toggles and setup, FILTER sweep with a stop at OFF, MACRO picks a synth "
              "and moves its macro live, kind cycling; master filter bypassed bit for bit at the centre, LP and HP "
              "cut; CPU attributed to the heavy track");
    return 0;
}
