#pragma once

// Why a plugged-in USB audio interface does not show up as an input (TrackWerk 1.2, requested by Kid
// Bunka, 16 Nov 2026). The app records from whatever input the system lists (SDL / ALSA); whether a
// USB interface is listed depends on the handheld's firmware: USB host mode plus the kernel's USB
// audio class driver (snd-usb-audio). This reads two places only, never opens a device:
//   /sys/bus/usb/devices/*/bInterfaceClass + bInterfaceSubClass  — an audio streaming interface
//                                                                 (class 01, subclass 02) is plugged in
//   /proc/asound/card*/usbid                                      — a sound card the USB driver made
// MIDI-only devices (class 01, subclass 03) are not audio inputs and are not counted.
// Off Linux (no /sys), or with nothing plugged in, the answer is empty.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace ptshell {

struct UsbAudioSeen {
    int streamingInterfaces = 0;   // USB audio streaming interfaces present on the bus
    int usbCards = 0;              // ALSA cards created by the USB audio driver
};

inline UsbAudioSeen usb_audio_seen(const std::string& usbDevices = "/sys/bus/usb/devices",
                                   const std::string& asound = "/proc/asound") {
    namespace fs = std::filesystem;
    UsbAudioSeen seen;
    auto read_hex = [](const fs::path& p) {
        std::ifstream f(p);
        std::string s;
        f >> s;
        return s;
    };
    std::error_code error;
    for (fs::directory_iterator it(usbDevices, error), end; !error && it != end; it.increment(error)) {
        const auto dir = it->path();
        if (read_hex(dir / "bInterfaceClass") == "01" && read_hex(dir / "bInterfaceSubClass") == "02")
            ++seen.streamingInterfaces;
    }
    error.clear();
    for (fs::directory_iterator it(asound, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name.rfind("card", 0) == 0 && fs::exists(it->path() / "usbid")) ++seen.usbCards;
    }
    return seen;
}

// The sentence the Record page shows, or "" when there is nothing to explain. What is known is only
// that the bus has an audio streaming interface and ALSA has no USB card: the driver may be missing,
// may still be binding, or may have refused the device (power, a non-compliant interface). Said so.
inline std::string usb_audio_diagnosis(const UsbAudioSeen& seen) {
    if (seen.streamingInterfaces > 0 && seen.usbCards == 0)
        return "A USB audio device is on the bus, but no USB sound card exists for it. Possible: "
               "this firmware lacks the USB audio driver (snd-usb-audio), the device needs more "
               "power (powered hub) or another port, or it is not class compliant.";
    return {};
}

// The sound cards ALSA has, from /proc/asound (no device is opened). `usb`: made by the USB audio
// driver (a usbid file); `playback` / `capture`: it has PCM device 0 in that direction.
struct AlsaCard {
    int index = -1;
    std::string id, name;
    bool usb = false, playback = false, capture = false;
    std::string usbid;   // "vendor:product" (USB cards)
    std::string port;    // USB interface path, e.g. "1-1.2:1.0" — the socket it is plugged into
    // Who it is across replugs and renames: product + socket for USB (two identical interfaces in two
    // sockets stay apart; the same one replugged in the same socket is the same), else the ALSA id.
    // Without a readable socket (no sysfs link) two identical products would share "usbid@?": then the
    // ALSA id ("Codec", "Codec_1") tells them apart — unique now, though a replug may swap it (Codex).
    std::string key() const {
        if (!usb) return "card:" + id;
        if (port.empty() || port == "?") return usbid + "@alsa:" + id;
        return usbid + "@" + port;
    }
};

inline std::vector<AlsaCard> alsa_cards(const std::string& asound = "/proc/asound",
                                        const std::string& soundClass = "/sys/class/sound") {
    namespace fs = std::filesystem;
    std::vector<AlsaCard> cards;
    std::error_code error;
    for (fs::directory_iterator it(asound, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name.rfind("card", 0) != 0 || name.size() < 5) continue;
        AlsaCard card;
        try { card.index = std::stoi(name.substr(4)); } catch (...) { continue; }
        std::ifstream idFile(it->path() / "id");
        std::getline(idFile, card.id);
        if (card.id.empty()) continue;
        card.usb = fs::exists(it->path() / "usbid");
        if (card.usb) {
            std::ifstream usbidFile(it->path() / "usbid");
            std::getline(usbidFile, card.usbid);
            // /sys/class/sound/cardN/device → …/1-1.2:1.0 (the interface); its name is the socket path.
            std::error_code linkError;
            const auto device = fs::canonical(fs::path(soundClass) / name / "device", linkError);
            card.port = linkError ? std::string("?") : device.filename().string();
        }
        card.playback = fs::exists(it->path() / "pcm0p");
        card.capture = fs::exists(it->path() / "pcm0c");
        card.name = card.id;
        cards.push_back(std::move(card));
    }
    // /proc/asound/cards: " 1 [Codec          ]: USB-Audio - USB Audio CODEC" — the long name.
    std::ifstream list(fs::path(asound) / "cards");
    for (std::string line; std::getline(list, line);) {
        const auto dash = line.find(" - ");
        int index = -1;
        try { index = std::stoi(line); } catch (...) { continue; }
        if (dash == std::string::npos) continue;
        for (auto& card : cards)
            if (card.index == index) card.name = line.substr(dash + 3);
    }
    std::sort(cards.begin(), cards.end(), [](const AlsaCard& a, const AlsaCard& b) { return a.index < b.index; });
    return cards;
}

// The ALSA name SDL opens for a card: plughw converts rate and sample format, which a USB DAC may
// not share with the engine (24-bit packed, 44.1 kHz only, …).
inline std::string alsa_card_device(const AlsaCard& card) { return "plughw:CARD=" + card.id + ",DEV=0"; }

}  // namespace ptshell
