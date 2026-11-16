#include "sdl/linux-audio-outputs.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include "sdl/sdl-audio-engine.h"

#include <cstdio>
#include <cstdlib>

namespace ptshell {

LinuxAudioOutputs::LinuxAudioOutputs(SdlAudioEngine& system, std::string asoundRoot, std::string soundClassRoot)
    : system_(system), asound_(std::move(asoundRoot)), soundClass_(std::move(soundClassRoot)) {
    // The first open (before this exists) chose the built-in route: the launcher's AUDIODEV, or the
    // chip choose_linux_route picked, or SDL's default. Kept, so BUILT-IN always means that route.
    builtinAudiodev_ = system_.opened_audiodev();
    hadBuiltinAudiodev_ = !builtinAudiodev_.empty();
    openedRoute_.clear();
    // AUTO at boot: a USB card already plugged in plays from the first reopen.
    rerouteOwed_ = switch_.set_cards(alsa_cards(asound_, soundClass_));
}

bool LinuxAudioOutputs::open_route(const std::string& device) {
    if (device.empty()) {
        if (hadBuiltinAudiodev_) setenv("AUDIODEV", builtinAudiodev_.c_str(), 1);
        else unsetenv("AUDIODEV");
        system_.set_strict_route(false);   // the built-in route keeps its usual quiet fallback
    } else {
        setenv("AUDIODEV", device.c_str(), 1);
        system_.set_strict_route(true);    // a USB card that will not open fails here, visibly
    }
    const bool ok = system_.openStream();
    system_.set_strict_route(false);
    if (!ok) { openedRoute_.clear(); return false; }
    // What SDL really opened: a USB route is only that when AUDIODEV was still the card at the open.
    openedRoute_ = device.empty() ? std::string() : system_.opened_audiodev();
    std::printf("audio:   output %s\n", device.empty() ? "built-in" : openedRoute_.c_str());
    return true;
}

void LinuxAudioOutputs::close_route() { system_.closeStream(); }
std::string LinuxAudioOutputs::opened_route() const { return openedRoute_; }

void LinuxAudioOutputs::closeStream() { system_.closeStream(); }
void LinuxAudioOutputs::resumeStream() { system_.resumeStream(); }
void LinuxAudioOutputs::setPaused(bool paused) { system_.setPaused(paused); }
int LinuxAudioOutputs::sampleRate() const { return system_.sampleRate(); }
AudioBackend::OutputLatency LinuxAudioOutputs::outputLatency() const { return system_.outputLatency(); }
bool LinuxAudioOutputs::deviceLost() const { return rerouteOwed_ || system_.deviceLost(); }

void LinuxAudioOutputs::poll(uint64_t nowMs) {
    if (nowMs < nextPollMs_) return;
    nextPollMs_ = nowMs + 2000;
    if (switch_.set_cards(alsa_cards(asound_, soundClass_)) && !rerouteOwed_) {
        std::printf("audio:   outputs changed - reopening\n");
        rerouteOwed_ = true;
    }
}

}  // namespace ptshell

#endif
