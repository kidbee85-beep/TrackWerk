// ptshow-view-check: SELECT + START opens the show view through the normal button path, SELECT's
// release then opens no help, and the view follows the tracks and the mute switches.
// usage: ptshow-view-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/demo_fx.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace pt::ui;

namespace {
int differing(const Canvas& a, const Canvas& b) {
    int n = 0;
    for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) n += a.pixels()[i] != b.pixels()[i];
    return n;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) {
        std::cerr << "usage: ptshow-view-check <new run directory>\n";
        return 2;
    }
    std::filesystem::create_directories(argv[1]);
    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host(engine.get(), 48000);
    auto project = songcore::make_default_project();
    project.name = "STAGE SET";
    assert(host.push_project(songcore::serialize_project(project)));
    AppState state;
    state.project = &host.edit_project();
    state.currentScreen = ScreenType::SONG;
    StdFileSystem fs((std::filesystem::path(argv[1]) / "data").string());
    InputDispatcher dispatch(state, host, fs);
    MapperState mapper;
    uint64_t now = 1000;
    auto event = [&](Button b, ButtonAction a, ButtonMods m = {}) {
        now += 20;
        dispatch.set_now(static_cast<long long>(now));
        const ButtonEvent e{b, a, m};
        if (!studio_button(e, state, host, *engine, fs)) handle_button(e, dispatch, mapper, now);
    };

    // SELECT held, START: the show view. Releasing SELECT afterwards opens no help.
    ButtonMods select;
    select.select = true;
    event(Button::SELECT, ButtonAction::PRESSED, select);
    event(Button::START, ButtonAction::PRESSED, select);
    assert(state.showActive && !host.is_playing());
    event(Button::START, ButtonAction::RELEASED, select);
    event(Button::SELECT, ButtonAction::RELEASED);
    assert(state.showActive && !state.helpOpen && !state.helpFull);
    // START alone still plays and stops outside the view; the view itself is entered only with SELECT.
    state.showActive = false;
    event(Button::START, ButtonAction::PRESSED);
    event(Button::START, ButtonAction::RELEASED);
    assert(!state.showActive);
    host.stop();

    // The drawing: tracks rise with their level, a muted track stays down, the same state repeats.
    std::vector<float> tracks(9 * 620);
    for (int t = 0; t < 8; ++t)
        for (int i = 0; i < 620; ++i) tracks[size_t(t * 620 + i)] = .08f * (t + 1) * std::sin(i * .05f);
    state.showActive = true;
    state.trackWaveforms = tracks.data();
    state.trackMask = 0xFF;
    state.animationMs = 5000;
    TrackerLayout layout;
    auto frame = [&] { Canvas c; layout.draw(c, state); return c; };
    const Canvas loud = frame();
    assert(differing(loud, frame()) == 0);
    state.trackMask = 0;
    assert(differing(loud, frame()) > 5000);
    state.trackMask = 0xFF;
    host.edit_project().tracks[3].mute = true;
    assert(differing(loud, frame()) > 500);
    std::cout << "ptshow-view-check: SELECT+START opens the show view without help, levels and mutes drawn\n";
    return 0;
}
