// ptusb-audio-diagnosis-check: why a plugged-in USB audio interface is not an input (requested by Kid
// Bunka, 16 Nov 2026). On the Linux handhelds the app lists what ALSA lists; a USB interface appears
// only if the firmware has USB host mode and the USB audio driver. The diagnosis reads sysfs and
// /proc/asound (faked here in a directory tree) and says so on the Record page — nothing is opened.
// Checked: nothing plugged in, a streaming interface without driver (said), with driver (silent),
// MIDI-only and HID devices (not audio), a built-in codec card beside an undriven USB device, no /sys
// at all; and the Record page shows the sentence for the selected input. Native image.
// And SETTINGS > AUDIO OUT on Linux (usb-audio-outputs.h): AUTO plays on a USB card while one is
// plugged in, BUILT-IN never does, a chosen USB card is found by name when others come and go, a
// card that leaves falls back to the built-in output, a USB microphone is no output.
// usage: ptusb-audio-diagnosis-check <new run directory>
#include "../../shell/linux/usb-audio-outputs.h"
#include <set>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/layout.h"
#include "ui/studio.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace pt::ui;
namespace fsys = std::filesystem;

namespace {

void put(const fsys::path& p, const std::string& text) {
    fsys::create_directories(p.parent_path());
    std::ofstream(p) << text << "\n";
}
void interface(const fsys::path& usb, const std::string& name, const char* cls, const char* sub) {
    put(usb / name / "bInterfaceClass", cls);
    put(usb / name / "bInterfaceSubClass", sub);
}
std::string diagnose(const fsys::path& root) {
    return ptshell::usb_audio_diagnosis(ptshell::usb_audio_seen((root / "sys").string(), (root / "asound").string()));
}

struct FakeCapture : CaptureBackend {
    std::string inputs;
    std::string inputs_json() override { return inputs; }
    bool start(int, int, int) override { return false; }
    void stop() override {}
    std::string state_json() override { return R"({"state":"idle"})"; }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || fsys::exists(argv[1])) {
        std::cerr << "usage: ptusb-audio-diagnosis-check <new run directory>\n";
        return 2;
    }
    const fsys::path dir = fsys::absolute(argv[1]);
    fsys::create_directories(dir);

    // No /sys, no /proc/asound (macOS, Windows, a sandbox): nothing to say.
    assert(diagnose(dir / "none").empty());

    // Only the handheld's own devices: a hub, a HID pad, the built-in codec card.
    const fsys::path a = dir / "a";
    interface(a / "sys", "1-0:1.0", "09", "00");
    interface(a / "sys", "1-1:1.0", "03", "00");
    put(a / "asound" / "card0" / "id", "audiocodec");
    assert(diagnose(a).empty());

    // A USB audio interface plugged in (control + two streaming interfaces), no USB card: said.
    const fsys::path b = dir / "b";
    interface(b / "sys", "1-1:1.0", "01", "01");
    interface(b / "sys", "1-1:1.1", "01", "02");
    interface(b / "sys", "1-1:1.2", "01", "02");
    put(b / "asound" / "card0" / "id", "audiocodec");   // the built-in codec does not count
    const auto said = diagnose(b);
    // Only what is known: a streaming interface on the bus, no USB card. The driver is one possible
    // cause among others (power, port, not class compliant) — the sentence says so (Codex, 9273001).
    assert(said.find("no USB sound card exists") != std::string::npos && said.find("snd-usb-audio") != std::string::npos &&
           said.find("Possible") != std::string::npos && said.find("power") != std::string::npos);
    {
        const auto seen = ptshell::usb_audio_seen((b / "sys").string(), (b / "asound").string());
        assert(seen.streamingInterfaces == 2 && seen.usbCards == 0);
    }

    // The same with the driver: the card exists (usbid), SDL lists it, nothing to explain.
    const fsys::path c = dir / "c";
    interface(c / "sys", "1-1:1.1", "01", "02");
    put(c / "asound" / "card1" / "usbid", "1397:0507");
    assert(diagnose(c).empty());

    // A USB MIDI keyboard (audio class, MIDI streaming subclass) is no audio input.
    const fsys::path d = dir / "d";
    interface(d / "sys", "1-1:1.0", "01", "01");
    interface(d / "sys", "1-1:1.1", "01", "03");
    assert(diagnose(d).empty());

    // ── AUDIO OUT (Codex' review of 9273001): strict USB routes, identity by product + socket ──
    {
        struct FakeRoute : ptshell::OutputRoute {
            std::set<std::string> broken;        // these USB routes do not open…
            bool fallsBack = false;              // …or "open" by quietly playing the default instead
            std::string open;                    // what really plays ("" built-in, "-" nothing)
            std::vector<std::string> log;
            bool open_route(const std::string& device) override {
                log.push_back(device.empty() ? "builtin" : device);
                if (!device.empty() && broken.count(device)) {
                    if (fallsBack) { open = ""; return true; }   // the old SdlAudioEngine behaviour
                    open = "-";
                    return false;
                }
                open = device;
                return true;
            }
            void close_route() override { open = "-"; }
            std::string opened_route() const override { return open == "-" ? "" : open; }
        };
        const fsys::path e = dir / "e";
        auto card = [&](int n, const std::string& id, const std::string& usbid, const std::string& port, bool play, bool rec) {
            const auto c = e / "asound" / ("card" + std::to_string(n));
            fsys::remove_all(c);
            put(c / "id", id);
            if (!usbid.empty()) put(c / "usbid", usbid);
            if (play) put(c / "pcm0p" / "info", "x");
            if (rec) put(c / "pcm0c" / "info", "x");
            const auto link = e / "class" / ("card" + std::to_string(n));
            fsys::remove_all(link);
            if (!port.empty()) {
                fsys::create_directories(e / "devices" / port);
                fsys::create_directories(link);
                fsys::create_directory_symlink(e / "devices" / port, link / "device");
            }
        };
        auto cards = [&] { return ptshell::alsa_cards((e / "asound").string(), (e / "class").string()); };
        card(0, "audiocodec", "", "", true, true);
        card(1, "Codec", "08bb:2902", "1-1:1.0", true, true);
        put(e / "asound" / "cards", " 0 [audiocodec     ]: audiocodec - audiocodec\n"
                                    " 1 [Codec          ]: USB-Audio - USB Audio CODEC\n");
        auto list = cards();
        assert(list.size() == 2 && list[1].usb && list[1].usbid == "08bb:2902" && list[1].port == "1-1:1.0");
        assert(list[1].key() == "08bb:2902@1-1:1.0" && list[1].name == "USB Audio CODEC");

        FakeRoute route;
        ptshell::AudioOutputSwitch out(route);
        assert(out.set_cards(list));                       // AUTO with a USB card: a reopen is owed
        assert(out.open() && route.open == "plughw:CARD=Codec,DEV=0" && out.active() == 0);
        assert((out.names() == std::vector<std::string>{ptshell::kAudioOutAuto, ptshell::kAudioOutBuiltIn, "USB: USB Audio CODEC"}));
        std::string error;
        assert(out.select(1, error) && route.open.empty() && out.active() == 1);   // BUILT-IN
        assert(out.select(2, error) && route.open == "plughw:CARD=Codec,DEV=0" && out.active() == 2);

        // (1) The USB card will not open, and the old engine would quietly play the default device:
        // the switch sees the route that really opened, says no, and never shows USB as playing.
        route.broken = {"plughw:CARD=Codec,DEV=0"};
        for (const bool fallsBack : {true, false}) {
            route.fallsBack = fallsBack;
            assert(out.select(1, error));
            error.clear();
            assert(!out.select(2, error) && error.find("USB output would not open") != std::string::npos);
            assert(out.active() == 1 && route.open.empty());   // back on BUILT-IN, as before
            assert(out.select(0, error) && out.active() == 0 && route.open.empty());   // AUTO: built-in, honestly
        }
        // Codex P1: after a USB failure AUTO plays built-in; unchanged polls must leave that sound
        // alone — no close, no reopen — until the cards change or a select retries.
        route.fallsBack = false;
        route.log.clear();
        for (int poll = 0; poll < 6; ++poll) assert(!out.set_cards(cards()));
        assert(route.log.empty() && route.open.empty() && out.active() == 0);
        // A real change (another card) lifts the latch: one retry is owed.
        card(4, "Other", "0d8c:0014", "1-4:1.0", true, false);
        assert(out.set_cards(cards()));
        fsys::remove_all(e / "asound" / "card4");
        fsys::remove_all(e / "class" / "card4");
        out.set_cards(cards());
        assert(out.open() && route.open.empty());          // still broken: built-in, latched again
        route.log.clear();
        for (int poll = 0; poll < 3; ++poll) assert(!out.set_cards(cards()));
        assert(route.log.empty());
        // An explicit select retries at once; now the card works.
        route.broken.clear();
        assert(out.select(0, error) && route.open == "plughw:CARD=Codec,DEV=0" && out.active() == 0);

        // (3) Two identical interfaces in two sockets: two rows, each its own card.
        card(2, "Codec_1", "08bb:2902", "1-2:1.0", true, true);
        put(e / "asound" / "cards", " 0 [audiocodec     ]: audiocodec - audiocodec\n"
                                    " 1 [Codec          ]: USB-Audio - USB Audio CODEC\n"
                                    " 2 [Codec_1        ]: USB-Audio - USB Audio CODEC\n");
        out.set_cards(cards());
        assert(out.names().size() == 4 && out.names()[3] == "USB: USB Audio CODEC (2)");
        assert(out.select(3, error) && route.open == "plughw:CARD=Codec_1,DEV=0" && out.choice_key() == "08bb:2902@1-2:1.0");
        // Both replugged the other way round: ALSA hands out the ids anew. The choice follows the socket.
        card(1, "Codec", "08bb:2902", "1-2:1.0", true, true);
        card(2, "Codec_1", "08bb:2902", "1-1:1.0", true, true);
        assert(out.set_cards(cards()));                   // the same socket is now card "Codec"
        assert(out.open() && route.open == "plughw:CARD=Codec,DEV=0" && out.active() == 2);

        // Unplugged: the chosen card is gone → built-in; it comes back in its socket → it plays again.
        // Codex P1: between the poll and the reopen the loop reads names()[active()] — it must be a row
        // of the new, shorter list (here the chosen card was row 3 of 4; the list now has 3 rows).
        auto valid = [&] { return out.active() >= 0 && out.active() < static_cast<int>(out.names().size()); };
        assert(out.select(3, error) && out.active() == 3 && out.names().size() == 4);
        fsys::remove_all(e / "asound" / "card2");
        fsys::remove_all(e / "class" / "card2");
        const bool owed = out.set_cards(cards());
        assert(valid() && out.names().size() == 3);       // the sync's read before any reopen
        assert(owed && out.open() && route.open.empty() && out.active() == 1 && valid());
        card(2, "Codec_1", "08bb:2902", "1-1:1.0", true, true);
        out.set_cards(cards());
        assert(out.select(2, error) && valid());
        fsys::remove_all(e / "asound" / "card1");
        fsys::remove_all(e / "class" / "card1");
        assert(out.set_cards(cards()) && valid() && out.open() && route.open.empty() && out.active() == 1);
        card(1, "Codec", "08bb:2902", "1-2:1.0", true, true);
        assert(out.set_cards(cards()) && out.open() && route.open == "plughw:CARD=Codec,DEV=0");

        // Codex: two identical products without a readable socket must not share one identity.
        {
            const fsys::path g = dir / "g";
            for (const auto& [n, id] : {std::pair{1, std::string("Codec")}, std::pair{2, std::string("Codec_1")}}) {
                put(g / "asound" / ("card" + std::to_string(n)) / "id", id);
                put(g / "asound" / ("card" + std::to_string(n)) / "usbid", "08bb:2902");
                put(g / "asound" / ("card" + std::to_string(n)) / "pcm0p" / "info", "x");
            }
            const auto twins = ptshell::alsa_cards((g / "asound").string(), (g / "no-class").string());
            assert(twins.size() == 2 && twins[0].port == "?" && twins[1].port == "?");
            assert(twins[0].key() == "08bb:2902@alsa:Codec" && twins[1].key() == "08bb:2902@alsa:Codec_1");
            FakeRoute twinRoute;
            ptshell::AudioOutputSwitch twin(twinRoute);
            twin.set_cards(twins);
            std::string err;
            assert(twin.select(3, err) && twinRoute.open == "plughw:CARD=Codec_1,DEV=0" && twin.active() == 3);
            assert(twin.select(2, err) && twinRoute.open == "plughw:CARD=Codec,DEV=0" && twin.active() == 2);
        }

        // A USB microphone (capture only) is no output.
        card(3, "Mic", "17a0:0310", "1-3:1.0", false, true);
        const size_t rows = out.names().size();
        out.set_cards(cards());
        assert(out.names().size() == rows);
    }

    // The Record page: the backend puts the sentence into the rows' detail; the page shows it for the
    // selected input.
    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host(engine.get(), 48000);
    assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
    FakeCapture backend;
    songcore::json rows = songcore::json::array();
    rows.push_back({{"id", 0}, {"name", "System default (check on Record)"}, {"kind", "default"}, {"detail", said}});
    rows.push_back({{"id", 1}, {"name", "Built-in microphone"}, {"kind", "mic"}, {"detail", said}});
    backend.inputs = rows.dump();
    AppState s;
    s.project = &host.edit_project();
    s.theme = theme_default();
    s.captureBackend = &backend;
    s.studioOpen = true;
    s.studioTab = 4;
    studio_capture_refresh(s);
    bool shown = false;
    for (const auto& in : s.capture.inputs) shown = shown || in.detail == said;
    assert(shown);
    for (size_t i = 0; i < s.capture.inputs.size(); ++i)
        if (s.capture.inputs[i].kind == "mic") s.capture.selected = int(i);
    Canvas canvas;
    TrackerLayout layout;
    layout.draw(canvas, s);
    std::ofstream out(dir / "record-usb-without-driver.ppm", std::ios::binary);
    out << "P6\n640 480\n255\n";
    for (int i = 0; i < 640 * 480; ++i) {
        const auto p = canvas.pixels()[i];
        const char rgb[] = {char(p >> 16), char(p >> 8), char(p)};
        out.write(rgb, 3);
    }

    std::cout << "ptusb-audio-diagnosis-check: an undriven USB audio interface is named on the Record page; with "
                 "the driver, MIDI-only, HID, the built-in codec or no /sys there is nothing to say, and the sentence names "
                 "the possible causes; AUDIO OUT: a USB route that fails or falls back to the default never reads as "
                 "USB (select refuses, previous kept), unchanged polls after a failure never reopen, identical cards stay apart by socket, a replug with swapped ALSA "
                 "ids follows the socket, twins without a socket stay apart by ALSA id, unplug falls back and replug returns, a USB mic is no output; native image\n";
    return 0;
}
