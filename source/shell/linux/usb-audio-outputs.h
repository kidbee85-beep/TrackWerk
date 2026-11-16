#pragma once

// SETTINGS > AUDIO OUT on Linux handhelds and desktops that play through ALSA (TrackWerk 1.2, USB-C
// audio, requested by Kid Bunka 16 Nov 2026). Without SDL, so the host checks can drive it:
//   0  AUTO (USB when plugged)  a USB audio card that can play, else the built-in route — like a phone
//   1  BUILT-IN                 the route the app always used (ALSA default / the chip, alsa-route.h)
//   2… USB: <card name>         that USB card (by product + socket, not by its name), while plugged in
// A USB card is opened as plughw:CARD=<id>,DEV=0 (ALSA converts rate and format). Recording needs
// nothing here: SDL lists a USB card's capture side as an input by itself.
//
// Codex' review of 9273001: (1) a USB open is strict and checked against the route that really
// opened — a failed USB card never reads as playing; (3) a choice is the card's key, so two cards of
// the same name and a replug stay apart / the same.

#include "usb-audio-diagnosis.h"   // same folder

#include <string>
#include <vector>

namespace ptshell {

inline constexpr const char* kAudioOutAuto = "AUTO (USB when plugged)";
inline constexpr const char* kAudioOutBuiltIn = "BUILT-IN";

// What the switch drives (SdlAudioEngine on a device, a fake in the checks).
class OutputRoute {
  public:
    virtual ~OutputRoute() = default;
    // Open exactly `device` ("" = the built-in route). For a USB device: no quiet fallback elsewhere.
    virtual bool open_route(const std::string& device) = 0;
    virtual void close_route() = 0;
    // The route that really is open now ("" = the built-in one).
    virtual std::string opened_route() const = 0;
};

class AudioOutputSwitch {
  public:
    struct Entry { std::string name, key, device; };   // key "" for AUTO and BUILT-IN

    explicit AudioOutputSwitch(OutputRoute& route) : route_(route) {}

    const std::vector<std::string>& names() const { return names_; }
    int active() const { return playing_; }
    const std::string& choice_key() const { return choiceKey_; }

    // New card list; true when the output must be reopened (the choice now lands elsewhere).
    bool set_cards(const std::vector<AlsaCard>& cards) {
        entries_ = {{kAudioOutAuto, "", ""}, {kAudioOutBuiltIn, "", ""}};
        for (const auto& c : cards) {
            if (!c.usb || !c.playback) continue;
            std::string name = "USB: " + c.name;
            int same = 1;
            for (const auto& e : entries_) if (e.name == name || e.name.rfind(name + " (", 0) == 0) ++same;
            if (same > 1) name += " (" + std::to_string(same) + ")";
            entries_.push_back({name, c.key(), alsa_card_device(c)});
        }
        names_.clear();
        for (const auto& e : entries_) names_.push_back(e.name);
        // A USB route that failed is not tried again on every poll (each try closes the sound that
        // plays): only when the cards really change, or on an explicit select (Codex, P1).
        std::string topology;
        for (const auto& e : entries_) topology += e.key + "=" + e.device + ";";
        if (topology != topology_) failedTarget_.clear();
        topology_ = std::move(topology);
        // What plays now, in the new list, before any reopen (Codex P1): the loop reads names()[active()]
        // right after this. A card that left shows the fallback row until the reopen lands.
        playing_ = row_of(openDevice_);
        const auto t = target();
        return t.device != openDevice_ && (t.device.empty() || t.device != failedTarget_);
    }

    // Open the output for the choice. A USB route that fails, or opens as something else, is closed
    // again and the built-in route opens instead; `active()` then says BUILT-IN (or AUTO), never USB.
    bool open() {
        const auto t = target();
        if (!t.device.empty()) {
            if (route_.open_route(t.device) && route_.opened_route() == t.device) {
                openDevice_ = t.device;
                playing_ = t.index;
                failedTarget_.clear();
                return true;
            }
            route_.close_route();
            failedTarget_ = t.device;   // latched until the cards change or a select retries
        }
        openDevice_.clear();
        playing_ = choice_ == 0 ? 0 : 1;
        return route_.open_route("");
    }

    // Choose a row. False (and the previous choice playing again) when it would not really play.
    bool select(int index, std::string& error) {
        if (index < 0 || index >= static_cast<int>(entries_.size())) { error = "no such output"; return false; }
        const int previous = choice_;
        const std::string previousKey = choiceKey_;
        choice_ = index;
        choiceKey_ = entries_[size_t(index)].key;
        failedTarget_.clear();   // an explicit choice is an explicit retry
        const auto t = target();
        if (t.device == openDevice_ && !openDevice_.empty()) { playing_ = t.index; return true; }
        route_.close_route();
        const bool opened = open();
        // AUTO is kept when it lands on the built-in route (that is what AUTO means without a working
        // USB card); a chosen USB card or BUILT-IN must play exactly there.
        if (opened && (choice_ == 0 || openDevice_ == t.device)) return true;
        error = t.device.empty() ? "the built-in output would not open"
                                 : "the USB output would not open (" + t.device + ")";
        route_.close_route();
        choice_ = previous;
        choiceKey_ = previousKey;
        if (!open()) error += "; the previous output neither";
        return false;
    }

  private:
    struct Target { std::string device; int index = 1; };
    // The row for an open device; always inside entries_.
    int row_of(const std::string& device) const {
        if (device.empty()) return choice_ == 0 ? 0 : 1;
        for (size_t i = 2; i < entries_.size(); ++i)
            if (entries_[i].device == device) return choice_ == 0 ? 0 : static_cast<int>(i);
        return choice_ == 0 ? 0 : 1;   // gone: AUTO stays AUTO, a chosen card shows BUILT-IN
    }
    // Where the choice plays with the cards present now.
    Target target() const {
        if (choice_ == 0) {
            if (entries_.size() > 2) return {entries_[2].device, 0};
            return {"", 0};
        }
        if (choice_ >= 2)
            for (size_t i = 2; i < entries_.size(); ++i)
                if (entries_[i].key == choiceKey_) return {entries_[i].device, int(i)};
        return {"", 1};
    }

    OutputRoute&             route_;
    std::vector<Entry>       entries_{{kAudioOutAuto, "", ""}, {kAudioOutBuiltIn, "", ""}};
    std::vector<std::string> names_{kAudioOutAuto, kAudioOutBuiltIn};
    int                      choice_ = 0;
    std::string              choiceKey_;
    int                      playing_ = 0;
    std::string              openDevice_;   // "" = built-in
    std::string              topology_;     // the cards as last seen (keys and devices)
    std::string              failedTarget_; // a USB route that would not open, for that topology
};

}  // namespace ptshell
