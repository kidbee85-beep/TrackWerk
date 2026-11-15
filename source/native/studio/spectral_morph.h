#pragma once
// SP MORPH — Spectral family, model 118 (TrackWerk, Kid Bunka, GPL-3.0).
//
// Morphs between two spectral shapes. Not a crossfade of two sounds: the shapes' parameters glide —
// formant frequencies along a log scale, brightness, the share of even harmonics and the
// inharmonicity — so "A" really travels to "O" through the vowels between, a saw opens into a bell.
//   macro A  Morph       position from shape A (0) to shape B (255)
//   macro B  Motion      sweeps the position around A, back and forth; 0 = still
//   macro C  Warp        shifts every formant by up to an octave down (0) or up (255); centre = as drawn
//   param 2  Shape A     one of 16 shapes (kShapeNames)
//   param 3  Shape B
//   param 4  Partials    8 … 128 (default 64)
//   param 5  Motion rate 0.05 … 4 Hz
// Synthesis as in SP ADDITIVE: up to 128 partials as complex rotators, shaped once per 96 samples and
// ramped across them (click-free), faded out below 0.42 of the rate (alias-free), normalised by the
// spectrum's energy (morphing does not change loudness). Fixed size, no allocation, no locks.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace studio {

class SpectralMorph {
public:
    static constexpr int kMaxPartials = 128;
    static constexpr int kShapeCount = 16;
    static constexpr float kRate = 48000.f;
    static constexpr int kUpdateFrames = 96;   // shape update interval: 2 ms

    // A spectral shape: amplitude ∝ k^-tilt, even harmonics × even, stretch β for f_k = f0·k·√(1+βk²),
    // and up to three formant peaks (Hz, extra gain, width in octaves; gain 0 = none).
    struct Shape {
        float tilt, even, stretch;
        float formantHz[3], formantGain[3], formantWidth[3];
    };
    static constexpr const char* kShapeNames[kShapeCount] = {
        "Saw", "Square", "Organ", "Vowel A", "Vowel E", "Vowel I", "Vowel O", "Vowel U",
        "Bell", "Glass", "Metal", "Reed", "Brass", "Strings", "Hollow", "Flute"};

    static const Shape& shape(int index) {
        // Formant gains are linear boosts on top of the tilt (6 ≈ +17 dB). Shapes without formants keep
        // neutral frequencies, so a morph towards a vowel grows its peaks in place.
        static constexpr Shape shapes[kShapeCount] = {
            {1.00f, 1.00f, 0.f,     {500, 1500, 2500}, {0, 0, 0},       {.5f, .5f, .5f}},   // Saw
            {1.00f, 0.00f, 0.f,     {500, 1500, 2500}, {0, 0, 0},       {.5f, .5f, .5f}},   // Square
            {1.80f, 1.00f, 0.f,     {500, 1500, 2500}, {0, 0, 0},       {.5f, .5f, .5f}},   // Organ
            {1.35f, 1.00f, 0.f,     {800, 1150, 2900}, {8, 6, 2.5f},    {.22f, .22f, .3f}}, // Vowel A
            {1.35f, 1.00f, 0.f,     {400, 1600, 2700}, {8, 5, 3},       {.22f, .25f, .3f}}, // Vowel E
            {1.35f, 1.00f, 0.f,     {270, 2300, 3000}, {8, 4, 3},       {.2f, .25f, .3f}},  // Vowel I
            {1.35f, 1.00f, 0.f,     {450, 800, 2830},  {8, 6, 1.5f},    {.22f, .22f, .3f}}, // Vowel O
            {1.35f, 1.00f, 0.f,     {325, 700, 2530},  {8, 4, 1},       {.2f, .22f, .3f}},  // Vowel U
            {0.85f, 1.00f, .0045f,  {500, 1500, 2500}, {0, 0, 0},       {.5f, .5f, .5f}},   // Bell
            {0.70f, 1.00f, .0010f,  {500, 1500, 3200}, {0, 0, 3},       {.5f, .5f, .5f}},   // Glass
            {0.55f, 0.70f, .0090f,  {500, 2200, 4500}, {0, 2, 3},       {.5f, .4f, .4f}},   // Metal
            {1.10f, 0.30f, 0.f,     {500, 1250, 2500}, {0, 5, 0},       {.5f, .35f, .5f}},  // Reed
            {0.80f, 1.00f, 0.f,     {500, 1250, 2500}, {0, 4, 2},       {.5f, .5f, .5f}},   // Brass
            {1.05f, 1.00f, 0.f,     {500, 1500, 2600}, {0, 2, 4},       {.5f, .4f, .35f}},  // Strings
            {1.40f, 0.00f, 0.f,     {500, 1500, 2500}, {0, 0, 0},       {.5f, .5f, .5f}},   // Hollow
            {2.60f, 0.40f, 0.f,     {500, 1500, 2500}, {0, 0, 0},       {.5f, .5f, .5f}},   // Flute
        };
        return shapes[std::clamp(index, 0, kShapeCount - 1)];
    }

    SpectralMorph() {
        for (int k = 0; k < kMaxPartials; ++k) logK_[k] = std::log(float(k + 1));
        start();
    }

    // A new note: partials restart at fixed, spread phases (low crest factor, reproducible).
    void start() {
        for (int k = 0; k < kMaxPartials; ++k) {
            const float phase = 6.2831853f * float((k * 0.618034f) - std::floor(k * 0.618034f));
            re_[k] = std::cos(phase);
            im_[k] = std::sin(phase);
            amp_[k] = ampStep_[k] = 0.f;
        }
        lastHz_ = lastStretch_ = -1.f;
        lastCount_ = -1;
        untilUpdate_ = 0;
        lfo_ = 0.f;
    }

    // `morph`, `motion`, `warp` are the macros 0..1; the rest the Studio's resolved 0..255 values.
    void render(float* out, int frames, float hz, float morph, float motion, float warp,
                int shapeA, int shapeB, int partials, int motionRate) {
        if (untilUpdate_ <= 0) {
            update(std::max(hz, 1.f), morph, motion, warp, shapeA, shapeB, std::clamp(partials, 8, kMaxPartials),
                   motionRate);
            untilUpdate_ = kUpdateFrames;
        }
        untilUpdate_ -= frames;

        // Two passes per sample, both vectorisable without -ffast-math (as SP ADDITIVE): rotate and
        // weight every partial, then sum in eight independent lanes.
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
        for (int k = 0; k < n; ++k) {   // keep every rotator on the unit circle
            const float g = 1.5f - .5f * (re_[k] * re_[k] + im_[k] * im_[k]);
            re_[k] *= g;
            im_[k] *= g;
        }
    }

    // The position the last update used (0..1), for tests and the display.
    float position() const { return position_; }

private:
    void update(float hz, float morph, float motion, float warp, int shapeA, int shapeB, int wanted, int motionRate) {
        // Motion: a triangle around the macro position, reflected back into 0..1.
        const float rateHz = .05f * std::exp2(6.32f * std::clamp(motionRate, 0, 255) / 255.f);   // 0.05 … 4 Hz
        lfo_ += rateHz * float(kUpdateFrames) / kRate;
        lfo_ -= std::floor(lfo_);
        const float tri = lfo_ < .5f ? 4.f * lfo_ - 1.f : 3.f - 4.f * lfo_;   // −1 … 1
        float x = std::clamp(morph, 0.f, 1.f) + .5f * std::clamp(motion, 0.f, 1.f) * tri;
        if (x < 0.f) x = -x;
        if (x > 1.f) x = 2.f - x;
        position_ = x;

        const Shape& a = shape(shapeA);
        const Shape& b = shape(shapeB);
        const auto mix = [x](float p, float q) { return p + (q - p) * x; };
        const float tilt = mix(a.tilt, b.tilt), even = mix(a.even, b.even), stretch = mix(a.stretch, b.stretch);
        const float warpFactor = std::exp2(2.f * (std::clamp(warp, 0.f, 1.f) - .5f));   // ½ … 2
        float fHz[3], fGain[3], fWidth[3];
        for (int i = 0; i < 3; ++i) {
            fHz[i] = std::exp2(mix(std::log2(a.formantHz[i]), std::log2(b.formantHz[i]))) * warpFactor;
            fGain[i] = mix(a.formantGain[i], b.formantGain[i]);
            fWidth[i] = mix(a.formantWidth[i], b.formantWidth[i]);
        }

        update_frequencies(hz, stretch, wanted);

        float energy = 0.f;
        for (int k = 0; k < count_; ++k) {
            float g = std::exp(-tilt * logK_[k]);
            if (((k + 1) & 1) == 0) g *= even;
            float boost = 1.f;
            for (int i = 0; i < 3; ++i) {
                if (fGain[i] <= 0.f) continue;
                const float octaves = std::log2(freq_[k] / fHz[i]) / fWidth[i];
                boost += fGain[i] * std::exp(-.5f * octaves * octaves);
            }
            g *= boost;
            g *= std::clamp((kRate * .42f - freq_[k]) / (kRate * .042f), 0.f, 1.f);   // band edge
            target_[k] = g;
            energy += g * g;
        }
        const float norm = energy > 0.f ? .3f / std::sqrt(energy) : 0.f;
        const float inv = 1.f / float(kUpdateFrames);
        for (int k = 0; k < count_; ++k) ampStep_[k] = (target_[k] * norm - amp_[k]) * inv;
    }

    void update_frequencies(float hz, float stretch, int wanted) {
        // Recomputed only when pitch, the partial count or the stretch actually moved.
        if (hz == lastHz_ && wanted == lastCount_ && std::fabs(stretch - lastStretch_) < 1e-6f) return;
        lastHz_ = hz;
        lastStretch_ = stretch;
        lastCount_ = wanted;
        int n = 0;
        for (int k = 1; k <= wanted; ++k) {
            const float f = hz * k * std::sqrt(1.f + stretch * float(k * k));
            freq_[k - 1] = f;
            if (f < kRate * .42f) n = k;
        }
        // A partial no longer rendered starts silent if it comes back; the padding to 8 stays silent.
        for (int k = n; k < kMaxPartials; ++k) amp_[k] = ampStep_[k] = 0.f;
        count_ = n;
        for (int k = 0; k < n; ++k) {
            const float w = 6.2831853f * freq_[k] / kRate;
            cr_[k] = std::cos(w);
            ci_[k] = std::sin(w);
        }
    }

    alignas(16) float re_[kMaxPartials]{}, im_[kMaxPartials]{}, cr_[kMaxPartials]{}, ci_[kMaxPartials]{};
    alignas(16) float amp_[kMaxPartials]{}, ampStep_[kMaxPartials]{}, target_[kMaxPartials]{}, weighted_[kMaxPartials]{};
    float freq_[kMaxPartials]{}, logK_[kMaxPartials]{};
    float lastHz_ = -1.f, lastStretch_ = -1.f, lfo_ = 0.f, position_ = 0.f;
    int lastCount_ = -1, count_ = 0, untilUpdate_ = 0;
};

}  // namespace studio
