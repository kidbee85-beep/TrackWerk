// ptmidi-test-check: the MIDI connection check (TrackWerk 1.2, feedback 2026-11-16). TEST sends its
// C-4 on a channel the player picks (A+D-PAD, 01..16, kept in settings.json only when not 01), says
// what happened in words that point at the fix (no device / OUTPUT off / not plugged in / send
// failed / sent on CH nn), leaves the song clean, and the screen shows one line of what the receiver
// needs on OUTPUT, PANIC and TEST. A fake port records the bytes; no hardware is involved.
// usage: ptmidi-test-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/midi_out.h"
#include "ui/button_mapper.h"
#include "ui/helpers.h"
#include "ui/input_dispatcher.h"
#include "ui/modules/midi_settings.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace pt::ui;

namespace {

struct FakeOut : songcore::IMidiOut {
    std::vector<std::vector<uint8_t>> sent;
    bool opened = true, isBroken = false, plugged = true;   // unplugged: a reopen fails, as on a device
    bool listed = true;   // false: the system lists no MIDI device at all
    int device_count() override { return listed ? 1 : 0; }
    std::string device_name(int) override { return "FM1"; }
    bool open(int) override { opened = plugged; return plugged; }
    void close() override { opened = false; }
    bool is_open() const override { return opened; }
    void send(const uint8_t* data, int len) override { sent.emplace_back(data, data + len); }
    bool broken() const override { return isBroken; }
};

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now = 1000;
    ButtonMods held;
    FakeOut out;
    explicit Rig(const std::filesystem::path& dir) : fs((dir / "data").string()), dispatch(state, host, fs) {
        host.new_project();
        state.project = &host.edit_project();
        state.currentScreen = ScreenType::MIDI;
        state.midiOut = &out;
        host.midi_out().set_out(&out);   // the real sender, as the shell wires it (review of a445741)
        state.midiDeviceNames = {"OFF", "AUTO", "FM1"};
        state.settings.midiOutDevice = "FM1";
        state.midiCursorRow = static_cast<int>(MidiRow::TEST);
        state.midiCursorColumn = 1;
    }
    void send(Button b, ButtonAction a) {
        now += 40;
        dispatch.set_now(static_cast<long long>(now));
        const ButtonEvent e{b, a, held};
        if (!studio_button(e, state, host, *engine, fs)) handle_button(e, dispatch, mapper, now);
    }
    void press(Button b) { if (b == Button::A) { held.a = true; now += 400; } send(b, ButtonAction::PRESSED); }
    void release(Button b) { send(b, ButtonAction::RELEASED); if (b == Button::A) held.a = false; }
    void tap(Button b) { press(b); release(b); }
    void wait(int ms) { now += static_cast<uint64_t>(ms); dispatch.set_now(static_cast<long long>(now)); }   // frames pass
};

bool has(const std::vector<std::vector<uint8_t>>& sent, std::vector<uint8_t> msg) {
    for (const auto& m : sent) if (m == msg) return true;
    return false;
}

int differing(const Canvas& a, const Canvas& b, int x, int y, int w, int h) {
    int n = 0;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) n += a.pixels()[yy * DESIGN_W + xx] != b.pixels()[yy * DESIGN_W + xx];
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) {
        std::cerr << "usage: ptmidi-test-check <new run directory>\n";
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    Rig r(dir / "a");
    const int version = r.state.projectVersion;

    // A sends C-4 on channel 01 by default; its note-off follows 250 ms later, not at once.
    r.tap(Button::A);
    assert(r.out.sent.size() == 1 && (r.out.sent[0] == std::vector<uint8_t>{0x90, 60, 100}));
    assert(r.state.midiStatusText == "TEST C-4 CH 01 SENT");
    r.wait(100);
    assert(r.out.sent.size() == 1);                                   // still held
    r.wait(200);
    assert(r.out.sent.size() == 2 && (r.out.sent[1] == std::vector<uint8_t>{0x80, 60, 0}));
    r.wait(500);
    assert(r.out.sent.size() == 2);                                   // exactly one off

    // A+Right picks channel 02; A then sends on it. The song stays clean (a settings value).
    r.press(Button::A);
    r.tap(Button::DPAD_RIGHT);
    r.release(Button::A);
    assert(r.state.settings.midiTestChannel == 1);
    // (The A that began A+Right already sent a test on 01; a new TEST ends it first, so nothing hangs.)
    r.out.sent.clear();
    r.tap(Button::A);
    assert(r.out.sent.size() == 2 && (r.out.sent[0] == std::vector<uint8_t>{0x80, 60, 0}) &&
           (r.out.sent[1] == std::vector<uint8_t>{0x91, 60, 100}));
    assert(r.state.midiStatusText == "TEST C-4 CH 02 SENT");
    assert(r.state.projectVersion == version && !r.state.project_dirty());

    // Review: PANIC right after TEST, with the real sender attached. It must send — the owed note-off
    // of the test, then sustain off and all notes off on all sixteen channels — and say so.
    r.out.sent.clear();
    r.state.midiCursorRow = static_cast<int>(MidiRow::PANIC);
    r.tap(Button::A);
    assert(r.state.midiStatusText == "PANIC SENT");
    assert(has(r.out.sent, {0x81, 60, 0}));                          // the test note ended
    for (int ch : {0, 15}) {
        assert(has(r.out.sent, {static_cast<uint8_t>(0xB0 | ch), 64, 0}));
        assert(has(r.out.sent, {static_cast<uint8_t>(0xB0 | ch), 123, 0}));
    }
    r.out.sent.clear();
    r.wait(600);
    assert(r.out.sent.empty());                                       // no stray off later
    // An empty sender (nothing owed) still sends the 32 resets.
    r.tap(Button::A);
    int ccs = 0;
    for (const auto& m : r.out.sent) ccs += (m[0] & 0xF0) == 0xB0 && (m[1] == 64 || m[1] == 123);
    assert(ccs == 32 && r.state.midiStatusText == "PANIC SENT");
    // Broken port: PANIC must not claim success.
    r.out.isBroken = true;
    r.tap(Button::A);
    assert(r.state.midiStatusText == "SEND FAILED: REPLUG IT");
    r.out.isBroken = false;
    r.state.midiCursorRow = static_cast<int>(MidiRow::TEST);

    // A hotplug that moved the open port elsewhere while the test note sounds: no late off to the new
    // device (the review's "no late off to the wrong port").
    r.state.midiOutOpenName = "FM1";
    r.tap(Button::A);
    r.out.sent.clear();
    r.state.midiOutOpenName = "OTHER DEVICE";
    r.wait(400);
    assert(r.out.sent.empty());
    r.state.midiOutOpenName = "FM1";

    // ── Lifecycle (Codex, MIDI-LEBENSZYKLUS-VORPRUEFUNG and MIDI-STOP-NEUE-NOTE-REPRO): after the test
    // note was ended some other way, its timer must never end a new note on the same key. Each case:
    // TEST on CH 02, the interruption, a new normal C-4 on CH 02 through the same sender, then the
    // timer's due time passes — only the new note-on may reach the port.
    {
        const std::vector<uint8_t> songOn{0x91, 60, 99};
        auto late_off_case = [&](const char* name, auto interrupt, bool interruptEndsNote) {
            assert(r.state.settings.midiTestChannel == 1);
            r.state.currentScreen = ScreenType::MIDI;
            r.state.midiCursorRow = static_cast<int>(MidiRow::TEST);
            r.tap(Button::A);
            assert(r.host.midi_out().test_note_owed());
            r.out.sent.clear();
            interrupt();
            if (interruptEndsNote) assert(has(r.out.sent, {0x81, 60, 0}));   // the test note did end
            assert(!r.host.midi_out().test_note_owed());
            r.out.sent.clear();
            assert(r.host.midi_out().send_direct(songOn.data(), 3));
            r.wait(400);
            if (!(r.out.sent.size() == 1 && r.out.sent[0] == songOn)) {
                std::cerr << "late off after " << name << ": " << r.out.sent.size() << " packets\n";
                assert(false);
            }
            const uint8_t off[3] = {0x81, 60, 0};
            r.host.midi_out().send_direct(off, 3);   // the song's own off; the next case starts silent
        };
        late_off_case("host.stop()", [&] { r.host.stop(); }, true);                 // Codex' repro
        late_off_case("PANIC", [&] {
            r.state.midiCursorRow = static_cast<int>(MidiRow::PANIC);
            r.tap(Button::A);
        }, true);
        late_off_case("play start", [&] { r.host.midi_out().on_play("PHRASE", "", 0, 120, 48000); }, true);
        late_off_case("NEW (host.new_project)", [&] { r.host.new_project(); }, true);
        r.state.project = &r.host.edit_project();
        // The song takes the key without any stop (a live key thru): the test's off is withdrawn.
        late_off_case("a song note on the key", [&] { r.host.midi_out().send_direct(songOn.data(), 3); }, false);
    }

    // cancel_midi_test (background, focus or controller lost): ends the test note at once, exactly once,
    // and leaves a playing song's notes alone — no panic, no CC 123.
    {
        const uint8_t songNote[3] = {0x92, 64, 90};   // a song note on CH 03, sounding
        assert(r.host.midi_out().send_direct(songNote, 3));
        r.tap(Button::A);
        r.out.sent.clear();
        r.dispatch.cancel_midi_test();
        assert(r.out.sent.size() == 1 && (r.out.sent[0] == std::vector<uint8_t>{0x81, 60, 0}));
        r.dispatch.cancel_midi_test();   // idempotent
        r.wait(400);                     // and the timer is gone
        assert(r.out.sent.size() == 1);
        // The song note is still owed by the sender: a later STOP ends it.
        r.out.sent.clear();
        r.host.stop();
        assert(has(r.out.sent, {0x82, 64, 0}));
        // Nothing sounding: cancel sends nothing.
        r.out.sent.clear();
        r.dispatch.cancel_midi_test();
        assert(r.out.sent.empty());
    }

    // Leaving the MIDI screen while the test note sounds ends it on the next frame, not 250 ms later
    // on another screen.
    {
        r.tap(Button::A);
        r.out.sent.clear();
        r.state.currentScreen = ScreenType::SONG;
        r.wait(16);
        assert(r.out.sent.size() == 1 && (r.out.sent[0] == std::vector<uint8_t>{0x81, 60, 0}));
        assert(!r.host.midi_out().test_note_owed());
        r.wait(400);
        assert(r.out.sent.size() == 1);
        r.state.currentScreen = ScreenType::MIDI;
    }

    // A hotplug moves the port while the test note sounds. Whether the timer or the once-a-second scan
    // comes first, the note ends at most once (the old port's close panics it) and the sender owes
    // nothing afterwards: a later cancel, frame or timer sends nothing more.
    {
        r.tap(Button::A);
        r.out.sent.clear();
        r.state.midiOutOpenName = "OTHER DEVICE";
        r.wait(1200);   // past the timer and past a hotplug scan
        assert(r.out.sent.size() <= 1);
        for (const auto& m : r.out.sent) assert((m == std::vector<uint8_t>{0x81, 60, 0}));
        assert(!r.host.midi_out().test_note_owed());
        const size_t n = r.out.sent.size();
        r.state.midiOutOpenName = "FM1";
        r.dispatch.cancel_midi_test();
        r.wait(400);
        int offs = 0;
        for (const auto& m : r.out.sent) offs += (m[0] & 0xF0) == 0x80;
        assert(offs == static_cast<int>(n));
        r.state.midiOutOpenName = "FM1";
    }

    // Channel 16 and a port change while the test note sounds: the change ends it (owed note-off).
    r.state.settings.midiTestChannel = 15;
    r.out.sent.clear();
    r.tap(Button::A);
    assert(has(r.out.sent, {0x9F, 60, 100}));
    r.state.midiCursorRow = static_cast<int>(MidiRow::OUTPUT);
    r.press(Button::A);
    r.tap(Button::DPAD_LEFT);   // OUTPUT: another choice (AUTO) — close, panic, reopen
    r.release(Button::A);
    assert(has(r.out.sent, {0x8F, 60, 0}));
    r.state.midiCursorRow = static_cast<int>(MidiRow::TEST);
    r.state.settings.midiOutDevice = "FM1";

    // A port that broke on the way says so.
    r.out.opened = true;
    r.out.isBroken = true;
    r.tap(Button::A);
    assert(r.state.midiStatusText == "SEND FAILED: REPLUG IT");
    r.out.isBroken = false;

    // No port: the reason in the words of the fix, for TEST and PANIC alike.
    r.out.opened = false;
    r.out.plugged = false;
    r.out.sent.clear();
    r.tap(Button::A);
    assert(r.state.midiStatusText == "OUTPUT NOT CONNECTED" && r.out.sent.empty());
    r.state.settings.midiOutDevice = MIDI_PORT_OFF;
    r.tap(Button::A);
    assert(r.state.midiStatusText == "OUTPUT IS OFF: PICK ONE");
    r.out.listed = false;
    r.state.midiDeviceNames = {"OFF", "AUTO"};
    r.tap(Button::A);
    assert(r.state.midiStatusText == "NO MIDI DEVICE FOUND");
    r.state.midiCursorRow = static_cast<int>(MidiRow::PANIC);
    r.tap(Button::A);
    assert(r.state.midiStatusText == "NO MIDI DEVICE FOUND");

    // The screen: TEST shows its channel, and one hint line appears on TEST that is not there on KEYS.
    {
        SettingsValues settings;
        settings.midiTestChannel = 4;
        const std::vector<std::string> names{"OFF", "AUTO", "FM1"};
        MidiState ms{r.host.project(), settings, names, names};
        ms.theme = theme_default();
        MidiModule m;
        Canvas onTest, onKeys;
        ms.cursorRow = static_cast<int>(MidiRow::TEST);
        m.draw(onTest, 10, 82, ms);
        ms.cursorRow = static_cast<int>(MidiRow::KEYS);
        m.draw(onKeys, 10, 82, ms);
        const int hintY = 82 + TEXT_PADDING + ROW_HEIGHT + 14 + midi_row_offset_y(MidiRow::TEST, ROW_HEIGHT) + ROW_HEIGHT * 3;
        assert(differing(onTest, onKeys, 10, hintY, MidiModule::WIDTH, 16) > 200);
    }

    // Settings: the channel survives save → load; the default writes nothing new.
    {
        Theme theme = theme_default();
        StdFileSystem plainFs((dir / "plain").string());
        assert(save_settings(plainFs, SettingsValues{}, theme));
        std::string json;
        assert(plainFs.read_file(plainFs.settings_path(), json) && json.find("midi_test_channel") == std::string::npos);
        SettingsValues five;
        five.midiTestChannel = 4;
        StdFileSystem fs5((dir / "five").string());
        assert(save_settings(fs5, five, theme));
        SettingsValues back;
        Theme backTheme;
        assert(load_settings(fs5, back, backTheme) && back.midiTestChannel == 4);
    }

    std::cout << "ptmidi-test-check: with the real sender attached, TEST holds C-4 250 ms on the picked channel (01, 02, "
                 "16) and ends once, PANIC after TEST and on an empty sender sends sustain off and all notes off on "
                 "16 channels, a broken port never reports SENT, a port change ends a sounding test note, a hotplug to another port gets no late off, after STOP / PANIC / play start / NEW / a song note on the key no late off ends a new C-4, cancel_midi_test (background, focus) ends only the test note once, leaving the MIDI screen ends it, the song stays "
                 "clean, SEND FAILED / NOT CONNECTED / OUTPUT OFF / NO DEVICE say what to fix (TEST and PANIC), the "
                 "screen shows the channel and a receiver hint, midi_test_channel round-trips and stays out of a "
                 "default file\n";
    return 0;
}
