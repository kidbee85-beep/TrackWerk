// SPDX-License-Identifier: GPL-3.0-or-later
// ptstudio-xy-check: the XY pad page of Studio > Synth. It is the last page of every synth model (R+Left/Right
// reaches it), shows the first two macros as X and Y, and while L (the audition) is held the D-pad moves the dot
// instead of the cursor. Without L the D-pad navigates as before; A + arrows edits the two cards as on any page.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/synth.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>

using namespace pt::ui;
namespace fs = std::filesystem;

namespace {

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState s;
    StdFileSystem sfs;
    explicit Rig(const fs::path& dir) : sfs((dir / "data").string()) {
        assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
        s.project = &host.edit_project();
        s.studioOpen = true;
        s.studioTab = 0;
        s.currentInstrument = 2;
        s.studioSynthInstrument = 2;   // already bound: no re-open that would reset the page
        s.studioModel = studio::kDubSirenModel;
        s.studioHarmonics = 100;
        s.studioTimbre = 150;
        s.studioMorph = 128;
    }
    bool send(Button b, bool down, ButtonMods m) { return studio_button(ButtonEvent{b, down ? ButtonAction::PRESSED : ButtonAction::RELEASED, m}, s, host, *engine, sfs); }
    void tap(Button b, ButtonMods m = {}) { send(b, true, m); send(b, false, m); }
};

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path dir = fs::absolute(argv[1]);
    fs::create_directories(dir);

    // Every model has the page, as the last one, with two controls named after its first two macros.
    for (int model : {0, 24, 78, 102, studio::kDubSirenModel}) {
        Rig r(dir / ("m" + std::to_string(model)));
        r.s.studioModel = model;
        const int pages = studio_synth_page_count(r.s);
        r.s.studioSynthPage = pages - 1;
        assert(studio_synth_xy_page(r.s) && studio_synth_page_name(r.s) == "XY pad" && studio_synth_control_count(r.s) == 2);
        assert(studio_synth_control_label(r.s, 0) == std::string("X  ") + studio::macro_name(model, 0));
        assert(studio_synth_control_label(r.s, 1) == std::string("Y  ") + studio::macro_name(model, 1));
        r.s.studioSynthPage = pages - 2;
        assert(!studio_synth_xy_page(r.s));   // the page before it (ADSR) is not the pad
        assert(studio_synth_page_name(r.s) == "ADSR envelope");
    }

    Rig r(dir / "dub");
    r.s.studioSynthPage = studio_synth_page_count(r.s) - 1;
    r.s.studioRow = 3;
    ButtonMods none, held;
    held.l = true;
    assert(studio_synth_control_level(r.s, 0) == 100 && studio_synth_control_level(r.s, 1) == 150);

    // L held: the D-pad moves the dot (4 per press); up raises Y; the cursor stays where it was.
    r.tap(Button::DPAD_RIGHT, held);
    assert(r.s.studioHarmonics == 104 && r.s.studioTimbre == 150 && r.s.studioRow == 3);
    r.tap(Button::DPAD_UP, held);
    assert(r.s.studioHarmonics == 104 && r.s.studioTimbre == 154 && r.s.studioRow == 3);
    r.tap(Button::DPAD_DOWN, held);
    r.tap(Button::DPAD_DOWN, held);
    r.tap(Button::DPAD_LEFT, held);
    assert(r.s.studioHarmonics == 100 && r.s.studioTimbre == 146 && r.s.studioSynthCustom);
    assert(studio_synth_control_level(r.s, 0) == 100 && studio_synth_control_value(r.s, 1) == "57 %");

    // The edges hold.
    for (int i = 0; i < 80; ++i) { r.tap(Button::DPAD_RIGHT, held); r.tap(Button::DPAD_UP, held); }
    assert(r.s.studioHarmonics == 255 && r.s.studioTimbre == 255);
    for (int i = 0; i < 80; ++i) { r.tap(Button::DPAD_LEFT, held); r.tap(Button::DPAD_DOWN, held); }
    assert(r.s.studioHarmonics == 0 && r.s.studioTimbre == 0);

    // Without L the D-pad navigates (the dot stays), and A + arrows edits the two cards like any page.
    r.s.studioHarmonics = 100; r.s.studioTimbre = 100;
    r.tap(Button::DPAD_RIGHT, none);
    assert(r.s.studioHarmonics == 100 && r.s.studioRow == 4);   // the cursor moved from X to Y
    ButtonMods a; a.a = true;
    r.send(Button::A, true, a);
    r.send(Button::DPAD_RIGHT, true, a);
    r.send(Button::DPAD_RIGHT, false, a);
    r.send(Button::A, false, none);
    assert(r.s.studioTimbre == 101 && r.s.studioHarmonics == 100);   // row 4 is Y

    // A page where L + D-pad means what it always did: the oscillator page (no dot moves).
    r.s.studioSynthPage = 0; r.s.studioRow = 3;
    r.s.studioHarmonics = 100;
    r.tap(Button::DPAD_RIGHT, held);
    assert(r.s.studioHarmonics == 100);
    std::printf("STUDIO_XY_PASS\n");
}
