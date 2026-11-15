#pragma once
// FM SYSEX — Vintage family, model 122 (TrackWerk, Kid Bunka, GPL-3.0).
//
// Plays one 6-operator voice in the DX7 format: either a voice the instrument carries (loaded from a
// .syx bank and stored in the project, fm_sysex.h) or one of the 96 voices of Plaits' three built-in
// banks. The operators, algorithms, envelopes and LFO are Plaits' fm::Voice (MIT, vendor/mutable),
// run here at exactly 48 kHz and with the gate held for the whole note: TrackWerk's instrument envelope
// shapes the release, the voice's own envelopes shape attack, decay and sustain.
//   macro A  Brightness  scales the modulators' levels; centre = as stored
//   macro B  Envelope    scales the envelope times; centre = as stored
//   macro C  Velocity    the key velocity the voice's sensitivity responds to
// Changing the voice data reloads the patch (Plaits precomputes the operators: the first 24 samples
// after a change stay silent rather than glitching). Fixed size, no allocation, no locks.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "fm_sysex.h"
#include "plaits/dsp/fm/algorithms.h"
#include "plaits/dsp/fm/lfo.h"
#include "plaits/dsp/fm/patch.h"
#include "plaits/dsp/fm/voice.h"
#include "stmlib/dsp/dsp.h"

namespace studio {

class FmSysexVoice {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kMaxBlock = 24;

    FmSysexVoice() {
        algorithms_.Init();
        voice_.Init(&algorithms_, kRate);
        lfo_.Init(kRate);
        packed_.fill(0);
        start();
    }

    // A new note: the next block opens the gate from closed, so every envelope starts at its attack.
    void start() {
        gateOpen_ = false;
        lfo_.Reset();
    }

    // The voice to play (128 packed bytes). Reloads only when the bytes differ from the loaded ones.
    void set_voice(const fm_sysex::PackedVoice& voice) {
        if (loaded_ && std::memcmp(voice.data(), packed_.data(), packed_.size()) == 0) return;
        packed_ = voice;
        patch_.Unpack(packed_.data());
        voice_.SetPatch(&patch_);
        lfo_.Set(patch_.modulations);
        loaded_ = true;
    }
    const fm_sysex::PackedVoice& voice() const { return packed_; }

    void render(float* out, int frames, float hz, float brightness, float envelope, float velocity) {
        frames = std::clamp(frames, 0, kMaxBlock);
        std::fill(temp_, temp_ + 3 * kMaxBlock, 0.f);
        if (!loaded_) {
            std::fill(out, out + frames, 0.f);
            return;
        }
        plaits::fm::Voice<6>::Parameters p{};
        p.sustain = false;
        p.gate = gateOpen_;
        p.note = 69.f + 12.f * std::log2(std::clamp(hz, 8.f, 12000.f) / 440.f);
        p.velocity = std::clamp(velocity, 0.f, 1.f);
        p.brightness = std::clamp(brightness, 0.f, 1.f);
        p.envelope_control = std::clamp(envelope, 0.f, 1.f);
        lfo_.Step(float(frames));
        p.pitch_mod = lfo_.pitch_mod();
        p.amp_mod = lfo_.amp_mod();
        voice_.Render(p, temp_, size_t(frames));
        for (int i = 0; i < frames; ++i) out[i] = stmlib::SoftClip(temp_[i] * .25f);
        gateOpen_ = true;   // closed for exactly one block after start(): a clean rising edge
    }

private:
    plaits::fm::Algorithms<6> algorithms_;
    plaits::fm::Voice<6> voice_;
    plaits::fm::Lfo lfo_;
    plaits::fm::Patch patch_{};
    fm_sysex::PackedVoice packed_{};
    float temp_[3 * kMaxBlock]{};
    bool loaded_ = false, gateOpen_ = false;
};

}  // namespace studio
