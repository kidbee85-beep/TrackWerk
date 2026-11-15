#pragma once
// SP FREEZE — Spectral family, model 117 (TrackWerk, Kid Bunka, GPL-3.0).
//
// Freezes the spectrum of a moment of a sample and plays it as a held, pitched texture:
//   macro A  Position   where in the source sample the 4096-point frame is taken
//   macro B  Smear      0 = one frozen frame; up = the frame keeps blending in neighbouring frames
//                       (±0.5 s around the position), so the freeze breathes
//   macro C  Tilt       darker … neutral (centre) … brighter
//   param 2  Source     the sample slot to freeze (an ordinary sampler instrument's slot)
//   param 3  Jitter     random phase motion: 0 keeps partials steady, up turns them to air
// Resynthesis: fixed magnitudes, per-bin phase advance (phase-vocoder style) plus jitter, Hann
// window, 4× overlap-add. The frozen spectrum is resampled by note/C4, so the keyboard plays it.
// Without a usable source (empty slot, a synth slot, shorter than a frame) a built-in vowel spectrum
// is frozen instead, so the model always speaks.
//
// Real-time: no allocation, no locks. KissFFT states live in fixed member buffers (sized once in the
// constructor). The source PCM is lent (set_source … release, see Synth::SourceLoan): it is read only
// while lent, which the engine does only inside a piece it mixes under the sample-edit lock. What was
// frozen stays playable after release; a new capture waits for the next loan.
// The source's own sample rate maps its bins to Hz, so C4 keeps the source pitch at 32/44.1/48/96 kHz,
// and Smear's reach is in seconds of the source.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "kiss_fft.h"
#include "kiss_fftr.h"

namespace studio {

class SpectralFreeze {
public:
    static constexpr int kN = 4096;           // frame
    static constexpr int kHop = kN / 4;       // 4× overlap
    static constexpr int kBins = kN / 2 + 1;
    static constexpr float kRate = 48000.f;
    static constexpr float kRootHz = 261.6256f;   // C4 plays the source at its own pitch

    SpectralFreeze() {
        for (int i = 0; i < kN; ++i) window_[i] = .5f - .5f * std::cos(6.2831853f * i / kN);
        size_t need = 0;
        kiss_fftr_alloc(kN, 0, nullptr, &need);
        size_t len = sizeof(forwardMem_);
        forward_ = need <= len ? kiss_fftr_alloc(kN, 0, forwardMem_, &len) : nullptr;
        need = 0;
        kiss_fft_alloc(kN, 1, nullptr, &need);
        len = sizeof(inverseMem_);
        inverse_ = need <= len ? kiss_fft_alloc(kN, 1, inverseMem_, &len) : nullptr;
        start();
    }
    SpectralFreeze(const SpectralFreeze&) = delete;
    SpectralFreeze& operator=(const SpectralFreeze&) = delete;

    bool ready() const { return forward_ && inverse_; }

    void start() {
        std::fill(std::begin(ola_), std::end(ola_), 0.f);
        std::fill(std::begin(out_), std::end(out_), 0.f);
        // Pseudo-random start phases: an evenly stepped pattern lines harmonics up into a pulse.
        seed_ = 0xF2EE5Eu;
        for (int k = 0; k < kBins; ++k) phase_[k] = 6.2831853f * random();
        readPos_ = kHop;   // compute a hop on the first read
        captured_ = false;   // the next hop freezes afresh — from the loan in force, if any
        lastStart_ = -1;
        // The loan and its identity (src_, srcKey_, srcGen_, srcLen_, srcRate_) are left alone: a note
        // starts inside a piece whose loan was opened before it, and its first hop must use that loan.
    }

    // Lends the slot's PCM (or none) for the rendering that follows, until release().
    void set_source(const float* data, int length, uint32_t generation, int sampleRate) {
        const bool usable = data && length >= kN && sampleRate > 0;
        src_ = usable ? data : nullptr;
        srcLen_ = usable ? length : 0;
        const int rate = usable ? sampleRate : 0;
        if (src_ != srcKey_ || generation != srcGen_ || srcLen_ != srcKeyLen_ || rate != srcRate_) {
            srcKey_ = src_; srcGen_ = generation; srcKeyLen_ = srcLen_; srcRate_ = rate;
            captured_ = false;   // a new or changed source: take a fresh frame
        }
    }
    // Ends the loan: no PCM is read again until the next set_source.
    void release() { src_ = nullptr; }

    void render(float* out, int frames, float hz, float a, float b, float c, int jitter) {
        if (!ready()) { std::fill(out, out + frames, 0.f); return; }
        for (int i = 0; i < frames; ++i) {
            if (readPos_ >= kHop) { hop(hz, a, b, c, std::clamp(jitter, 0, 255) / 255.f); readPos_ = 0; }
            out[i] = out_[readPos_++];
        }
    }

private:
    static float fract(float x) { return x - std::floor(x); }
    float random() { seed_ = seed_ * 1664525u + 1013904223u; return float(seed_ >> 8) / float(1u << 24); }

    // One frame of the source at `start`, windowed, into |X| (and the bin phases, if asked).
    void analyse(int start, float* magnitudes, float* phases = nullptr) {
        for (int i = 0; i < kN; ++i) frame_[i] = src_[start + i] * window_[i];
        kiss_fftr(forward_, frame_, half_);
        for (int k = 0; k < kBins; ++k) {
            magnitudes[k] = std::hypot(half_[k].r, half_[k].i);
            if (phases) phases[k] = std::atan2(half_[k].i, half_[k].r);
        }
    }

    // Phase-vocoder frequency estimate: the phase each bin gains over one hop, two frames apart, gives
    // its true frequency (in bins). Resynthesis advances phases by that, so a partial between two
    // bins stays one steady partial instead of beating at the bin spacing.
    void estimate_frequencies(int start) {
        // The partner frame one hop later — or, at the very end of the source, one hop earlier.
        const int span = srcLen_ - kN;
        const int other = start + kHop <= span ? start + kHop : std::max(0, start - kHop);
        analyse(start, mag_, phaseA_);
        if (other == start) { for (int k = 0; k < kBins; ++k) binFreq_[k] = float(k); return; }
        analyse(other, fresh_, phaseB_);
        const int gap = other - start;   // ±kHop
        for (int k = 0; k < kBins; ++k) {
            const float expected = 6.2831853f * k * gap / kN;
            float d = phaseB_[k] - phaseA_[k] - expected;
            d -= 6.2831853f * std::round(d / 6.2831853f);
            binFreq_[k] = k + d * kN / (6.2831853f * gap);
        }
    }

    // The built-in source: a sung "ah" on C3 — harmonics shaped by three formants — as magnitudes.
    void builtin(float* magnitudes) {
        std::fill(magnitudes, magnitudes + kBins, 0.f);
        for (int k = 0; k < kBins; ++k) binFreq_[k] = float(k);
        const float f0 = 130.81f;
        for (int h = 1; h * f0 < 11000.f; ++h) {
            const float f = h * f0;
            auto peak = [&](float centre, float width, float gain) { const float d = (f - centre) / width; return gain * std::exp(-d * d); };
            const float level = (.08f + peak(730.f, 90.f, 1.f) + peak(1090.f, 110.f, .5f) + peak(2440.f, 160.f, .25f)) / std::sqrt(float(h));
            const float bin = f * kN / kRate;
            const int k = int(bin);
            if (k + 1 < kBins) {
                magnitudes[k] += level * (1.f - (bin - k)) * kN * .25f;
                magnitudes[k + 1] += level * (bin - k) * kN * .25f;
                binFreq_[k] = binFreq_[k + 1] = bin;   // both bins carry the partial's true frequency
            }
        }
    }

    void hop(float hz, float a, float b, float c, float jitter) {
        // 1. The frozen magnitudes: captured on a new source/position, blended on with Smear.
        const bool hasSource = srcKey_ != nullptr;   // the slot's identity, kept across loans
        if (src_) {
            const int span = srcLen_ - kN;
            const int start = int(std::clamp(a, 0.f, 1.f) * span);
            // Any change of position is captured, at most once per hop (this function's rate): short
            // sources (4096…5119 frames) move by less than a hop end to end.
            const bool moved = start != lastStart_;
            if (!captured_ || moved) {
                estimate_frequencies(start);   // also fills mag_ from the frame at `start`
                captured_ = true; lastStart_ = start;
            } else if (b > .02f) {
                const int reach = int(b * .5f * float(srcRate_));   // ±0.5 s of the source
                const int at = std::clamp(start + int((random() * 2.f - 1.f) * reach), 0, span);
                analyse(at, fresh_);
                const float blend = .25f * b;
                for (int k = 0; k < kBins; ++k) mag_[k] += blend * (fresh_[k] - mag_[k]);
            }
        } else if (!captured_ || (!hasSource && lastStart_ != -2)) {
            // No PCM lent: a new note (or a slot without a source) speaks the built-in vowel.
            builtin(mag_);
            captured_ = true; lastStart_ = -2;
        }
        // (A held note between loans keeps what it froze; capturing waits for the next loan.)
        const bool fromSource = lastStart_ >= 0;   // mag_ holds source bins, spaced srcRate_/N

        // 2. The output spectrum: magnitudes read at bin/ratio (the keyboard), tilted, with phases
        //    advanced coherently per bin plus Jitter.
        // Source bins are srcRate/N apart, output bins kRate/N: C4 keeps the source's own pitch.
        const float sourceScale = fromSource && srcRate_ > 0 ? float(srcRate_) / kRate : 1.f;
        const float ratio = std::max(hz, 1.f) / kRootHz * sourceScale;
        const float tilt = (std::clamp(c, 0.f, 1.f) - .5f) * 2.4f;   // −1.2 … +1.2 octave slope
        if (tilt != lastTilt_) {   // pow per bin only when Tilt moved
            lastTilt_ = tilt;
            for (int k = 1; k < kN / 2; ++k) tiltGain_[k] = std::pow(float(k) / 96.f, tilt);   // ≈ 1.1 kHz pivot
        }
        float energy = 0.f;
        spec_[0] = {0.f, 0.f};
        spec_[kN / 2] = {0.f, 0.f};
        for (int k = 1; k < kN / 2; ++k) {
            const float from = k / ratio;
            const int i = int(from);
            float m = 0.f, freq = float(k);
            if (i + 1 < kBins) {
                m = mag_[i] + (from - i) * (mag_[i + 1] - mag_[i]);
                freq = (binFreq_[i] + (from - i) * (binFreq_[i + 1] - binFreq_[i])) * ratio;
            }
            m *= tiltGain_[k];
            phase_[k] += 6.2831853f * freq * kHop / kN + jitter * 3.1415927f * (random() * 2.f - 1.f);
            phase_[k] -= 6.2831853f * std::floor(phase_[k] / 6.2831853f);
            spec_[k] = {m * std::cos(phase_[k]), m * std::sin(phase_[k])};
            spec_[kN - k] = {spec_[k].r, -spec_[k].i};
            energy += m * m;
        }
        // RMS ≈ 0.16 whatever the source level, frame position or tilt. With steady phases (no
        // Jitter) the four overlapping frames add in amplitude (Σw = 2, power 4); with random phases in
        // power (Σw² = 1.5). The overlap factor follows Jitter between the two.
        const float overlap = 2.f * (4.f - 2.5f * jitter);   // 8 … 3: 2·Σ|X|² · overlap / N²
        const float gain = energy > 1e-12f ? .16f / std::sqrt(overlap * energy / (float(kN) * float(kN))) / float(kN) : 0.f;

        // 3. Inverse (this KissFFT's inverse already scales by 1/N), window, overlap-add.
        kiss_fft(inverse_, spec_, time_);
        for (int i = 0; i < kN; ++i) ola_[i] += time_[i].r * window_[i] * gain * float(kN);
        for (int i = 0; i < kHop; ++i) out_[i] = std::isfinite(ola_[i]) ? std::clamp(ola_[i], -1.5f, 1.5f) : 0.f;
        std::copy(ola_ + kHop, ola_ + kN, ola_);
        std::fill(ola_ + kN - kHop, ola_ + kN, 0.f);
    }

    float window_[kN]{}, frame_[kN]{}, ola_[kN]{}, out_[kHop]{};
    float tiltGain_[kBins]{}, lastTilt_ = -99.f;
    float mag_[kBins]{}, fresh_[kBins]{}, phase_[kBins]{}, binFreq_[kBins]{}, phaseA_[kBins]{}, phaseB_[kBins]{};
    kiss_fft_cpx half_[kBins]{}, spec_[kN]{}, time_[kN]{};
    alignas(16) char forwardMem_[48 * 1024]{};
    alignas(16) char inverseMem_[48 * 1024]{};
    kiss_fftr_cfg forward_ = nullptr;
    kiss_fft_cfg inverse_ = nullptr;
    const float* src_ = nullptr;
    const float* srcKey_ = nullptr;
    int srcLen_ = 0, srcKeyLen_ = 0, srcRate_ = 0, readPos_ = kHop, lastStart_ = -1;
    uint32_t srcGen_ = 0, seed_ = 0xF2EE5Eu;
    bool captured_ = false;
};

}  // namespace studio
