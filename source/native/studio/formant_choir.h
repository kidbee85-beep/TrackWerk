#pragma once
// FORMANT CHOIR — Voice family, model 119 (TrackWerk, Kid Bunka, GPL-3.0).
//
// A sung choir: up to eight singers, each a band-limited glottal source with its own tuning, vibrato
// and slow pitch drift, plus breath noise, shaped together by five formant resonators. The formants
// come from measured vowel tables for four voice types and glide between the vowels.
//   macro A  Vowel       A (0) → E → I → O → U (255), gliding through the vowels between
//   macro B  Ensemble    0 = one solo singer; raising it brings in the others and spreads them apart
//   macro C  Breath      air in the voice: breath noise through the same formants, a softer source
//   param 2  Voice       Bass, Tenor, Alto, Soprano (formant set)
//   param 3  Singers     1 … 8: how many singers Ensemble can bring in
//   param 4  Vibrato     depth 0 … 80 cents, fading in after the note starts
//   param 5  Vibrato rate 3.5 … 7.5 Hz
// Controls are recomputed every 32 samples and the formants glide (no zipper noise, no clicks). The
// level is normalised from the formant bank's response to the source at the sung pitch, so vowel,
// voice and pitch changes keep the loudness. Fixed size, no allocation, no locks.
//
// Formant data (centre frequency, level, bandwidth for five formants per vowel and voice): the
// widely published choir formant tables, as reproduced in the Csound manual's formant appendix.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

class FormantChoir {
public:
    static constexpr int kMaxSingers = 8;
    static constexpr int kVoiceCount = 4;
    static constexpr int kVowelCount = 5;
    static constexpr int kFormants = 5;
    static constexpr float kRate = 48000.f;
    static constexpr int kControlFrames = 32;
    static constexpr const char* kVoiceNames[kVoiceCount] = {"Bass", "Tenor", "Alto", "Soprano"};
    static constexpr const char* kVowelNames[kVowelCount] = {"A", "E", "I", "O", "U"};

    struct Vowel {
        float hz[kFormants], db[kFormants], bw[kFormants];
    };
    static const Vowel& vowel(int voice, int vowelIndex) {
        static constexpr Vowel table[kVoiceCount][kVowelCount] = {
            {   // Bass
                {{600, 1040, 2250, 2450, 2750}, {0, -7, -9, -9, -20},   {60, 70, 110, 120, 130}},
                {{400, 1620, 2400, 2800, 3100}, {0, -12, -9, -12, -18}, {40, 80, 100, 120, 120}},
                {{250, 1750, 2600, 3050, 3340}, {0, -30, -16, -22, -28}, {60, 90, 100, 120, 120}},
                {{400, 750, 2400, 2600, 2900},  {0, -11, -21, -20, -40}, {40, 80, 100, 120, 120}},
                {{350, 600, 2400, 2675, 2950},  {0, -20, -32, -28, -36}, {40, 80, 100, 120, 120}},
            },
            {   // Tenor
                {{650, 1080, 2650, 2900, 3250}, {0, -6, -7, -8, -22},   {80, 90, 120, 130, 140}},
                {{400, 1700, 2600, 3200, 3580}, {0, -14, -12, -14, -20}, {70, 80, 100, 120, 120}},
                {{290, 1870, 2800, 3250, 3540}, {0, -15, -18, -20, -30}, {40, 90, 100, 120, 120}},
                {{400, 800, 2600, 2800, 3000},  {0, -10, -12, -12, -26}, {40, 80, 100, 120, 120}},
                {{350, 600, 2700, 2900, 3300},  {0, -20, -17, -14, -26}, {40, 60, 100, 120, 120}},
            },
            {   // Alto
                {{800, 1150, 2800, 3500, 4950}, {0, -4, -20, -36, -60},  {80, 90, 120, 130, 140}},
                {{400, 1600, 2700, 3300, 4950}, {0, -24, -30, -35, -60}, {60, 80, 120, 150, 200}},
                {{350, 1700, 2700, 3700, 4950}, {0, -20, -30, -36, -60}, {50, 100, 120, 150, 200}},
                {{450, 800, 2830, 3500, 4950},  {0, -9, -16, -28, -55},  {70, 80, 100, 130, 135}},
                {{325, 700, 2530, 3500, 4950},  {0, -12, -30, -40, -64}, {50, 60, 170, 180, 200}},
            },
            {   // Soprano
                {{800, 1150, 2900, 3900, 4950}, {0, -6, -32, -20, -50},  {80, 90, 120, 130, 140}},
                {{350, 2000, 2800, 3600, 4950}, {0, -20, -15, -40, -56}, {60, 100, 120, 150, 200}},
                {{270, 2140, 2950, 3900, 4950}, {0, -12, -26, -26, -44}, {60, 90, 100, 120, 120}},
                {{450, 800, 2830, 3800, 4950},  {0, -11, -22, -22, -50}, {40, 80, 100, 120, 120}},
                {{325, 700, 2700, 3800, 4950},  {0, -16, -35, -40, -60}, {50, 60, 170, 180, 200}},
            },
        };
        return table[std::clamp(voice, 0, kVoiceCount - 1)][std::clamp(vowelIndex, 0, kVowelCount - 1)];
    }

    FormantChoir() { start(); }

    // A new note: singers restart at spread phases, vibrato fades in again, filters are cleared.
    void start() {
        for (int j = 0; j < kMaxSingers; ++j) {
            phase_[j] = float(j) * .618034f - std::floor(float(j) * .618034f);
            vibPhase_[j] = float(j) * .37f - std::floor(float(j) * .37f);
            drift_[j] = driftTarget_[j] = 0.f;
            level_[j] = j == 0 ? 1.f : 0.f;
        }
        for (int i = 0; i < kFormants; ++i) s1_[i] = s2_[i] = 0.f;
        noiseState_ = 0x9E3779B9u;
        tilt_ = 0.f;
        noteSamples_ = 0;
        untilControl_ = 0;
        driftClock_ = 0;
        fresh_ = true;
    }

    // `vowelPos`, `ensemble`, `breath` are the macros 0..1; `voice` 0..3, `singers` 1..8,
    // `vibratoCents` 0..80, `vibratoHz` the rate.
    void render(float* out, int frames, float hz, float vowelPos, float ensemble, float breath, int voice,
                int singers, float vibratoCents, float vibratoHz) {
        hz = std::clamp(hz, 20.f, 4000.f);
        for (int i = 0; i < frames; ++i) {
            if (untilControl_ <= 0) {
                control(hz, std::clamp(vowelPos, 0.f, 1.f), std::clamp(ensemble, 0.f, 1.f), std::clamp(breath, 0.f, 1.f),
                        std::clamp(voice, 0, kVoiceCount - 1), std::clamp(singers, 1, kMaxSingers),
                        std::clamp(vibratoCents, 0.f, 100.f), std::clamp(vibratoHz, .5f, 12.f));
                untilControl_ = kControlFrames;
            }
            --untilControl_;

            // Glottal source: band-limited sawtooths (PolyBLEP), one per singer, weighted by its level.
            float voiced = 0.f;
            for (int j = 0; j < activeSingers_; ++j) {
                const float dt = inc_[j];
                float p = phase_[j] + dt;
                if (p >= 1.f) p -= 1.f;
                phase_[j] = p;
                float saw = 2.f * p - 1.f;
                if (p < dt) {
                    const float t = p / dt;
                    saw -= t + t - t * t - 1.f;
                } else if (p > 1.f - dt) {
                    const float t = (p - 1.f) / dt;
                    saw -= t * t + t + t + 1.f;
                }
                voiced += levelNow_[j] * saw;
                levelNow_[j] += levelStep_[j];
            }
            // Breath: white noise, louder while the lead singer's glottis opens (first half of its cycle).
            noiseState_ ^= noiseState_ << 13;
            noiseState_ ^= noiseState_ >> 17;
            noiseState_ ^= noiseState_ << 5;
            const float white = float(int32_t(noiseState_)) * (1.f / 2147483648.f);
            const float noise = white * (phase_[0] < .5f ? 1.f : .45f);
            // A softer, darker source as breath rises (one-pole lowpass), then the breath on top.
            tilt_ += tiltCoef_ * (voiced - tilt_);
            const float source = (voiced + (tilt_ - voiced) * tiltMix_) * voicedGain_ + noise * noiseGain_;

            // Five formant resonators in parallel (TPT state-variable band-pass, unity peak gain).
            float y = 0.f;
            for (int f = 0; f < kFormants; ++f) {
                const float g = gNow_[f], k = kNow_[f];
                const float hp = (source - (k + g) * s1_[f] - s2_[f]) / (1.f + g * (g + k));
                const float bp = g * hp + s1_[f];
                s1_[f] = g * hp + bp;
                const float lp = g * bp + s2_[f];
                s2_[f] = g * bp + lp;
                y += ampNow_[f] * k * bp;
                gNow_[f] += gStep_[f];
                kNow_[f] += kStep_[f];
                ampNow_[f] += ampStep_[f];
            }
            out[i] = y * gainNow_;
            gainNow_ += gainStep_;
        }
        for (int i = 0; i < kFormants; ++i) {   // keep the filter state finite whatever arrives
            if (!std::isfinite(s1_[i]) || !std::isfinite(s2_[i])) s1_[i] = s2_[i] = 0.f;
        }
    }

    // The formant centre frequencies of the last control update, for tests and the display.
    float formant_hz(int index) const { return formantHz_[std::clamp(index, 0, kFormants - 1)]; }

private:
    void control(float hz, float vowelPos, float ensemble, float breath, int voice, int singers, float vibratoCents,
                 float vibratoHz) {
        const float dtControl = float(kControlFrames) / kRate;
        noteSamples_ += kControlFrames;
        const float noteSeconds = float(noteSamples_) / kRate;

        // The vowel: between two neighbours of A E I O U — frequencies and bandwidths on a log scale.
        const float pos = vowelPos * float(kVowelCount - 1);
        const int lo = std::min(int(pos), kVowelCount - 2);
        const float x = pos - float(lo);
        const Vowel& a = vowel(voice, lo);
        const Vowel& b = vowel(voice, lo + 1);
        float targetHz[kFormants], targetAmp[kFormants], targetBw[kFormants];
        for (int f = 0; f < kFormants; ++f) {
            targetHz[f] = std::exp2(std::log2(a.hz[f]) + (std::log2(b.hz[f]) - std::log2(a.hz[f])) * x);
            targetBw[f] = std::exp2(std::log2(a.bw[f]) + (std::log2(b.bw[f]) - std::log2(a.bw[f])) * x);
            targetAmp[f] = std::pow(10.f, (a.db[f] + (b.db[f] - a.db[f]) * x) / 20.f);
            // A singer's first formant follows the pitch up rather than sitting below it (formant tuning).
            if (f == 0) targetHz[f] = std::max(targetHz[f], hz * 1.05f);
            targetBw[f] *= 1.f + 1.5f * breath;   // breathy voices have damped, wider formants
        }
        // Glide: on a new note the formants start where they belong, afterwards they move smoothly.
        const float glide = fresh_ ? 1.f : .18f;
        for (int f = 0; f < kFormants; ++f) {
            formantHz_[f] = fresh_ ? targetHz[f] : std::exp2(std::log2(formantHz_[f]) + (std::log2(targetHz[f]) - std::log2(formantHz_[f])) * glide);
            formantBw_[f] = fresh_ ? targetBw[f] : formantBw_[f] + (targetBw[f] - formantBw_[f]) * glide;
            formantAmp_[f] = fresh_ ? targetAmp[f] : formantAmp_[f] + (targetAmp[f] - formantAmp_[f]) * glide;
        }

        // Singers: Ensemble brings them in one after another and spreads their tuning.
        activeSingers_ = singers;
        const float spreadCents = 22.f * ensemble;
        const float driftCents = 9.f * ensemble;
        static constexpr float detune[kMaxSingers] = {0.f, .9f, -.8f, .45f, -.5f, 1.f, -1.f, .2f};
        static constexpr float rateSpread[kMaxSingers] = {1.f, 1.07f, .94f, 1.03f, .97f, 1.1f, .91f, 1.05f};
        // Vibrato fades in after 150 ms over 400 ms, as a held sung note does.
        const float onset = std::clamp((noteSeconds - .15f) / .4f, 0.f, 1.f);
        if (++driftClock_ >= 375) {   // new drift targets every 250 ms
            driftClock_ = 0;
            for (int j = 0; j < kMaxSingers; ++j) driftTarget_[j] = random_bipolar();
        }
        float power = 0.f;
        for (int j = 0; j < singers; ++j) {
            const float wanted = j == 0 ? 1.f : std::clamp(ensemble * float(singers - 1) * 1.25f - float(j - 1), 0.f, 1.f);
            if (fresh_) level_[j] = wanted;
            const float from = level_[j];
            level_[j] = wanted;
            levelNow_[j] = from;
            levelStep_[j] = (wanted - from) / float(kControlFrames);
            power += wanted * wanted;

            drift_[j] += (driftTarget_[j] - drift_[j]) * .01f;
            vibPhase_[j] += vibratoHz * rateSpread[j] * dtControl;
            vibPhase_[j] -= std::floor(vibPhase_[j]);
            const float vib = std::sin(6.2831853f * vibPhase_[j]) * vibratoCents * onset;
            const float cents = detune[j] * spreadCents + drift_[j] * driftCents + vib;
            inc_[j] = std::min(hz * std::exp2(cents / 1200.f) / kRate, .45f);
        }
        for (int j = singers; j < kMaxSingers; ++j) level_[j] = levelNow_[j] = levelStep_[j] = 0.f;

        // Breath: noise grows, the voiced source softens and darkens.
        const float noiseLevel = .9f * breath * breath + .015f;
        voicedGain_ = 1.f - .55f * breath;
        tiltMix_ = .85f * breath;
        tiltCoef_ = 1.f - std::exp(-6.2831853f * 1800.f / kRate);

        // Loudness: the bank's response to one singer's source at this pitch (harmonic k has 1/k),
        // summed over harmonics below the band edge; the singers add as uncorrelated voices.
        float response = 0.f;
        const int harmonics = std::min(160, int(kRate * .45f / hz));
        for (int k = 1; k <= harmonics; ++k) {
            const float f0 = hz * float(k);
            float h = 0.f;
            for (int f = 0; f < kFormants; ++f) h += formantAmp_[f] * bandpass_gain(f0, formantHz_[f], formantBw_[f]);
            response += h * h / float(k * k);
        }
        // A sawtooth's harmonic k has amplitude 2/(πk), so power (2/π)²/(2k²).
        response *= .2026424f * voicedGain_ * voicedGain_ * std::max(power, 1.f);
        // Noise through the bank: white power density × the bank's bandwidth (≈ Σ a² · bw · π/2).
        float noiseResponse = 0.f;
        for (int f = 0; f < kFormants; ++f) noiseResponse += formantAmp_[f] * formantAmp_[f] * formantBw_[f] * 1.5708f;
        // Uniform noise in −1…1 has power 1/3; one-sided density over 24 kHz; the half-cycle gate ≈ .6.
        noiseResponse *= noiseLevel * noiseLevel * (1.f / 3.f) / (kRate * .5f) * .6f;
        noiseGain_ = noiseLevel;
        const float gainTarget = .2f / std::sqrt(std::max(response + noiseResponse, 1e-6f));

        // Ramps for everything the sample loop interpolates.
        const float inv = 1.f / float(kControlFrames);
        for (int f = 0; f < kFormants; ++f) {
            const float g = std::tan(3.14159265f * std::min(formantHz_[f], kRate * .45f) / kRate);
            const float k = formantBw_[f] / formantHz_[f];
            if (fresh_) { gNow_[f] = g; kNow_[f] = k; ampNow_[f] = formantAmp_[f]; }
            gStep_[f] = (g - gNow_[f]) * inv;
            kStep_[f] = (k - kNow_[f]) * inv;
            ampStep_[f] = (formantAmp_[f] - ampNow_[f]) * inv;
        }
        if (fresh_) gainNow_ = gainTarget;
        gainStep_ = (gainTarget - gainNow_) * inv;
        fresh_ = false;
    }

    // |H|² of a unity-peak band-pass at `f` for centre `fc` and bandwidth `bw` (all Hz), as a magnitude.
    static float bandpass_gain(float f, float fc, float bw) {
        const float q = fc / bw;
        const float d = q * (f / fc - fc / f);
        return 1.f / std::sqrt(1.f + d * d);
    }
    float random_bipolar() {
        noiseState_ ^= noiseState_ << 13;
        noiseState_ ^= noiseState_ >> 17;
        noiseState_ ^= noiseState_ << 5;
        return float(int32_t(noiseState_)) * (1.f / 2147483648.f);
    }

    float phase_[kMaxSingers]{}, inc_[kMaxSingers]{}, vibPhase_[kMaxSingers]{}, drift_[kMaxSingers]{},
        driftTarget_[kMaxSingers]{}, level_[kMaxSingers]{}, levelNow_[kMaxSingers]{}, levelStep_[kMaxSingers]{};
    float s1_[kFormants]{}, s2_[kFormants]{}, gNow_[kFormants]{}, kNow_[kFormants]{}, ampNow_[kFormants]{},
        gStep_[kFormants]{}, kStep_[kFormants]{}, ampStep_[kFormants]{};
    float formantHz_[kFormants]{500, 1500, 2500, 3500, 4500}, formantBw_[kFormants]{80, 90, 120, 130, 140},
        formantAmp_[kFormants]{};
    float tilt_ = 0.f, tiltCoef_ = 0.f, tiltMix_ = 0.f, voicedGain_ = 1.f, noiseGain_ = 0.f;
    float gainNow_ = 0.f, gainStep_ = 0.f;
    uint32_t noiseState_ = 0x9E3779B9u;
    int activeSingers_ = 1, noteSamples_ = 0, untilControl_ = 0, driftClock_ = 0;
    bool fresh_ = true;
};

}  // namespace studio
