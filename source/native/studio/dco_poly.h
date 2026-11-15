#pragma once
// DCO POLY — Vintage family, model 121 (TrackWerk, Kid Bunka, GPL-3.0).
//
// A classic DCO polysynth voice of the early-80s kind: a digitally controlled oscillator with saw,
// pulse (manual width and LFO PWM), a square sub-oscillator one octave down and noise; a passive
// high-pass with a bass-boost position; a resonant four-pole low-pass with envelope, key follow and
// soft saturation; and the two-tap bucket-brigade chorus in its three settings. Our own model of
// that signal path — no code or samples from any emulator or instrument.
//   macro A  Cutoff      low-pass cutoff, 20 Hz … 20 kHz
//   macro B  Resonance   up to self-oscillation
//   macro C  Chorus      Off, I, II, I+II (four quarters of the range)
//   params   Saw, Pulse, Pulse width, PWM, Sub, Noise (levels 0..1), HPF (0 boost, 1 flat, 2, 3),
//            Env amount, Env decay, Env sustain, LFO rate, Vibrato, Key follow
// The oscillator's jumps (saw wrap, pulse edges, sub flips) are smoothed by a two-sample PolyBLEP
// (one sample of latency). The filter is a zero-delay-feedback ladder solved per sample, with the
// cutoff updated every 8 samples and the macros smoothed (no zipper noise, no clicks); the chorus
// glides between its settings. Fixed size, no allocation, no locks.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

class DcoPoly {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kHpfCount = 4;
    static constexpr int kChorusCount = 4;
    static constexpr const char* kHpfNames[kHpfCount] = {"0 Bass boost", "1 Flat", "2", "3"};
    static constexpr const char* kChorusNames[kChorusCount] = {"Off", "I", "II", "I+II"};

    struct Settings {
        float saw = 1.f, pulse = 0.f, width = 0.f, pwm = 0.f, sub = 0.f, noise = 0.f;
        int hpf = 1;
        float envAmount = .25f, decaySeconds = .4f, sustain = .5f, lfoHz = 3.f, vibratoCents = 0.f, keyFollow = .5f;
    };

    DcoPoly() { start(); }

    void start() {
        phase_ = 0.f;
        sub_ = 1.f;
        pending_ = 0.f;
        for (float& s : ladder_) s = 0.f;
        hpState_ = boostState_ = dcIn_ = dcOut_ = 0.f;
        env_ = 0.f;
        attack_ = true;
        lfo_ = 0.f;
        noise_ = 0x2545F491u;
        untilControl_ = 0;
        fresh_ = true;
    }

    void render(float* out, int frames, float hz, float cutoff, float resonance, float chorus, const Settings& s) {
        hz = std::clamp(hz, 8.f, 8000.f);
        const int chorusMode = std::min(int(std::clamp(chorus, 0.f, 1.f) * kChorusCount), kChorusCount - 1);
        // Chorus settings: LFO rate, centre delay and depth (ms) — I slow and wide, II faster, I+II a quick shimmer.
        static constexpr float chorusHz[kChorusCount] = {.5f, .513f, .863f, 9.75f};
        static constexpr float chorusCentre[kChorusCount] = {3.5f, 3.5f, 3.5f, 3.5f};
        static constexpr float chorusDepth[kChorusCount] = {0.f, 1.85f, 1.85f, .2f};
        const float k = 3.9f * std::clamp(resonance, 0.f, 1.f);
        const float hpCoef = s.hpf == 2 ? 1.f - std::exp(-6.2831853f * 230.f / kRate)
                           : s.hpf == 3 ? 1.f - std::exp(-6.2831853f * 720.f / kRate) : 0.f;
        const float boostCoef = 1.f - std::exp(-6.2831853f * 90.f / kRate);
        const float decayCoef = 1.f - std::exp(-1.f / (std::max(s.decaySeconds, .005f) * kRate));
        const float attackStep = 1.f / (.002f * kRate);

        for (int i = 0; i < frames; ++i) {
            // LFO (triangle) for vibrato and PWM.
            lfo_ += s.lfoHz / kRate;
            lfo_ -= std::floor(lfo_);
            const float tri = lfo_ < .5f ? 4.f * lfo_ - 1.f : 3.f - 4.f * lfo_;

            // Filter envelope: 2 ms attack, then exponential decay to the sustain level.
            if (attack_) {
                env_ += attackStep;
                if (env_ >= 1.f) { env_ = 1.f; attack_ = false; }
            } else {
                env_ += (s.sustain - env_) * decayCoef;
            }

            if (untilControl_ <= 0) {
                untilControl_ = 8;
                // Macros glide (≈ 3 ms), then the cutoff in octaves: macro + envelope + key follow.
                const float glide = fresh_ ? 1.f : .08f;
                cutoff_ += (std::clamp(cutoff, 0.f, 1.f) - cutoff_) * glide;
                const float octaves = std::log2(20.f) + 10.f * cutoff_ + 7.f * s.envAmount * env_ +
                                      s.keyFollow * std::log2(hz / 261.63f);
                const float fc = std::clamp(std::exp2(octaves), 10.f, kRate * .45f);
                g_ = std::tan(3.14159265f * fc / kRate);
                // Chorus glides towards its setting (≈ 20 ms), the wet share fades in and out.
                const float cg = fresh_ ? 1.f : .03f;
                chorusMix_ += ((chorusMode == 0 ? 0.f : 1.f) - chorusMix_) * cg;
                chorusDepth_ += (chorusDepth[chorusMode] - chorusDepth_) * cg;
                chorusCentreMs_ += (chorusCentre[chorusMode] - chorusCentreMs_) * cg;
                chorusRate_ = chorusHz[chorusMode];
                // Level of the oscillator mix as it actually sums (saw and pulse partly cancel): the AC
                // power over two cycles of the sub at the current width, plus the noise.
                {
                    const float w = std::clamp(.5f - .45f * s.width + .45f * s.pwm * (lfo_ < .5f ? 4.f * lfo_ - 1.f : 3.f - 4.f * lfo_) * .5f, .05f, .95f);
                    float sum = 0.f, squares = 0.f;
                    for (int n = 0; n < 64; ++n) {
                        const float p = (float(n % 32) + .5f) / 32.f;
                        const float v = s.saw * (2.f * p - 1.f) + s.pulse * (p < w ? 1.f : -1.f) + s.sub * (n < 32 ? 1.f : -1.f);
                        sum += v;
                        squares += v * v;
                    }
                    const float mean = sum / 64.f;
                    const float power = squares / 64.f - mean * mean + s.noise * s.noise / 3.f;
                    const float target = .32f / std::sqrt(std::max(power, .12f));
                    mixGain_ = fresh_ ? target : mixGain_ + (target - mixGain_) * .1f;
                }
                fresh_ = false;
            }
            --untilControl_;

            // DCO: saw, pulse and sub from one phase; every jump band-limited.
            const float f = hz * std::exp2(s.vibratoCents * tri / 1200.f);
            const float dt = std::min(f / kRate, .45f);
            const float width = std::clamp(.5f - .45f * s.width + .45f * s.pwm * tri * .5f, .05f, .95f);
            blepPrev_ = blepNow_ = 0.f;
            const float p0 = phase_;
            float p1 = p0 + dt;
            if (p0 < width && p1 >= width) step((width - p0) / dt, -2.f * s.pulse);
            if (p1 >= 1.f) {
                p1 -= 1.f;
                const float at = (1.f - p0) / dt;
                step(at, -2.f * s.saw + 2.f * s.pulse - 2.f * sub_ * s.sub);
                sub_ = -sub_;
                if (p1 >= width) step((1.f + width - p0) / dt, -2.f * s.pulse);
            }
            phase_ = p1;
            const float naive = s.saw * (2.f * p1 - 1.f) + s.pulse * (p1 < width ? 1.f : -1.f) + s.sub * sub_;
            noise_ ^= noise_ << 13;
            noise_ ^= noise_ >> 17;
            noise_ ^= noise_ << 5;
            const float white = float(int32_t(noise_)) * (1.f / 2147483648.f);
            float x = (pending_ + blepPrev_) * mixGain_;
            pending_ = naive + blepNow_ + s.noise * white;

            // High-pass: 0 lifts the bass, 1 is flat, 2 and 3 thin the low end.
            if (s.hpf == 0) {
                boostState_ += boostCoef * (x - boostState_);
                x += .6f * boostState_;
            } else if (s.hpf >= 2) {
                hpState_ += hpCoef * (x - hpState_);
                x -= hpState_;
            }

            // Four-pole ladder, zero-delay feedback: y4 = (G⁴·x + S) / (1 + k·G⁴), input soft-clipped.
            const float g = g_, G = g / (1.f + g), beta = 1.f / (1.f + g);
            const float S = G * G * G * beta * ladder_[0] + G * G * beta * ladder_[1] + G * beta * ladder_[2] + beta * ladder_[3];
            const float G4 = G * G * G * G;
            const float drive = x * (1.f + .35f * k);   // partly restores the bass the resonance takes
            const float y4 = (G4 * drive + S) / (1.f + k * G4);
            float u = std::tanh(drive - k * y4);
            for (float& state : ladder_) {
                const float v = (u - state) * G;
                const float y = v + state;
                state = y + v;
                u = y;
            }
            const float voice = u;

            // Bucket-brigade chorus: two taps on opposite LFO slopes, summed to mono with the dry voice.
            delay_[writeIndex_] = voice;
            chorusPhase_ += chorusRate_ / kRate;
            chorusPhase_ -= std::floor(chorusPhase_);
            const float ctri = chorusPhase_ < .5f ? 4.f * chorusPhase_ - 1.f : 3.f - 4.f * chorusPhase_;
            const float wet = .5f * (tap(chorusCentreMs_ + chorusDepth_ * ctri) + tap(chorusCentreMs_ - chorusDepth_ * ctri));
            wetLp_ += .6f * (wet - wetLp_);   // the BBD's own low-pass
            writeIndex_ = (writeIndex_ + 1) & (kDelaySize - 1);
            const float mixed = voice + chorusMix_ * .85f * wetLp_;
            const float y = mixed * (1.f / (1.f + .35f * chorusMix_));

            const float dcFree = y - dcIn_ + .9987f * dcOut_;
            dcIn_ = y;
            dcOut_ = dcFree;
            out[i] = dcFree;
        }
        for (float& state : ladder_)
            if (!std::isfinite(state)) state = 0.f;
        if (!std::isfinite(dcOut_) || !std::isfinite(hpState_) || !std::isfinite(boostState_)) dcIn_ = dcOut_ = hpState_ = boostState_ = 0.f;
    }

    float envelope() const { return env_; }

private:
    static constexpr int kDelaySize = 512;   // 10.7 ms at 48 kHz

    void step(float at, float height) {
        // Two-sample band-limited step: (t+1)²/2 before the jump (t = −at), −(1−t)²/2 after (t = 1−at).
        at = std::clamp(at, 0.f, 1.f);
        blepPrev_ += height * .5f * (1.f - at) * (1.f - at);
        blepNow_ -= height * .5f * at * at;
    }
    float tap(float ms) const {
        const float back = std::clamp(ms, .5f, 10.f) * kRate * .001f;
        float pos = float(writeIndex_) - back;
        if (pos < 0.f) pos += float(kDelaySize);
        const int i0 = int(pos) & (kDelaySize - 1);
        const int i1 = (i0 + 1) & (kDelaySize - 1);
        const float frac = pos - std::floor(pos);
        return delay_[i0] + (delay_[i1] - delay_[i0]) * frac;
    }

    float phase_ = 0.f, sub_ = 1.f, pending_ = 0.f, blepPrev_ = 0.f, blepNow_ = 0.f;
    float ladder_[4]{};
    float g_ = .1f, cutoff_ = .5f, mixGain_ = .5f;
    float hpState_ = 0.f, boostState_ = 0.f, dcIn_ = 0.f, dcOut_ = 0.f;
    float env_ = 0.f, lfo_ = 0.f;
    bool attack_ = true, fresh_ = true;
    uint32_t noise_ = 0x2545F491u;
    int untilControl_ = 0;
    float delay_[kDelaySize]{};
    int writeIndex_ = 0;
    float chorusPhase_ = 0.f, chorusRate_ = .5f, chorusMix_ = 0.f, chorusDepth_ = 0.f, chorusCentreMs_ = 3.5f, wetLp_ = 0.f;
};

}  // namespace studio
