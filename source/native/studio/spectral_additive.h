#pragma once
// SP ADDITIVE — the first engine of the Studio's Spectral family (TrackWerk, Kid Bunka, GPL-3.0).
//
// A bank of up to 128 partials, each a complex rotator (two multiplies and two adds per sample, no
// phase wrap, no table). The spectrum is shaped once per 24-sample block and ramped linearly across
// it, so moving any control is click-free:
//   macro A  Brightness      spectral tilt, from k^-2.4 (dark) to k^-0.35 (bright)
//   macro B  Spectral comb   a cosine comb over the partial index that drifts slowly; 0 = off
//   macro C  Stretch         inharmonicity f_k = f0·k·sqrt(1 + β k²), organ (0) to bell (1)
//   param 2  Partials        8 … 128 (default 64)
//   param 3  Even harmonics  0 = odd only (hollow), 255 = all
//   param 4  Formant         a +12 dB resonance swept 200 Hz … 16 kHz, 0 = off
//   param 5  Shimmer         slow random motion of each partial's level
// Partials above 0.42 of the sample rate are faded out before they can alias. The level is
// normalised by the spectrum's energy, so brightness and partial count do not change loudness.
// Fixed size, no allocation, no locks; 48 kHz native rate like the other Studio DSP models.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace studio {

class SpectralAdditive {
public:
    static constexpr int kMaxPartials = 128;
    static constexpr float kRate = 48000.f;

    // A new note: partials restart at fixed, spread phases (low crest factor, reproducible).
    void start() {
        for (int k = 0; k < kMaxPartials; ++k) {
            const float phase = 6.2831853f * float((k * 0.618034f) - std::floor(k * 0.618034f));
            re_[k] = std::cos(phase);
            im_[k] = std::sin(phase);
            amp_[k] = 0.f;
            drift_[k] = 0.f;
            driftTarget_[k] = 0.f;
        }
        lastHz_ = lastStretch_ = lastTilt_ = lastFormant_ = -1.f;
        lastCount_ = -1;
        combPhase_ = 0.f;
        shimmerClock_ = 0;
    }

    // `params` are the Studio's 0..255 values (-1 = factory default, resolved by the caller).
    void render(float* out, int frames, float hz, float a, float b, float c,
                int partials, int even, int formant, int shimmer) {
        const int wanted = std::clamp(partials, 8, kMaxPartials);
        const float stretch = c;
        update_frequencies(std::max(hz, 1.f), stretch, wanted);
        update_amplitudes(a, b, even, formant, shimmer, frames);

        // Two passes per sample, both vectorisable without -ffast-math: rotate every partial and store
        // its weighted output, then sum those in eight independent lanes (a single running sum would
        // be added strictly in order). Partials past count_ up to the next multiple of 8 are silent.
        const int n = (count_ + 7) & ~7;
        float* __restrict re = re_;
        float* __restrict im = im_;
        const float* __restrict cr = cr_;
        const float* __restrict ci = ci_;
        float* __restrict amp = amp_;
        const float* __restrict step = ampStep_;
        float* __restrict weighted = weighted_;
        for (int i = 0; i < frames; ++i) {
            for (int k = 0; k < n; ++k) {
                const float r = re[k] * cr[k] - im[k] * ci[k];
                const float m = re[k] * ci[k] + im[k] * cr[k];
                re[k] = r;
                im[k] = m;
                weighted[k] = m * amp[k];
                amp[k] += step[k];
            }
            float l0 = 0, l1 = 0, l2 = 0, l3 = 0, l4 = 0, l5 = 0, l6 = 0, l7 = 0;
            for (int k = 0; k < n; k += 8) {
                l0 += weighted[k];     l1 += weighted[k + 1]; l2 += weighted[k + 2]; l3 += weighted[k + 3];
                l4 += weighted[k + 4]; l5 += weighted[k + 5]; l6 += weighted[k + 6]; l7 += weighted[k + 7];
            }
            out[i] = ((l0 + l1) + (l2 + l3)) + ((l4 + l5) + (l6 + l7));
        }
        // Keep every rotator on the unit circle (first-order correction, once per block).
        for (int k = 0; k < n; ++k) {
            const float g = 1.5f - .5f * (re_[k] * re_[k] + im_[k] * im_[k]);
            re_[k] *= g;
            im_[k] *= g;
        }
    }

private:
    void update_frequencies(float hz, float stretch, int wanted) {
        if (hz == lastHz_ && stretch == lastStretch_ && wanted == lastCount_) return;
        lastHz_ = hz;
        lastStretch_ = stretch;
        lastCount_ = wanted;
        const float beta = .0025f * stretch * stretch;
        int n = 0;
        for (int k = 1; k <= wanted; ++k) {
            const float f = hz * k * std::sqrt(1.f + beta * float(k * k));
            freq_[k - 1] = f;
            if (f < kRate * .42f) n = k;
        }
        // A partial that is no longer rendered must start silent if it comes back, and the padding up
        // to the next multiple of 8 stays silent.
        for (int k = n; k < kMaxPartials; ++k) amp_[k] = ampStep_[k] = 0.f;
        count_ = n;
        lastTilt_ = lastFormant_ = -1.f;   // both caches depend on the partial set
        for (int k = 0; k < n; ++k) {
            const float w = 6.2831853f * freq_[k] / kRate;
            cr_[k] = std::cos(w);
            ci_[k] = std::sin(w);
        }
    }

    void update_amplitudes(float a, float b, int even, int formant, int shimmer, int frames) {
        const float tilt = 2.4f - 2.05f * a;
        const float evenGain = std::clamp(even, 0, 255) / 255.f;
        const float combDepth = b < .04f ? 0.f : std::min(1.f, (b - .04f) * 2.f);
        const float combDensity = .03f + .22f * b;              // comb cycles per partial
        // Slow drift, 0.03 … 0.28 comb cycles per second: the notches glide, they do not tremolo.
        combPhase_ += (.03f + .25f * b) * float(frames) / kRate;
        combPhase_ -= std::floor(combPhase_);
        const bool formantOn = formant > 0;
        const float formantHz = 200.f * std::exp2(6.3f * std::clamp(formant, 0, 255) / 255.f);
        const float shimmerDepth = std::clamp(shimmer, 0, 255) / 255.f;

        // Shimmer: each partial walks towards a new random level every ~85 ms.
        if (shimmerDepth > 0.f && (shimmerClock_++ % 170) == 0)
            for (int k = 0; k < count_; ++k) driftTarget_[k] = random() * 2.f - 1.f;

        // pow/exp only when their control moved: a held note costs a multiply per partial per block.
        if (tilt != lastTilt_) {
            lastTilt_ = tilt;
            for (int k = 0; k < count_; ++k) tiltGain_[k] = std::pow(float(k + 1), -tilt);
        }
        const float formantKey = formantOn ? formantHz : 0.f;
        if (formantKey != lastFormant_) {
            lastFormant_ = formantKey;
            for (int k = 0; k < count_; ++k) {
                const float octaves = formantOn ? std::log2(freq_[k] / formantHz) : 0.f;
                formantGain_[k] = formantOn ? 1.f + 3.f * std::exp(-octaves * octaves * 4.f) : 1.f;   // ≈ +12 dB, ½ octave
            }
        }
        // The comb as a rotating complex phasor over the partial index: one cos/sin per block.
        const float combStep = 6.2831853f * combDensity, combStart = 6.2831853f * (combDensity + combPhase_);
        const float stepR = std::cos(combStep), stepI = std::sin(combStep);
        float combR = std::cos(combStart), combI = std::sin(combStart);

        float energy = 0.f;
        for (int k = 0; k < count_; ++k) {
            const int number = k + 1;
            float g = tiltGain_[k] * formantGain_[k];
            if ((number & 1) == 0) g *= evenGain;
            if (combDepth > 0.f) {
                g *= 1.f - combDepth * (.5f - .5f * combR);
                const float r = combR * stepR - combI * stepI;
                combI = combR * stepI + combI * stepR;
                combR = r;
            }
            drift_[k] += .02f * (driftTarget_[k] - drift_[k]);
            g *= std::max(0.f, 1.f + shimmerDepth * .8f * drift_[k]);
            // Band edge: fade the top 10 % below the alias limit.
            g *= std::clamp((kRate * .42f - freq_[k]) / (kRate * .042f), 0.f, 1.f);
            target_[k] = g;
            energy += g * g;
        }
        const float norm = energy > 0.f ? .3f / std::sqrt(energy) : 0.f;
        const float inv = 1.f / float(std::max(1, frames));
        for (int k = 0; k < count_; ++k) ampStep_[k] = (target_[k] * norm - amp_[k]) * inv;
    }

    float random() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return float(seed_ >> 8) / float(1u << 24);
    }

    alignas(16) float re_[kMaxPartials]{}, im_[kMaxPartials]{}, cr_[kMaxPartials]{}, ci_[kMaxPartials]{};
    alignas(16) float amp_[kMaxPartials]{}, ampStep_[kMaxPartials]{}, target_[kMaxPartials]{}, weighted_[kMaxPartials]{};
    float freq_[kMaxPartials]{}, drift_[kMaxPartials]{}, driftTarget_[kMaxPartials]{};
    float tiltGain_[kMaxPartials]{}, formantGain_[kMaxPartials]{};
    float lastHz_ = -1.f, lastStretch_ = -1.f, lastTilt_ = -1.f, lastFormant_ = -1.f, combPhase_ = 0.f;
    int lastCount_ = -1, count_ = 0;
    uint32_t shimmerClock_ = 0, seed_ = 0x5eedu;
};

}  // namespace studio
