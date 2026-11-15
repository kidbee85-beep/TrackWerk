// ptbutton-conflicts-check: the button conflicts from the double-assignment audit (2026-11-14), each
// through the normal path (studio_button, then handle_button):
//   1 A+B / R+B in Studio no longer leave it      2 Punch R + D-pad moves without a bank change
//   3 Drumlab L/R as a modifier turn no bar       4 Sources L as a modifier does not re-sort
//   5 Harmony L locks on its release, alone only  6 R + SELECT leaves Studio from the Sources browser
//   8 tracker: B then A is the delete chord like A then B; A waiting then R is R + A
// usage: ptbutton-conflicts-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/punch.h"
#include "studio/rack.h"
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/source_sampler.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>

using namespace pt::ui;

namespace {

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now = 1000;
    explicit Rig(const std::filesystem::path& dir) : fs((dir / "data").string()), dispatch(state, host, fs) {
        auto project = songcore::make_default_project();
        project.tracks[0].chainRefs = {0, 1};
        project.chains[0].phraseRefs[0] = 0;
        project.chains[1].phraseRefs[0] = 1;
        assert(host.push_project(songcore::serialize_project(project)));
        state.project = &host.edit_project();
        state.currentScreen = ScreenType::SONG;
    }
    ButtonMods held;   // what is down right now
    void set(Button b, bool down) {
        switch (b) {
            case Button::A: held.a = down; break;
            case Button::B: held.b = down; break;
            case Button::L_SHIFT: held.l = down; break;
            case Button::R_SHIFT: held.r = down; break;
            case Button::SELECT: held.select = down; break;
            default: break;
        }
    }
    void press(Button b) { set(b, true); send(b, ButtonAction::PRESSED); }
    void release(Button b) { send(b, ButtonAction::RELEASED); set(b, false); }
    void send(Button b, ButtonAction a) {
        now += 40;
        dispatch.set_now(static_cast<long long>(now));
        const ButtonEvent e{b, a, held};
        if (!studio_button(e, state, host, *engine, fs)) handle_button(e, dispatch, mapper, now);
    }
    void open_studio(int tab) {
        state.studioOpen = true;
        state.studioTab = tab;
        state.studioRow = 0;
        state.studioBUsed = state.studioAUsed = false;
    }
};

struct RecordingBackend : SourceBackend {
    std::vector<std::string> commands;
    bool command(const std::string& action, const std::string&) override { commands.push_back(action); return true; }
    std::string state_json() override { return R"({"status":"idle"})"; }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) {
        std::cerr << "usage: ptbutton-conflicts-check <new run directory>\n";
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);

    // 1 · A + B and R + B in Studio: the page stays open, in either order.
    {
        Rig r(dir / "1");
        r.open_studio(1);
        r.press(Button::A); r.press(Button::B); r.release(Button::B); r.release(Button::A);
        assert(r.state.studioOpen);
        r.press(Button::B); r.press(Button::A); r.release(Button::A); r.release(Button::B);
        assert(r.state.studioOpen);
        r.press(Button::R_SHIFT); r.press(Button::B); r.release(Button::B); r.release(Button::R_SHIFT);
        assert(r.state.studioOpen);
        r.press(Button::B); r.release(Button::B);   // B alone still leaves
        assert(!r.state.studioOpen);
    }
    // 2 · Punch: R + Up moves the pad; R's release is no bank change.
    {
        Rig r(dir / "2");
        r.open_studio(3);
        r.state.studioPunch = studio::punch_id(0, 4);
        r.press(Button::R_SHIFT); r.press(Button::DPAD_UP); r.release(Button::DPAD_UP); r.release(Button::R_SHIFT);
        assert(studio::punch_bank(r.state.studioPunch) == 0 && studio::punch_pad(r.state.studioPunch) == 0);
        r.press(Button::R_SHIFT); r.release(Button::R_SHIFT);   // alone: next bank
        assert(studio::punch_bank(r.state.studioPunch) == 1);
    }
    // 3 · Drumlab: R as a modifier of the D-pad turns no bar; alone it does.
    {
        Rig r(dir / "3");
        r.open_studio(5);
        const int page = r.state.drumLab.gridPage;
        r.press(Button::R_SHIFT); r.press(Button::DPAD_DOWN); r.release(Button::DPAD_DOWN); r.release(Button::R_SHIFT);
        assert(r.state.drumLab.gridPage == page);
        r.press(Button::R_SHIFT); r.release(Button::R_SHIFT);
        assert(r.state.drumLab.gridPage != page);
    }
    // 4 · Sources: L after a combo sends no sort; L alone does.
    {
        Rig r(dir / "4");
        RecordingBackend backend;
        r.state.sourceSampler.backend = &backend;
        r.open_studio(7);
        r.press(Button::L_SHIFT); r.press(Button::DPAD_DOWN); r.release(Button::DPAD_DOWN); r.release(Button::L_SHIFT);
        assert(std::count(backend.commands.begin(), backend.commands.end(), "sort") == 0);
        r.press(Button::L_SHIFT); r.release(Button::L_SHIFT);
        assert(std::count(backend.commands.begin(), backend.commands.end(), "sort") == 1);
        r.state.sourceSampler.backend = nullptr;
    }
    // 5 · Harmony: on the slot row L locks on its release, alone only.
    {
        Rig r(dir / "5");
        r.open_studio(8);
        r.state.harmony.cursor = 5;
        r.press(Button::L_SHIFT);
        assert(!r.state.harmony.slots[0].locked);   // not on the press
        r.release(Button::L_SHIFT);
        assert(r.state.harmony.slots[0].locked);
        r.press(Button::L_SHIFT); r.press(Button::R_SHIFT); r.release(Button::R_SHIFT); r.release(Button::L_SHIFT);
        assert(r.state.harmony.slots[0].locked);    // a shoulder combo: unchanged
    }
    // 6 · Sources browser: R + SELECT leaves Studio as on every page.
    {
        Rig r(dir / "6");
        r.open_studio(7);
        r.state.sourceSampler.collectionOpen = true;
        r.press(Button::R_SHIFT); r.press(Button::SELECT); r.release(Button::SELECT); r.release(Button::R_SHIFT);
        assert(!r.state.studioOpen);
    }
    // 8 · SONG: B then A deletes like A then B; neither order is a plain A.
    {
        Rig first(dir / "8a"), second(dir / "8b");
        for (Rig* r : {&first, &second}) { r->state.cursorRow = 1; r->state.cursorColumn = 1; }
        first.press(Button::A); first.press(Button::B); first.release(Button::B); first.release(Button::A);
        second.press(Button::B); second.press(Button::A); second.release(Button::A); second.release(Button::B);
        const auto a = songcore::serialize_project(first.host.project());
        const auto b = songcore::serialize_project(second.host.project());
        assert(a == b);
        assert(first.host.project().tracks[0].chainRefs.size() < 2 || first.host.project().tracks[0].chainRefs[1] < 0);
    }
    // 10/11 · A + Up / Down is the coarse step: FX values ±16, Synth target slot ±16.
    {
        Rig r(dir / "10");
        r.open_studio(1);
        r.state.studioRow = 2;   // FX MIX, 0…255
        const int before = r.host.project().studioMaster[studio::FX_MIX];
        r.press(Button::A); r.press(Button::DPAD_UP); r.release(Button::DPAD_UP);
        r.press(Button::DPAD_RIGHT); r.release(Button::DPAD_RIGHT); r.release(Button::A);
        assert(r.host.project().studioMaster[studio::FX_MIX] == std::min(255, before + 17));
        open_studio_synth(r.state, r.host.project().instruments[0]);
        r.state.studioOpen = true; r.state.studioTab = 0; r.state.studioRow = 0; r.state.currentInstrument = 0;
        r.press(Button::A); r.press(Button::DPAD_UP); r.release(Button::DPAD_UP); r.release(Button::A);
        assert(r.state.currentInstrument == 16);
    }
    std::cout << "ptbutton-conflicts-check: Studio A+B / R+B, Punch R+D-pad, Drumlab and Sources shoulders, "
                 "Harmony lock on release, browser R+SELECT, SONG B-then-A, coarse A + Up / Down all as the audit asks\n";
    return 0;
}
