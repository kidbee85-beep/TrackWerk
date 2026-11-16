// ptdemo-fx-check: the demo effects (ui/demo_fx.h), the attract screen and the demo visualizers.
// Deterministic (same arguments, same pixels), confined to their box, reacting to their inputs, and
// cheap enough to draw every frame.
// usage: ptdemo-fx-check <new run directory>
#include "studio/rack.h"
#include "ui/demo_fx.h"
#include "ui/layout.h"
#include "ui/modules/oscilloscope.h"
#include "ui/scene_presets.h"
#include "ui/theme_io.h"
#include "songcore/project_io.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <vector>

using namespace pt::ui;

namespace {

int differing(const Canvas& a, const Canvas& b, int x, int y, int w, int h) {
    int n = 0;
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            n += a.pixels()[py * DESIGN_W + px] != b.pixels()[py * DESIGN_W + px];
    return n;
}

Canvas blank() {
    Canvas c;
    c.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000);
    return c;
}

// Draws `effect` into a 300×120 box at (100, 100) and checks it stays inside and repeats exactly.
void confined(const char* name, const std::function<void(Canvas&)>& effect) {
    Canvas a = blank(), b = blank();
    const Canvas empty = blank();
    effect(a);
    effect(b);
    assert(differing(a, b, 0, 0, DESIGN_W, DESIGN_H) == 0);
    const int inside = differing(a, empty, 100, 100, 300, 120);
    const int all = differing(a, empty, 0, 0, DESIGN_W, DESIGN_H);
    if (inside < 200 || all != inside) std::cerr << name << ": inside " << inside << ", all " << all << "\n";
    assert(inside >= 200 && all == inside);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) {
        std::cerr << "usage: ptdemo-fx-check <new run directory>\n";
        return 2;
    }
    const Theme t = theme_default();

    // ── The effects ───────────────────────────────────────────────────────────────────────────
    confined("starfield", [&](Canvas& c) { demo_fx::starfield(c, 100, 100, 300, 120, t, 5000, .5f, 300); });
    confined("plasma", [&](Canvas& c) { demo_fx::plasma(c, 100, 100, 300, 120, t, 5000, 8, .3f); });
    confined("copper", [&](Canvas& c) { demo_fx::copper(c, 100, 100, 300, 120, t, 5000); });
    confined("scroller", [&](Canvas& c) {
        demo_fx::scroller(c, 100, 140, 300, 20, "TRACKWERK DEMO SCROLLER", t, 5000, 18, 80);
    });
    // Time moves them.
    {
        Canvas a = blank(), b = blank();
        demo_fx::plasma(a, 100, 100, 300, 120, t, 5000);
        demo_fx::plasma(b, 100, 100, 300, 120, t, 5400);
        assert(differing(a, b, 100, 100, 300, 120) > 1000);
    }
    // The ramp runs from the ground to the ink.
    assert(demo_fx::ramp(t, 0) == t.background && demo_fx::ramp(t, 255) == t.textValue);

    // ── Attract screen ────────────────────────────────────────────────────────────────────────
    auto project = songcore::make_default_project();
    project.name = "BLUE HOUR";
    AppState s;
    s.project = &project;
    s.theme = theme_default();
    s.attractActive = true;
    s.attractStarted = 1000;
    TrackerLayout layout;
    auto frame = [&](uint64_t ms) {
        Canvas c;
        s.animationMs = s.attractStarted + ms;
        layout.draw(c, s);
        return c;
    };
    const Canvas part1 = frame(3000), part2 = frame(15000), part3 = frame(27000);
    assert(differing(part1, frame(3000), 0, 0, DESIGN_W, DESIGN_H) == 0);
    assert(differing(part1, part2, 0, 0, DESIGN_W, DESIGN_H) > 50000);
    assert(differing(part2, part3, 0, 0, DESIGN_W, DESIGN_H) > 50000);
    // The three parts cycle: 36 s later part 1 is back (its stars and scroller have moved on).
    {
        const Canvas again = frame(3000 + 3 * demo_fx::kAttractPartMs);
        assert(differing(part1, again, 0, 0, DESIGN_W, DESIGN_H) < differing(part2, again, 0, 0, DESIGN_W, DESIGN_H));
        assert(differing(part1, again, 0, 0, DESIGN_W, DESIGN_H) < differing(part3, again, 0, 0, DESIGN_W, DESIGN_H));
    }
    // Every design draws its own demo.
    s.theme = scene_preset(20, VisualizerType::OCTA);
    assert(differing(part2, frame(15000), 0, 0, DESIGN_W, DESIGN_H) > 50000);
    s.theme = theme_default();
    // Drawing time, as a budget: the Miyoo Mini's A7 is roughly 20× slower than this host.
    const auto start = std::chrono::steady_clock::now();
    int frames = 0;
    for (uint64_t ms = 0; ms < 36000; ms += 120, ++frames) frame(ms);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / frames;
    std::printf("attract: %.0f us per frame on this host\n", us);
    assert(us < 2500);

    // ── Demo visualizers ──────────────────────────────────────────────────────────────────────
    std::vector<float> wave(WAVEFORM_SIZE), loud(WAVEFORM_SIZE), tracks(TRACK_WAVEFORM_COUNT * WAVEFORM_SIZE);
    for (int i = 0; i < WAVEFORM_SIZE; ++i) loud[size_t(i)] = .4f * std::sin(i * .07f);
    for (int k = 0; k < 8; ++k)
        for (int i = 0; i < WAVEFORM_SIZE; ++i) tracks[size_t(k * WAVEFORM_SIZE + i)] = .1f * (k + 1) * std::sin(i * .05f);
    for (VisualizerType v : {VisualizerType::COPPER, VisualizerType::STARFIELD, VisualizerType::PLASMA}) {
        Theme vt = theme_default();
        vt.visualizerType = v;
        Theme back;
        assert(parse_theme(serialize_theme(vt), back) && back.visualizerType == v);
        auto strip = [&](const std::vector<float>& signal) {
            Canvas c = blank();
            OscilloscopeState st;
            st.theme = vt;
            st.waveform = signal.data();
            st.trackWaveforms = tracks.data();
            st.activeTrackMask = signal[1] != 0.f ? 0xFF : 0x01;
            st.animationMs = 4321;
            st.playing = true;
            OscilloscopeModule{}.draw(c, 10, 10, st);
            return c;
        };
        const Canvas quiet = strip(wave), sound = strip(loud);
        assert(differing(quiet, strip(wave), 0, 0, DESIGN_W, DESIGN_H) == 0);
        assert(differing(quiet, sound, 10, 10, OscilloscopeModule::WIDTH, OscilloscopeModule::HEIGHT) > 50);
        assert(differing(quiet, blank(), 0, 10 + OscilloscopeModule::HEIGHT, DESIGN_W,
                         DESIGN_H - 10 - OscilloscopeModule::HEIGHT) == 0);
    }
    // ── Status entrance and cursor glide (TrackerLayout) ─────────────────────────────────────
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.currentScreen = ScreenType::PHRASE;
        TrackerLayout l;
        Canvas c;
        st.animationMs = 1000;
        l.draw(c, st);
        st.statusMessage = "SAVED";
        st.animationMs = 1010;
        l.draw(c, st);
        const Canvas entering = c;
        assert(l.has_motion(1010));
        st.animationMs = 1400;
        l.draw(c, st);
        assert(!l.has_motion(1400));
        Canvas still;
        TrackerLayout fresh;
        st.animationMs = 0;      // no clock: the message drawn in place
        fresh.draw(still, st);
        st.animationMs = 1400;
        assert(differing(c, still, 0, 0, DESIGN_W, DESIGN_H) == 0);
        assert(differing(entering, still, 0, 0, DESIGN_W, 80) > 30);
        // The cursor glides: a moved cursor keeps the layout in motion for 110 ms, then settles to
        // exactly what a fresh layout draws.
        st.statusMessage.clear();
        st.animationMs = 2000;
        l.draw(c, st);
        st.cursorRow = 9;
        st.animationMs = 2010;
        l.draw(c, st);
        assert(l.has_motion(2050));
        st.animationMs = 2200;
        l.draw(c, st);
        Canvas settled;
        TrackerLayout other;
        other.draw(settled, st);
        assert(!l.has_motion(2200) && differing(c, settled, 0, 0, DESIGN_W, DESIGN_H) == 0);
        // An edit at the cursor flashes its cell for 150 ms, then the frame is the settled one again.
        st.projectVersion += 1;
        st.animationMs = 2300;
        Canvas flash;
        l.draw(flash, st);
        assert(l.has_motion(2350) && differing(flash, settled, 0, 0, DESIGN_W, DESIGN_H) > 20);
        st.animationMs = 2500;
        l.draw(c, st);
        assert(!l.has_motion(2500) && differing(c, settled, 0, 0, DESIGN_W, DESIGN_H) == 0);
    }
    // ── Scrolling lists mark their viewport and offset: SONG, the FX list and Harmony's settings ──
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        const auto mark = [&](auto&& setup) {
            TrackerLayout l;
            Canvas c;
            setup();
            l.draw(c, st);
            return c.scroll_mark();
        };
        st.currentScreen = ScreenType::SONG;
        const auto songTop = mark([&] { st.songScrollPosition = 0; });
        const auto songOn = mark([&] { st.songScrollPosition = 3; });
        assert(songTop.id == 1 && songOn.id == 1 && songOn.offset - songTop.offset == 3 * (songTop.view.h / 16));
        st.studioOpen = true;
        st.studioTab = 1;
        const auto fxTop = mark([&] { st.studioRow = 1; });
        const auto fxOn = mark([&] { st.studioRow = 9; });
        assert(fxTop.id == 11 && fxTop.offset == 32 && fxOn.offset > fxTop.offset && fxOn.view.y == fxTop.view.y);
        st.studioTab = 8;
        const auto harmonyTop = mark([&] { st.harmony.cursor = 0; });
        const auto harmonyOn = mark([&] { st.harmony.cursor = 5; });
        assert(harmonyTop.id == 40 && harmonyOn.offset - harmonyTop.offset == 3 * 19);
        st.studioOpen = false;
    }
    // ── The Studio tab and screen-map highlights glide to the new entry, then settle exactly ──
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        for (bool studio : {true, false}) {
            st.studioOpen = studio;
            st.studioTab = 1;
            st.currentScreen = ScreenType::PHRASE;
            TrackerLayout l;
            Canvas c;
            st.animationMs = 2500;   // the first frame starts the (empty) status entrance
            l.draw(c, st);
            st.animationMs = 3000;
            l.draw(c, st);
            const auto from = c.selector_mark();
            assert(from.id == (studio ? 2 : 1) && !l.has_motion(3000));
            if (studio) st.studioTab = 3; else st.currentScreen = ScreenType::INSTRUMENT;
            st.animationMs = 3010;
            Canvas moving;
            l.draw(moving, st);
            TrackerLayout still;
            Canvas plain;
            still.draw(plain, st);
            assert(l.has_motion(3060) && differing(moving, plain, 0, 0, DESIGN_W, DESIGN_H) > 100);
            assert(moving.selector_mark().at.x != from.at.x || moving.selector_mark().at.y != from.at.y);
            st.animationMs = 3200;
            l.draw(c, st);
            Canvas settled;
            TrackerLayout other;
            other.draw(settled, st);
            assert(!l.has_motion(3200) && differing(c, settled, 0, 0, DESIGN_W, DESIGN_H) == 0);
            // Motion switched off on the way: the bar is gone at once.
            if (studio) st.studioTab = 1; else st.currentScreen = ScreenType::PHRASE;
            st.animationMs = 3300;
            l.draw(c, st);
            st.settings.sceneMotion = false;
            st.animationMs = 3320;
            l.draw(c, st);
            Canvas off;
            TrackerLayout fresh;
            fresh.draw(off, st);
            assert(!l.has_motion(3320) && differing(c, off, 0, 0, DESIGN_W, DESIGN_H) == 0);
            st.settings.sceneMotion = true;
        }
        st.studioOpen = false;
    }
    // ── Knob, synth level blocks and mixer faders ease to a new value, then settle exactly ──────
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        const auto check = [&](auto&& setup, auto&& change) {
            setup();
            TrackerLayout l;
            Canvas c;                       // one canvas across frames, as the shell keeps it
            st.animationMs = 4000;
            l.draw(c, st);
            change();
            st.animationMs = 4020;
            l.draw(c, st);
            const Canvas moving = c;
            Canvas plain;
            TrackerLayout fresh;
            fresh.draw(plain, st);
            assert(c.tweening(4060) && differing(moving, plain, 0, 0, DESIGN_W, DESIGN_H) > 10);
            st.animationMs = 4200;
            l.draw(c, st);
            Canvas settled;
            TrackerLayout other;
            other.draw(settled, st);
            assert(!c.tweening(4200) && differing(c, settled, 0, 0, DESIGN_W, DESIGN_H) == 0);
            // Without a clock the new value is drawn at once.
            st.animationMs = 0;
            Canvas still;
            TrackerLayout still_layout;
            still_layout.draw(still, st);
            assert(!still.tweening(1));
        };
        // FX page: the Wet / Dry knob.
        check([&] { st.studioOpen = true; st.studioTab = 1; st.studioRow = 2; p.studioMaster[studio::FX_MIX] = 20; },
              [&] { p.studioMaster[studio::FX_MIX] = 220; });
        // MASTER page: the dynamics transfer curve morphs to a new threshold and ratio.
        check([&] { st.studioOpen = true; st.studioTab = 2; st.studioRow = 1; p.studioMaster[studio::ENABLE] = 1;
                    p.studioMaster[studio::THRESHOLD] = 230; p.studioMaster[studio::RATIO] = 10; },
              [&] { p.studioMaster[studio::THRESHOLD] = 40; p.studioMaster[studio::RATIO] = 240; });
        // MIXER: track 1's fader line.
        check([&] { st.studioOpen = false; st.currentScreen = ScreenType::MIXER; p.tracks[0].volume = 0x20; },
              [&] { p.tracks[0].volume = 0xE0; });
        st.animationMs = 0;
    }
    // ── The cursor glides between the synth controls too, then settles exactly ─────────────────
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.studioOpen = true;
        st.studioTab = 0;
        st.studioRow = 3;
        TrackerLayout l;
        Canvas c;
        st.animationMs = 6000;
        l.draw(c, st);
        const auto first = c.cursor_mark();
        st.animationMs = 6300;
        l.draw(c, st);
        st.studioRow = 4;
        st.animationMs = 6310;
        l.draw(c, st);
        assert(first.w > 0 && c.cursor_mark().x != first.x && l.has_motion(6350));
        st.animationMs = 6500;
        l.draw(c, st);
        Canvas settled;
        TrackerLayout other;
        other.draw(settled, st);
        assert(!l.has_motion(6500) && differing(c, settled, 0, 0, DESIGN_W, DESIGN_H) == 0);
        st.studioOpen = false;
    }
    // ── The phrase playhead glides down from step to step and jumps back at the wrap ───────────
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.currentScreen = ScreenType::PHRASE;
        st.currentPhrase = 0;
        st.isPlaying = true;
        st.playheads[0].phraseId = 0;
        TrackerLayout l;
        Canvas c;
        const auto at = [&](int step, uint64_t ms) { st.playheads[0].step = step; st.animationMs = ms; l.draw(c, st); };
        const auto fresh = [&] { Canvas f; TrackerLayout other; other.draw(f, st); return f; };
        at(4, 8000);
        at(4, 8300);
        at(5, 8310);
        const Canvas moving = c;
        assert(c.tweening(8350) && differing(moving, fresh(), 0, 0, DESIGN_W, DESIGN_H) > 100);
        at(5, 8500);
        assert(!c.tweening(8500) && differing(c, fresh(), 0, 0, DESIGN_W, DESIGN_H) == 0);
        at(15, 8600);
        at(15, 8800);
        at(0, 8810);                       // the wrap: no sweep back up
        assert(!c.tweening(8811) && differing(c, fresh(), 0, 0, DESIGN_W, DESIGN_H) == 0);
        // CHAIN: the same glide from chain row to chain row.
        st.currentScreen = ScreenType::CHAIN;
        st.currentChain = 0;
        st.playheads[0].chainId = 0;
        const auto row = [&](int r, uint64_t ms) { st.playheads[0].chainRow = r; st.animationMs = ms; l.draw(c, st); };
        row(1, 9000);
        row(1, 9300);
        row(2, 9310);
        assert(c.tweening(9350) && differing(c, fresh(), 0, 0, DESIGN_W, DESIGN_H) > 100);
        row(2, 9500);
        assert(!c.tweening(9500) && differing(c, fresh(), 0, 0, DESIGN_W, DESIGN_H) == 0);
        st.isPlaying = false;
        st.playheads[0] = {};
    }
    // ── Show view peak caps: a loud moment leaves caps over silent columns, which fall to nothing ──
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.trackMask = 0xFF;
        std::vector<float> silent(tracks.size(), 0.f);
        demo_fx::ShowPeaks peaks;
        Canvas c;
        st.trackWaveforms = tracks.data();
        st.animationMs = 7000;
        demo_fx::draw_show(c, st, &peaks);
        st.trackWaveforms = silent.data();
        st.animationMs = 7100;
        Canvas held, plain;
        demo_fx::draw_show(held, st, &peaks);
        demo_fx::draw_show(plain, st);
        assert(differing(held, plain, 0, 100, DESIGN_W, 280) > 500);   // eight caps hold
        st.animationMs = 9000;
        Canvas gone, plainLater;
        demo_fx::draw_show(gone, st, &peaks);
        demo_fx::draw_show(plainLater, st);
        assert(differing(gone, plainLater, 0, 0, DESIGN_W, DESIGN_H) == 0);
        st.animationMs = 0;                                             // no clock: no caps
        demo_fx::ShowPeaks still;
        Canvas a, b;
        st.trackWaveforms = tracks.data();
        demo_fx::draw_show(a, st, &still);
        demo_fx::draw_show(b, st);
        assert(differing(a, b, 0, 0, DESIGN_W, DESIGN_H) == 0);
    }
    // ── Motion switched off while a status entrance and a trigger LED run: both end at once ────
    {
        auto p = songcore::make_default_project();
        p.phrases[0].steps[4].note = songcore::Note::C4();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.currentScreen = ScreenType::SONG;
        st.isPlaying = true;
        st.playheads[2].phraseId = 0;
        st.playheads[2].step = 3;
        TrackerLayout l;
        Canvas c;
        st.animationMs = 1000;
        l.draw(c, st);
        st.statusMessage = "SAVED";
        st.playheads[2].step = 4;
        st.animationMs = 1010;
        l.draw(c, st);
        assert(l.has_motion(1050));
        st.settings.sceneMotion = false;
        st.animationMs = 1050;
        l.draw(c, st);
        TrackerLayout other;
        Canvas off;
        other.draw(off, st);
        assert(!l.has_motion(1050) && differing(c, off, 0, 0, DESIGN_W, DESIGN_H) == 0);
    }
    // ── Track rail trigger LEDs ───────────────────────────────────────────────────────────────
    {
        auto p = songcore::make_default_project();
        p.phrases[0].steps[4].note = songcore::Note::C4();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.currentScreen = ScreenType::SONG;
        st.isPlaying = true;
        st.playheads[2].phraseId = 0;
        st.playheads[2].step = 3;   // an empty step: no flash
        TrackerLayout l;
        Canvas before, lit, after;
        st.animationMs = 5000;
        l.draw(before, st);
        assert(!l.has_motion(5000));
        st.playheads[2].step = 4;   // a note: track 3's row flashes
        st.animationMs = 5010;
        l.draw(lit, st);
        assert(l.has_motion(5100));
        st.animationMs = 5400;
        l.draw(after, st);
        assert(!l.has_motion(5400));
        assert(differing(lit, after, 0, 0, DESIGN_W, DESIGN_H) > 100);
    }
    // ── Frame motion (Canvas::afterglow, Canvas::blinds) ─────────────────────────────────────
    {
        Canvas now = blank(), before = blank();
        now.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xffc8c8c8);
        before.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff404040);
        std::vector<uint32_t> old(before.pixels(), before.pixels() + DESIGN_W * DESIGN_H);
        Canvas same = now;
        same.afterglow(old.data(), 0);
        assert(differing(same, now, 0, 0, DESIGN_W, DESIGN_H) == 0);   // keep 0: untouched
        Canvas half = now;
        half.afterglow(old.data(), 128);
        assert(half.pixels()[1234] == 0xff848484u);                    // (0xc8 + 0x40) / 2
        Canvas wipe = now;
        wipe.blinds(old.data(), 1);
        int fresh = 0;
        for (int y = 0; y < DESIGN_H; ++y) fresh += wipe.pixels()[y * DESIGN_W + 7] == 0xffc8c8c8u;
        assert(fresh == DESIGN_H / 4);                                 // one line in four is new
        Canvas done = now;
        done.blinds(old.data(), 4);
        assert(differing(done, now, 0, 0, DESIGN_W, DESIGN_H) == 0);
        // Family transitions: at 1 the new frame untouched, half-way a real mix of old and new pixels.
        using T = Canvas::Transition;
        for (T kind : {T::BLINDS, T::DISSOLVE, T::WIPE_DOWN, T::WIPE_RIGHT, T::IRIS, T::WIPE_UP, T::WIPE_LEFT,
                       T::PUSH_RIGHT, T::PUSH_LEFT, T::PUSH_DOWN, T::PUSH_UP}) {
            Canvas full = now;
            full.transition(old.data(), kind, 1.f);
            assert(differing(full, now, 0, 0, DESIGN_W, DESIGN_H) == 0);
            Canvas half = now;
            half.transition(old.data(), kind, .5f);
            int fresh = 0, stale = 0;
            for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) {
                fresh += half.pixels()[i] == 0xffc8c8c8u;
                stale += half.pixels()[i] == 0xff404040u;
            }
            assert(fresh > DESIGN_W * DESIGN_H / 8 && stale > DESIGN_W * DESIGN_H / 8);
        }
    }
    // Limited to a stage: outside it the new frame at once, inside a real mix, at 1 the new frame.
    {
        Canvas fresh;
        fresh.clear(0xffc8c8c8u);
        std::vector<uint32_t> old(size_t(DESIGN_W) * DESIGN_H, 0xff404040u);
        const Canvas::Rect stage{131, 80, 499, 390};
        using T = Canvas::Transition;
        for (T kind : {T::BLINDS, T::DISSOLVE, T::WIPE_DOWN, T::WIPE_RIGHT, T::IRIS, T::WIPE_UP, T::WIPE_LEFT,
                       T::PUSH_RIGHT, T::PUSH_LEFT, T::PUSH_DOWN, T::PUSH_UP}) {
            for (float progress : {.2f, .5f, .8f}) {
                Canvas c = fresh;
                c.transition(old.data(), kind, progress, 0xff00ff00u, stage);
                int outsideChanged = 0, insideOld = 0;
                for (int y = 0; y < DESIGN_H; ++y)
                    for (int x = 0; x < DESIGN_W; ++x) {
                        const bool in = x >= stage.x && x < stage.x + stage.w && y >= stage.y && y < stage.y + stage.h;
                        const uint32_t v = c.pixels()[y * DESIGN_W + x];
                        if (!in) outsideChanged += v != 0xffc8c8c8u;
                        else insideOld += v == 0xff404040u;
                    }
                assert(outsideChanged == 0 && insideOld > 0);
            }
            Canvas done = fresh;
            done.transition(old.data(), kind, 1.f, 0xff00ff00u, stage);
            assert(differing(done, fresh, 0, 0, DESIGN_W, DESIGN_H) == 0);
        }
    }
    // The row-wise iris equals the per-pixel rule of 0092 at every step the shell draws, ring included.
    {
        Canvas fresh;
        fresh.clear(0xffc8c8c8u);
        std::vector<uint32_t> old(size_t(DESIGN_W) * DESIGN_H, 0xff404040u);
        for (float progress : {.02f, .2f, .4f, .6f, .8f, .95f}) {
            for (Argb edge : {Argb{0}, Argb{0xff00ff00u}}) {
                Canvas c = fresh;
                c.transition(old.data(), Canvas::Transition::IRIS, progress, edge);
                const float r = progress * 1.15f;
                int wrong = 0;
                for (int y = 0; y < DESIGN_H; ++y) {
                    const float dy = (y - DESIGN_H / 2) / float(DESIGN_H / 2);
                    for (int x = 0; x < DESIGN_W; ++x) {
                        const float dx = (x - DESIGN_W / 2) / float(DESIGN_W / 2);
                        const float d = dx * dx + dy * dy;
                        const uint32_t want = d > r * r ? 0xff404040u
                                            : (edge >> 24) && d > (r - .02f) * (r - .02f) ? 0xff00ff00u
                                                                                         : 0xffc8c8c8u;
                        wrong += c.pixels()[y * DESIGN_W + x] != want;
                    }
                }
                assert(wrong == 0);
            }
        }
    }
    // Smooth scrolling: a list of 20-pixel rows, each row filled with its content number, scrolled
    // up by one row. Half-way every line of the view shows the content 10 pixels further up than the
    // new frame; at the end the new frame untouched; outside the view nothing changes.
    {
        const Canvas::Rect view{100, 100, 200, 200};
        const auto list = [&](int scroll) {
            Canvas c;
            c.clear(0xff000000u);
            for (int y = view.y; y < view.y + view.h; ++y) {
                const int content = (y - view.y + scroll) / 20;
                c.fill_rect(view.x, y, view.w, 1, 0xff000000u | static_cast<uint32_t>(content * 9 + 10));
            }
            return c;
        };
        const Canvas before = list(0), after = list(20);
        std::vector<uint32_t> old(before.pixels(), before.pixels() + DESIGN_W * DESIGN_H);
        Canvas end = after;
        end.slide(old.data(), view, 20, 0);
        assert(differing(end, after, 0, 0, DESIGN_W, DESIGN_H) == 0);
        Canvas start = after;
        start.slide(old.data(), view, 20, 20);
        assert(differing(start, before, 0, 0, DESIGN_W, DESIGN_H) == 0);   // remaining = all: the old frame
        Canvas half = after;
        half.slide(old.data(), view, 20, 10);
        const Canvas mid = list(10);
        assert(differing(half, mid, 0, 0, DESIGN_W, DESIGN_H) == 0);       // exactly the list at scroll 10
        Canvas back = before;                                               // and scrolling down
        std::vector<uint32_t> was(after.pixels(), after.pixels() + DESIGN_W * DESIGN_H);
        back.slide(was.data(), view, -20, -10);
        assert(differing(back, mid, 0, 0, DESIGN_W, DESIGN_H) == 0);
        Canvas far = after;
        far.slide(old.data(), view, 200, 100);                              // a jump of a whole view: none
        assert(differing(far, after, 0, 0, DESIGN_W, DESIGN_H) == 0);
    }
    // ── Reference colours (1.2): one colour per number, neighbours apart; the volume bar follows VOL ──
    {
        const Theme th = theme_default();
        assert(ref_color(th, 5) == ref_color(th, 5));
        for (int id = 0; id < 16; ++id) assert(ok_delta_e(ref_color(th, id), ref_color(th, id + 1)) > 0.05);
        auto p = songcore::make_default_project();
        p.tracks[0].chainRefs = {3};
        AppState st;
        st.project = &p;
        st.theme = th;
        st.currentScreen = ScreenType::SONG;
        st.cursorRow = 5;   // the cursor away from row 0
        Canvas with, without;
        TrackerLayout a, b;
        a.draw(with, st);
        p.tracks[0].chainRefs = {-1};
        b.draw(without, st);
        assert(differing(with, without, 0, 0, DESIGN_W, DESIGN_H) > 60);   // the chip and the number
        // PHRASE: a loud and a soft note draw bars of different length.
        st.currentScreen = ScreenType::PHRASE;
        st.currentPhrase = 0;
        p.phrases[0].steps[2].note = songcore::Note::C4();
        p.phrases[0].steps[2].volume = 0x7F;
        Canvas loud, soft;
        TrackerLayout c1, c2;
        c1.draw(loud, st);
        p.phrases[0].steps[2].volume = 0x20;
        c2.draw(soft, st);
        assert(differing(loud, soft, 0, 0, DESIGN_W, DESIGN_H) > 10);
    }
    // ── Instrument colours (1.2): opposite the reference walk, drawn only for a slot with a sound ──
    {
        const Theme th = theme_default();
        assert(inst_color(th, 0) != ref_color(th, 0));
        for (int id = 0; id < 16; ++id) assert(ok_delta_e(inst_color(th, id), inst_color(th, id + 1)) > 0.05);
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = th;
        st.currentScreen = ScreenType::INST_POOL;
        st.currentInstrument = 5;   // the cursor away from slot 00, the list still at its top
        Canvas freeSlot, used;
        TrackerLayout a, b;
        a.draw(freeSlot, st);
        p.instruments[0].sampleFilePath = std::string("/@mutable/0/0");
        b.draw(used, st);
        assert(differing(freeSlot, used, 0, 0, 40, DESIGN_H) > 20);   // the mark left of the slot number
    }
    // ── Title underlines (1.2): CHAIN and PHRASE titles wear their number's colour ──────────────
    {
        auto p = songcore::make_default_project();
        AppState st;
        st.project = &p;
        st.theme = theme_default();
        st.currentScreen = ScreenType::CHAIN;
        Canvas c1, c2;
        TrackerLayout a, b;
        st.currentChain = 1;
        a.draw(c1, st);
        st.currentChain = 2;
        b.draw(c2, st);
        // The title area: the digit changes too, so only count the underline row below the text.
        int under = 0;
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 200; ++x)
                under += c1.pixels()[y * DESIGN_W + x] != c2.pixels()[y * DESIGN_W + x] &&
                         c1.pixels()[y * DESIGN_W + x] == (ref_color(st.theme, 1) | 0xff000000u);
        assert(under > 50);
    }
    // ── The colour cache never goes stale across themes (the Scene browser draws several a frame) ──
    {
        const Theme a = theme_default(), b = scene_preset(20, VisualizerType::SCOPE);
        for (int round = 0; round < 3; ++round)
            for (int id : {0, 1, 7, 42, 255, 300}) {
                assert(ref_color(a, id) == ref_color_detail::walk(a.rowCursor, 0.0, id));
                assert(ref_color(b, id) == ref_color_detail::walk(b.rowCursor, 0.0, id));
                assert(inst_color(a, id) == ref_color_detail::walk(a.rowCursor, 180.0, id));
                assert(inst_color(b, id) == ref_color_detail::walk(b.rowCursor, 180.0, id));
            }
    }
    std::cout << "ptdemo-fx-check: starfield, plasma, copper and scroller confined and deterministic; attract parts, "
                 "loop and per-design look; COPPER / STARFIELD / PLASMA react to sound and save by name; afterglow, blinds and eleven transitions exact, also limited to a stage, the iris pixel-equal to the per-pixel rule; smooth scrolling lands on every in-between list exactly and the lists mark their scroll; status entrance, cursor glide, edit flash and rail trigger LEDs settle exactly; show peak caps hold and fall; reference colours stay per number and the volume bar follows VOL; instrument colours mark used slots; CHAIN and PHRASE titles underline in their colour; the colour cache follows every theme; Studio tab and screen-map highlights glide and settle; knob, fader and dynamics curve ease and settle; the cursor glides between synth controls; the phrase and chain playheads glide forward and jump at the wrap; Motion off ends running effects at once\n";
    return 0;
}
