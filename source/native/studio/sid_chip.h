#pragma once
// SID CHIP — Vintage family, model 120 (TrackWerk, Kid Bunka, GPL-3.0).
//
// The sound of the C64's SID as a tracker uses it: one SID voice with every waveform the chip offers,
// tracker arpeggios at the PAL frame rate, pulse-width sweeps and the multimode filter, with the
// character of the 6581 (dark, saturating) or the 8580 (clean). Our own model of the chip's
// behaviour, not a circuit emulation; no code from reSID or other emulators.
//   macro A  Colour      pulse width on pulse waves (square → thin), the modulator ratio on Sync and
//                        Ring, the DAC bit depth (12 → 4 bits) on Triangle, Saw, Saw+Tri and Noise
//   macro B  Arp         Off, Octave, Fifth, Major, Minor, Major 7, Minor 7, Sus 4, Diminished
//   macro C  Cutoff      filter cutoff
//   param 2  Wave        Triangle, Saw, Pulse, Noise, Saw+Tri, Pulse+Saw, Pulse+Tri, Sync, Ring
//   param 3  Chip        6581 or 8580
//   param 4  Filter      Low pass, Band pass, High pass, Off
//   param 5  Resonance
//   param 6  PWM         depth of a slow pulse-width sweep (pulse waves)
//   param 7  Arp speed   1 … 8 frames (50 Hz) per arpeggio step
// Every jump in a waveform — wraps, pulse edges, sync resets, ring flips, noise steps — is smoothed by
// a two-sample polynomial band-limited step (PolyBLEP, one sample of latency), so high notes stay
// free of fold-back. The combined Saw+Tri wave (the chip ANDs both) plays from band-limited tables.
// Fixed size, no allocation, no locks; the shared tables are built once, off the audio thread.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace studio {

class SidChip {
public:
    static constexpr float kRate = 48000.f;
    static constexpr int kWaveCount = 9;
    static constexpr int kChordCount = 9;
    static constexpr int kModeCount = 4;
    enum Wave { kTriangle, kSaw, kPulse, kNoise, kSawTri, kPulseSaw, kPulseTri, kSync, kRing };
    enum Mode { kLowPass, kBandPass, kHighPass, kFilterOff };
    static constexpr const char* kWaveNames[kWaveCount] = {"Triangle", "Saw", "Pulse", "Noise", "Saw+Tri",
                                                          "Pulse+Saw", "Pulse+Tri", "Sync", "Ring"};
    static constexpr const char* kChordNames[kChordCount] = {"Off", "Octave", "Fifth", "Major", "Minor",
                                                            "Major 7", "Minor 7", "Sus 4", "Diminished"};
    static constexpr const char* kModeNames[kModeCount] = {"Low pass", "Band pass", "High pass", "Off"};
    static constexpr int kFrameSamples = 960;   // one PAL frame (50 Hz) at 48 kHz

    SidChip() {
        tables();   // build the shared Saw+Tri tables here, never on the audio thread
        start();
    }

    void start() {
        phase_ = modPhase_ = carrier_ = 0.f;
        lfsr_ = 0x7FFFF8u;
        noiseValue_ = noise_out();
        pending_ = 0.f;
        lp_ = bp_ = 0.f;
        dcIn_ = dcOut_ = 0.f;
        frameCount_ = 0;
        arpStep_ = 0;
        untilFrame_ = kFrameSamples;   // the first note sounds a full frame
        pwmPhase_ = 0.f;
    }

    // Macros 0..1; wave, chip (0 = 6581, 1 = 8580), mode as indices; resonance and pwm 0..1; speed 1..8.
    void render(float* out, int frames, float hz, float colour, float arp, float cutoff, int wave, int chip, int mode,
                float resonance, float pwm, int speed) {
        wave = std::clamp(wave, 0, kWaveCount - 1);
        mode = std::clamp(mode, 0, kModeCount - 1);
        colour = std::clamp(colour, 0.f, 1.f);
        const int chord = std::min(int(std::clamp(arp, 0.f, 1.f) * kChordCount), kChordCount - 1);
        speed = std::clamp(speed, 1, 8);
        const bool old = chip == 0;

        // Filter coefficients (TPT state-variable filter). The 6581's curve starts higher and ends lower.
        const float c = std::clamp(cutoff, 0.f, 1.f);
        const float fc = old ? 220.f * std::exp2(5.2f * c) : 30.f * std::exp2(8.6f * c);
        const float g = std::tan(3.14159265f * std::min(fc, kRate * .45f) / kRate);
        const float k = 1.f / (.707f * std::exp2(2.2f * std::clamp(resonance, 0.f, 1.f)));
        const float h = 1.f / (1.f + g * (g + k));

        const float bits = (wave == kTriangle || wave == kSaw || wave == kSawTri || wave == kNoise)
                               ? std::exp2(std::round(12.f - 8.f * colour)) : 4096.f;
        const float gain = kWaveGain[wave];

        for (int i = 0; i < frames; ++i) {
            // Tracker time base: one arpeggio step every `speed` frames of 50 Hz.
            if (untilFrame_ <= 0) {
                untilFrame_ = kFrameSamples;
                if (++frameCount_ >= speed) { frameCount_ = 0; ++arpStep_; }
            }
            --untilFrame_;
            const int notes = kChordSize[chord];
            const float semis = chord == 0 ? 0.f : float(kChords[chord][arpStep_ % notes]);
            const float f = std::clamp(hz * std::exp2(semis / 12.f), 1.f, 8000.f);
            const float dt = std::min(f / kRate, .45f);

            // Pulse width with the slow sweep.
            pwmPhase_ += .6f / kRate;
            pwmPhase_ -= std::floor(pwmPhase_);
            const float tri = pwmPhase_ < .5f ? 4.f * pwmPhase_ - 1.f : 3.f - 4.f * pwmPhase_;
            const float width = std::clamp(.5f - .47f * colour + .45f * std::clamp(pwm, 0.f, 1.f) * tri * .5f, .02f, .98f);
            const float low = 1.f - width;   // the gate opens at this phase and closes at the wrap

            float now = 0.f;
            blepPrev_ = 0.f;
            blepNow_ = 0.f;
            const float p0 = phase_;
            float p1 = p0 + dt;
            const bool wrapped = p1 >= 1.f;
            if (wrapped) p1 -= 1.f;
            phase_ = p1;
            const float wrapAt = wrapped ? (1.f - p0) / dt : -1.f;   // fraction of this sample

            switch (wave) {
            case kTriangle:
                now = triangle(p1);
                break;
            case kSaw:
                now = 2.f * p1 - 1.f;
                if (wrapped) step(wrapAt, -2.f);
                break;
            case kPulse:
                now = p1 >= low ? 1.f : -1.f;
                edge(p0, dt, low, wrapped, wrapAt, 2.f, -2.f);
                break;
            case kNoise: {
                // The LFSR clocks 16 times per cycle (bit 19 of the 24-bit accumulator).
                const float n0 = p0 * 16.f, n1 = (p0 + dt) * 16.f;
                for (float edgeAt = std::floor(n0) + 1.f; edgeAt <= n1; edgeAt += 1.f) {
                    const float before = noiseValue_;
                    clock_lfsr();
                    noiseValue_ = noise_out();
                    step((edgeAt - n0) / (n1 - n0), noiseValue_ - before);
                }
                now = noiseValue_;
                break;
            }
            case kSawTri:
                now = read_table(tables_level(f), p1);   // the table for the pitch sounding now
                break;
            case kPulseSaw: {
                const float open = p1 >= low ? 1.f : 0.f;
                now = open * (2.f * p1 - 1.f) + (1.f - open) * -1.f;
                // Opens: −1 → saw value at `low`; closes at the wrap: +1 → −1.
                edge(p0, dt, low, wrapped, wrapAt, 2.f * low, -2.f);
                break;
            }
            case kPulseTri: {
                const float open = p1 >= low ? 1.f : 0.f;
                now = open * triangle(p1) + (1.f - open) * -1.f;
                edge(p0, dt, low, wrapped, wrapAt, triangle(low) + 1.f, 0.f);
                break;
            }
            case kSync: {
                // A saw at 1 … 8 × the note, reset whenever the note's own cycle wraps.
                const float ratio = 1.f + 7.f * colour;
                const float dc = std::min(dt * ratio, .45f);
                const float c0 = carrier_;
                float c1 = c0 + dc;
                if (wrapped) {
                    // Up to the reset the carrier runs on (wrapping on its own at most once).
                    float atReset = c0 + dc * wrapAt;
                    if (atReset >= 1.f) {
                        step((1.f - c0) / dc, -2.f);
                        atReset -= 1.f;
                    }
                    step(wrapAt, -2.f * atReset);
                    c1 = dc * (1.f - wrapAt);
                } else if (c1 >= 1.f) {
                    step((1.f - c0) / dc, -2.f);
                    c1 -= 1.f;
                }
                carrier_ = c1;
                now = 2.f * c1 - 1.f;
                break;
            }
            case kRing: {
                // The triangle's sign follows a modulator square at 0.5 … 4 × the note.
                const float ratio = .5f * std::exp2(3.f * colour);
                const float dm = std::min(dt * ratio, .45f);
                const float m0 = modPhase_;
                float m1 = m0 + dm;
                if (m0 < .5f && m1 >= .5f) {
                    const float at = (.5f - m0) / dm;
                    step(at, -2.f * triangle(p0 + dt * at - (p0 + dt * at >= 1.f ? 1.f : 0.f)));
                }
                if (m1 >= 1.f) {
                    const float at = (1.f - m0) / dm;
                    step(at, 2.f * triangle(p0 + dt * at - (p0 + dt * at >= 1.f ? 1.f : 0.f)));
                    m1 -= 1.f;
                }
                modPhase_ = m1;
                now = triangle(p1) * (m1 < .5f ? 1.f : -1.f);
                break;
            }
            default:
                break;
            }
            if (bits < 4096.f) now = std::round(now * bits * .5f) / (bits * .5f);   // the DAC's grit

            // One sample of latency: the step's correction reaches back to the previous sample.
            const float voice = (pending_ + blepPrev_) * gain;
            pending_ = now + blepNow_;

            // Filter (6581: the input and the band-pass state saturate).
            float x = voice;
            if (old) x = std::tanh(1.3f * x) / 1.3f;
            float y = x;
            if (mode != kFilterOff) {
                const float hp = (x - (k + g) * bp_ - lp_) * h;
                const float b = g * hp + bp_;
                bp_ = g * hp + b;
                if (old) bp_ = std::tanh(bp_);
                const float l = g * b + lp_;
                lp_ = g * b + l;
                // Band pass scaled by √k: a narrow band keeps about the loudness of a wide one.
                y = mode == kLowPass ? l : mode == kBandPass ? b * std::sqrt(k) : hp;
            }
            // The output stage blocks DC (combined and pulse waves carry an offset).
            const float dcFree = y - dcIn_ + .9987f * dcOut_;
            dcIn_ = y;
            dcOut_ = dcFree;
            out[i] = dcFree;
        }
        if (!std::isfinite(lp_) || !std::isfinite(bp_) || !std::isfinite(dcOut_)) lp_ = bp_ = dcIn_ = dcOut_ = 0.f;
    }

    int arp_step() const { return arpStep_; }

private:
    // Level per wave so that each sits near the same loudness before the filter.
    static constexpr float kWaveGain[kWaveCount] = {.42f, .42f, .26f, .42f, .555f, .3f, .51f, .44f, .42f};
    static constexpr int kChordSize[kChordCount] = {1, 2, 3, 3, 3, 4, 4, 3, 4};
    static constexpr int kChords[kChordCount][4] = {{0, 0, 0, 0},  {0, 12, 0, 0}, {0, 7, 12, 0},
                                                    {0, 4, 7, 0},  {0, 3, 7, 0},  {0, 4, 7, 11},
                                                    {0, 3, 7, 10}, {0, 5, 7, 0},  {0, 3, 6, 9}};

    static float triangle(float p) { return p < .5f ? 4.f * p - 1.f : 3.f - 4.f * p; }

    // A jump of `height` at `at` (0..1) of the way from the previous sample to this one.
    void step(float at, float height) {
        // Residual of a two-sample band-limited step, t measured from the jump in samples:
        // (t+1)²/2 for the sample before (t = −at), −(1−t)²/2 for the sample after (t = 1−at).
        at = std::clamp(at, 0.f, 1.f);
        blepPrev_ += height * .5f * (1.f - at) * (1.f - at);
        blepNow_ -= height * .5f * at * at;
    }
    // The pulse gate: opens (+open) when the phase crosses `low`, closes (+close) at the wrap.
    void edge(float p0, float dt, float low, bool wrapped, float wrapAt, float open, float close) {
        const float end = p0 + dt;
        if (p0 < low && end >= low) step((low - p0) / dt, open);
        if (wrapped) {
            step(wrapAt, close);
            if (low < end - 1.f) step((1.f + low - p0) / dt, open);   // a very thin gap: both in one sample
        }
    }

    void clock_lfsr() {
        const uint32_t bit = ((lfsr_ >> 22) ^ (lfsr_ >> 17)) & 1u;
        lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFFu;
    }
    // The chip's noise output: eight register bits as one byte.
    float noise_out() const {
        const uint32_t r = lfsr_;
        const uint32_t byte = ((r >> 22) & 1u) << 7 | ((r >> 20) & 1u) << 6 | ((r >> 16) & 1u) << 5 |
                              ((r >> 13) & 1u) << 4 | ((r >> 11) & 1u) << 3 | ((r >> 7) & 1u) << 2 |
                              ((r >> 4) & 1u) << 1 | ((r >> 2) & 1u);
        return float(byte) / 127.5f - 1.f;
    }

    // Saw+Tri: the 12-bit saw ANDed with the 12-bit triangle, as band-limited tables. Level l keeps
    // the harmonics up to 512 >> l (l = 0 … 9); the table is read with linear interpolation.
    static constexpr int kTableSize = 2048;
    static constexpr int kTableLevels = 10;
    struct Tables {
        float data[kTableLevels][kTableSize + 1];
        Tables() {
            constexpr int kSuper = 16384;   // the AND wave sampled finely before the analysis
            static float wave[kSuper];
            static float cosTable[kSuper];
            for (int n = 0; n < kSuper; ++n) {
                const uint32_t acc = uint32_t((uint64_t(n) << 24) / kSuper);
                const uint32_t saw = acc >> 12;
                const uint32_t tri = ((acc & 0x800000u) ? ~acc : acc) >> 11 & 0xFFFu;
                wave[n] = float(saw & tri) / 2047.5f - 1.f;
                cosTable[n] = float(std::cos(6.283185307179586 * n / kSuper));
            }
            for (auto& level : data) std::fill(std::begin(level), std::end(level), 0.f);
            for (int harmonic = 1; harmonic <= 512; ++harmonic) {
                double re = 0, im = 0;
                for (int n = 0; n < kSuper; ++n) {
                    const int index = int((int64_t(harmonic) * n) % kSuper);
                    re += wave[n] * cosTable[index];
                    im += wave[n] * cosTable[(index + kSuper - kSuper / 4) % kSuper];
                }
                const float a = float(2.0 * re / kSuper), b = float(2.0 * im / kSuper);
                for (int n = 0; n < kTableSize; ++n) {
                    const int index = int((int64_t(harmonic) * n * (kSuper / kTableSize)) % kSuper);
                    const float v = a * cosTable[index] + b * cosTable[(index + kSuper - kSuper / 4) % kSuper];
                    for (int l = 0; l < kTableLevels && harmonic <= (512 >> l); ++l) data[l][n] += v;
                }
            }
            for (auto& level : data) level[kTableSize] = level[0];
        }
    };
    static const Tables& tables() {
        static const Tables shared;
        return shared;
    }
    // The finest table whose highest harmonic stays below 0.45 of the rate at this pitch.
    static float tables_level(float hz) {
        const float allowed = kRate * .45f / std::max(hz, 1.f);
        int l = 0;
        while (l < kTableLevels - 1 && float(512 >> l) > allowed) ++l;
        return float(l);
    }
    static float read_table(float level, float p) {
        const float* t = tables().data[int(level)];
        const float x = p * kTableSize;
        const int i = std::min(int(x), kTableSize - 1);
        return t[i] + (t[i + 1] - t[i]) * (x - float(i));
    }

    float phase_ = 0.f, modPhase_ = 0.f, carrier_ = 0.f;
    uint32_t lfsr_ = 0x7FFFF8u;
    float noiseValue_ = 0.f, pending_ = 0.f, blepPrev_ = 0.f, blepNow_ = 0.f;
    float lp_ = 0.f, bp_ = 0.f, dcIn_ = 0.f, dcOut_ = 0.f, pwmPhase_ = 0.f;
    int frameCount_ = 0, arpStep_ = 0, untilFrame_ = 0;
};

}  // namespace studio
