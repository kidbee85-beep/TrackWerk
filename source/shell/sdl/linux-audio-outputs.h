// SETTINGS > AUDIO OUT on Linux with SDL's ALSA driver: the built-in route or a USB audio card
// (USB-C DAC, headphones, an interface), chosen or followed automatically. The choice itself is
// AudioOutputSwitch (shell/linux/usb-audio-outputs.h, host-tested); this is its SDL route and the
// AudioBackend the loop drives, like WindowsAudioOutputs. main.cpp creates it only when SDL plays
// through ALSA. An empty TU elsewhere.
#ifndef POCKETTRACKER_LINUX_AUDIO_OUTPUTS_H
#define POCKETTRACKER_LINUX_AUDIO_OUTPUTS_H

#if defined(__linux__) && !defined(__ANDROID__)

#include "app.h"
#include "audio-backend.h"
#include "linux/usb-audio-outputs.h"

#include <cstdint>
#include <string>
#include <vector>

class SdlAudioEngine;

namespace ptshell {

class LinuxAudioOutputs : public AudioBackend, public AudioOutputSelector, private OutputRoute {
  public:
    // `system` must already be open (at the built-in route). Starts on AUTO: a USB card plugged in
    // at boot plays at once (through the loop's reopen).
    explicit LinuxAudioOutputs(SdlAudioEngine& system, std::string asoundRoot = "/proc/asound",
                               std::string soundClassRoot = "/sys/class/sound");

    const std::vector<std::string>& names() const override { return switch_.names(); }
    int  active() const override { return switch_.active(); }
    bool select(int index, std::string& error) override { return switch_.select(index, error); }
    // Re-reads the cards every 2 s; a change that moves the choice asks the loop for a reopen.
    void poll(uint64_t nowMs) override;

    bool openStream() override { rerouteOwed_ = false; return switch_.open(); }
    void closeStream() override;
    void resumeStream() override;
    void setPaused(bool paused) override;
    int  sampleRate() const override;
    OutputLatency outputLatency() const override;
    bool deviceLost() const override;

  private:
    // OutputRoute: SDL with AUDIODEV, strict for a USB card; the route that opened, as SDL used it.
    bool open_route(const std::string& device) override;
    void close_route() override;
    std::string opened_route() const override;

    SdlAudioEngine&   system_;
    std::string       asound_, soundClass_;
    std::string       builtinAudiodev_;   // AUDIODEV of the built-in route ("" = SDL's default)
    bool              hadBuiltinAudiodev_ = false;
    std::string       openedRoute_;
    AudioOutputSwitch switch_{*this};
    bool              rerouteOwed_ = false;
    uint64_t          nextPollMs_ = 0;
};

}  // namespace ptshell

#endif
#endif  // POCKETTRACKER_LINUX_AUDIO_OUTPUTS_H
