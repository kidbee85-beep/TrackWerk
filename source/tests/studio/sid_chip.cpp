// SID CHIP (model 120, Studio > Synth > Vintage): measured through studio::Synth at 48 kHz with an
// 8192-point Hann FFT (pt-kissfft). Each check names the property a chip musician would hear.
#include "kiss_fftr.h"
#include "sid_chip.h"
#include "synth.h"
#include "synth_params.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <array>
#include <string>
#include <vector>

using namespace studio;

namespace {

constexpr float kRate = 48000.f;
constexpr int kFft = 8192;

std::vector<float> render(int code, float hz, const SynthParams& params, int frames, int skip) {
    Synth s;
    s.start(code);
    s.update(code, params);
    std::vector<float> out;
    for (int i = 0; i < skip + frames; ++i) {
        const float v = s.next(hz, kRate);
        assert(std::isfinite(v));
        if (i >= skip) out.push_back(v);
    }
    return out;
}
std::vector<double> spectrum(const std::vector<float>& x, size_t from = 0) {
    kiss_fftr_cfg cfg = kiss_fftr_alloc(kFft, 0, nullptr, nullptr);
    std::vector<float> w(kFft);
    for (int i = 0; i < kFft; ++i) w[size_t(i)] = x[from + size_t(i)] * (.5f - .5f * std::cos(6.2831853f * i / (kFft - 1)));
    std::vector<kiss_fft_cpx> bins(kFft / 2 + 1);
    kiss_fftr(cfg, w.data(), bins.data());
    kiss_fftr_free(cfg);
    std::vector<double> p(bins.size());
    for (size_t k = 0; k < bins.size(); ++k) p[k] = double(bins[k].r) * bins[k].r + double(bins[k].i) * bins[k].i;
    return p;
}
int bin_of(float hz) { return int(std::lround(hz * kFft / kRate)); }
double range(const std::vector<double>& p, float lo, float hi) {
    double e = 0;
    for (int k = std::max(0, bin_of(lo)); k <= std::min(int(p.size()) - 1, bin_of(hi)); ++k) e += p[size_t(k)];
    return e;
}
double total(const std::vector<double>& p) { double e = 0; for (double v : p) e += v; return e; }
double centroid(const std::vector<double>& p, float lo, float hi) {
    double num = 0, den = 0;
    for (int k = std::max(1, bin_of(lo)); k <= std::min(int(p.size()) - 1, bin_of(hi)); ++k) {
        num += k * kRate / kFft * p[size_t(k)];
        den += p[size_t(k)];
    }
    return num / den;
}
double harmonic(const std::vector<double>& p, float hz, int n) { return range(p, hz * n - 15.f, hz * n + 15.f); }
// Energy away from every harmonic of hz (more than 20 Hz off), relative to all of it.
double off_harmonic(const std::vector<double>& p, float hz) {
    double off = 0;
    for (size_t k = 1; k < p.size(); ++k) {
        const double f = k * kRate / kFft;
        const double nearest = std::max(1.0, std::round(f / hz)) * hz;
        if (std::fabs(f - nearest) > 20.0) off += p[k];
    }
    return off / total(p);
}
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double db(double powerRatio) { return 10 * std::log10(std::max(powerRatio, 1e-30)); }

int wave_value(int wave) { return wave * 28 + 14; }
// Wave, chip (0/1), filter mode, resonance, PWM, arp speed in frames.
SynthParams params(int wave, int chip = 1, int mode = SidChip::kFilterOff, int resonance = 0, int pwm = 0, int speed = 1) {
    auto p = default_synth_params();
    p[2] = wave_value(wave);
    p[3] = chip * 128 + 64;
    p[4] = mode * 64 + 32;
    p[5] = resonance;
    p[6] = pwm;
    p[7] = ((speed - 1) * 255 + 6) / 7;
    return p;
}
int chord(int index) { return int((index + .5f) * 255.f / 9.f); }
int custom(int colour, int arp, int cutoff) { return custom_code(kSidChipModel, colour, arp, cutoff); }

}  // namespace

int main() {
    // Registration: the Vintage family's first model, three macros, eight parameters, sixteen named sounds.
    assert(kVintageFirstModel == kVoiceFirstModel + kVoiceModelCount && kSidChipModel == kVintageFirstModel);
    assert(model_count() == kVintageFirstModel + kVintageModelCount && is_vintage_model(kSidChipModel) &&
           !is_voice_model(kSidChipModel) && std::string(model_name(kSidChipModel)) == "SID CHIP");
    assert(std::string(macro_name(kSidChipModel, 0)) == "Colour" && std::string(macro_name(kSidChipModel, 1)) == "Arp" &&
           std::string(macro_name(kSidChipModel, 2)) == "Cutoff");
    assert(synth_param_count(kSidChipModel) == 8 && std::string(synth_param_name(kSidChipModel, 2)) == "Wave" &&
           std::string(synth_param_name(kSidChipModel, 3)) == "Chip" &&
           std::string(synth_param_name(kSidChipModel, 7)) == "Arp speed");
    for (int w = 0; w < SidChip::kWaveCount; ++w)
        assert(synth_param_value(kSidChipModel, 2, wave_value(w)) == SidChip::kWaveNames[w]);
    assert(synth_param_value(kSidChipModel, 3, 0) == "6581" && synth_param_value(kSidChipModel, 3, 255) == "8580" &&
           synth_param_value(kSidChipModel, 4, 0) == "Low pass" && synth_param_value(kSidChipModel, 4, 255) == "Off" &&
           synth_param_value(kSidChipModel, 7, 0) == "1 frame" && synth_param_value(kSidChipModel, 7, 255) == "8 frames" &&
           synth_param_value(kSidChipModel, 2, -1) == "Factory");
    // One press, one step; both ends held.
    assert(synth_param_adjust(kSidChipModel, 2, wave_value(0), 1, false) == wave_value(1));
    assert(synth_param_adjust(kSidChipModel, 2, wave_value(8), 1, true) == wave_value(8));
    assert(synth_param_adjust(kSidChipModel, 3, 64, 1, false) == 192 && synth_param_adjust(kSidChipModel, 3, 192, 1, false) == 192);
    assert(synth_param_adjust(kSidChipModel, 4, 32, -1, false) == 32 && synth_param_adjust(kSidChipModel, 4, 32, 1, false) == 96);
    for (int n = 1, v = 0; n <= 8; ++n) {
        assert(synth_param_value(kSidChipModel, 7, v) == std::to_string(n) + (n == 1 ? " frame" : " frames"));
        v = synth_param_adjust(kSidChipModel, 7, v, 1, false);
    }
    assert(preset_name(preset_code(kSidChipModel, 0)) == "SID CHIP / Lead Pulse" && !synth_needs_strike(kSidChipModel));

    // Every named sound plays, stays bounded and sits in one loudness range.
    {
        double quietest = 1, loudest = 0;
        for (int v = 0; v < variation_count(); ++v) {
            const auto x = render(preset_code(kSidChipModel, v), 196.f, default_synth_params(), 24000, 4800);
            double peak = 0;
            for (float s : x) peak = std::max(peak, double(std::fabs(s)));
            assert(peak < 1.0);
            quietest = std::min(quietest, rms(x));
            loudest = std::max(loudest, rms(x));
        }
        std::printf("16 sounds at G3: rms %.3f … %.3f (%.1f dB)\n", quietest, loudest, db(loudest * loudest / (quietest * quietest)));
        assert(quietest > .05 && db(loudest * loudest / (quietest * quietest)) < 9.0);
    }

    // The waveforms have their textbook harmonics (filter off, 8580): the saw's second harmonic is
    // −6 dB, the square has none, the triangle's third is −19 dB, a thin pulse keeps the second strong.
    {
        const float hz = 220.f;
        const auto saw = spectrum(render(custom(0, 0, 255), hz, params(SidChip::kSaw), kFft, 4800));
        const auto square = spectrum(render(custom(0, 0, 255), hz, params(SidChip::kPulse), kFft, 4800));
        const auto tri = spectrum(render(custom(0, 0, 255), hz, params(SidChip::kTriangle), kFft, 4800));
        const auto thin = spectrum(render(custom(255, 0, 255), hz, params(SidChip::kPulse), kFft, 4800));
        const double saw2 = harmonic(saw, hz, 2) / harmonic(saw, hz, 1), square2 = harmonic(square, hz, 2) / harmonic(square, hz, 1),
                     square3 = harmonic(square, hz, 3) / harmonic(square, hz, 1), tri3 = harmonic(tri, hz, 3) / harmonic(tri, hz, 1),
                     thin2 = harmonic(thin, hz, 2) / harmonic(thin, hz, 1);
        std::printf("Power ratios at A3: saw H2 %.3f, square H2 %.4f H3 %.3f, triangle H3 %.4f, thin pulse H2 %.3f\n", saw2,
                    square2, square3, tri3, thin2);
        assert(saw2 > .2 && saw2 < .3);
        assert(square2 < .005 && square3 > .09 && square3 < .13);
        assert(tri3 > .009 && tri3 < .016);
        assert(thin2 > .8);
    }

    // Band-limited: a saw and a square on E7 put almost nothing between their harmonics.
    {
        const float hz = 2637.f;
        const double saw = off_harmonic(spectrum(render(custom(0, 0, 255), hz, params(SidChip::kSaw), kFft, 4800)), hz);
        const double pulse = off_harmonic(spectrum(render(custom(120, 0, 255), hz, params(SidChip::kPulse), kFft, 4800)), hz);
        const double sync = off_harmonic(spectrum(render(custom(100, 0, 255), hz, params(SidChip::kSync), kFft, 4800)), hz);
        const double combo = off_harmonic(spectrum(render(custom(0, 0, 255), hz, params(SidChip::kSawTri), kFft, 4800)), hz);
        std::printf("Fold-back on E7 (energy off the harmonics): saw %.1e, pulse %.1e, sync %.1e, saw+tri %.1e\n", saw, pulse,
                    sync, combo);
        assert(saw < 3e-3 && pulse < 3e-3 && sync < 1e-2 && combo < 1e-4);
    }

    // Arp: a major chord as a tracker arpeggio — each 50 Hz frame plays the next note: root, third, fifth.
    {
        const float hz = 220.f;
        const auto x = render(custom(0, chord(3), 255), hz, params(SidChip::kTriangle), 960 * 9, 960 * 3);
        // The pitch of one frame from its upward zero crossings (skipping the frame's first 120 samples).
        const auto pitch = [&](const std::vector<float>& y, int frame) {
            double first = -1, last = -1;
            int count = 0;
            for (int i = frame * 960 + 120; i < (frame + 1) * 960; ++i) {
                if (y[size_t(i) - 1] < 0 && y[size_t(i)] >= 0) {
                    const double t = i - 1 + y[size_t(i) - 1] / (y[size_t(i) - 1] - y[size_t(i)]);
                    if (first < 0) first = t;
                    last = t;
                    ++count;
                }
            }
            return count > 1 ? (count - 1) * kRate / (last - first) : 0.0;
        };
        std::printf("Major arp on A3, frame by frame:");
        for (int frame = 0; frame < 9; ++frame) {
            const double expected = hz * std::exp2(std::array<int, 3>{0, 4, 7}[size_t(frame % 3)] / 12.0);
            const double measured = pitch(x, frame);
            std::printf(" %.1f", measured);
            assert(std::fabs(measured / expected - 1) < .01);
        }
        std::printf(" Hz\n");
        const auto off = render(custom(0, chord(0), 255), hz, params(SidChip::kTriangle), 960 * 3, 960 * 3);
        for (int frame = 0; frame < 3; ++frame) assert(std::fabs(pitch(off, frame) / hz - 1) < .01);
        // Speed: one step per `speed` frames of 50 Hz.
        SidChip chip;
        std::vector<float> block(960);
        for (int frame = 0; frame < 12; ++frame) chip.render(block.data(), 960, hz, 0, chord(3) / 255.f, 1, 0, 1, 3, 0, 0, 4);
        assert(chip.arp_step() == 2);   // frames 0–3 the root, 4–7 the third, 8–11 the fifth
    }

    // Filter: low pass at a low cutoff removes the highs; resonance lifts the region at the cutoff.
    {
        const float hz = 110.f;
        const auto open = spectrum(render(custom(0, 0, 255), hz, params(SidChip::kSaw, 1, SidChip::kLowPass), kFft, 4800));
        const auto closed = spectrum(render(custom(0, 0, 60), hz, params(SidChip::kSaw, 1, SidChip::kLowPass), kFft, 4800));
        const double highOpen = range(open, 2000, 20000) / total(open), highClosed = range(closed, 2000, 20000) / total(closed);
        const auto flat = spectrum(render(custom(0, 0, 128), hz, params(SidChip::kSaw, 1, SidChip::kLowPass, 0), kFft, 4800));
        const auto peaked = spectrum(render(custom(0, 0, 128), hz, params(SidChip::kSaw, 1, SidChip::kLowPass, 255), kFft, 4800));
        const float fc = 30.f * std::exp2(8.6f * 128 / 255.f);
        const double lift = (range(peaked, fc * .8f, fc * 1.25f) / total(peaked)) / (range(flat, fc * .8f, fc * 1.25f) / total(flat));
        std::printf("Low pass: energy above 2 kHz open %.3f, closed %.5f; resonance lifts %.0f Hz by %.1f×\n", highOpen,
                    highClosed, fc, lift);
        assert(highClosed < highOpen / 50 && lift > 2.0);
        const auto high = spectrum(render(custom(0, 0, 128), hz, params(SidChip::kSaw, 1, SidChip::kHighPass), kFft, 4800));
        // High pass: the fundamental and the low harmonics go, the highs stay.
        assert(range(high, 50, 200) < range(open, 50, 200) / 20 && centroid(high, 50, 12000) > centroid(open, 50, 12000));
    }

    // The 6581 saturates where the 8580 stays clean.
    {
        const auto clean = render(custom(0, 0, 255), 220.f, params(SidChip::kTriangle, 1, SidChip::kFilterOff), kFft, 4800);
        const auto warm = render(custom(0, 0, 255), 220.f, params(SidChip::kTriangle, 0, SidChip::kFilterOff), kFft, 4800);
        const auto pc = spectrum(clean), pw = spectrum(warm);
        const double odd5Clean = harmonic(pc, 220.f, 5) / harmonic(pc, 220.f, 1), odd5Warm = harmonic(pw, 220.f, 5) / harmonic(pw, 220.f, 1);
        std::printf("Triangle H5 / H1: 8580 %.5f, 6581 %.5f\n", odd5Clean, odd5Warm);
        assert(std::fabs(odd5Warm - odd5Clean) > .1 * odd5Clean);
    }

    // Noise: broadband, and brighter on a higher note (the chip clocks it with the oscillator).
    {
        const auto low = spectrum(render(custom(0, 0, 255), 65.4f, params(SidChip::kNoise), kFft, 4800));
        const auto high = spectrum(render(custom(0, 0, 255), 1046.5f, params(SidChip::kNoise), kFft, 4800));
        std::printf("Noise centroid: C2 %.0f Hz, C6 %.0f Hz\n", centroid(low, 20, 20000), centroid(high, 20, 20000));
        assert(centroid(high, 20, 20000) > 3 * centroid(low, 20, 20000));
        assert(off_harmonic(high, 1046.5f) > .5);
    }

    // Sync brightens with its ratio and stays harmonic; Ring is inharmonic, as a bell.
    {
        const float hz = 220.f;
        const auto sync1 = spectrum(render(custom(0, 0, 255), hz, params(SidChip::kSync), kFft, 4800));
        const auto sync5 = spectrum(render(custom(128, 0, 255), hz, params(SidChip::kSync), kFft, 4800));
        assert(centroid(sync5, 50, 12000) > 2 * centroid(sync1, 50, 12000));
        assert(off_harmonic(sync5, hz) < .02);
        const auto ring = spectrum(render(custom(128, 0, 255), hz, params(SidChip::kRing), kFft, 4800));
        std::printf("Ring at colour 50 %%: %.2f of the energy off the harmonics\n", off_harmonic(ring, hz));
        assert(off_harmonic(ring, hz) > .3);
    }

    // Colour on a saw lowers the DAC to 4 bits: the steps of the staircase can be heard.
    {
        const float hz = 220.f;
        const auto clean = spectrum(render(custom(0, 0, 255), hz, params(SidChip::kSaw), kFft, 4800));
        const auto gritty = spectrum(render(custom(255, 0, 255), hz, params(SidChip::kSaw), kFft, 4800));
        // Sixteen steps per cycle: the staircase adds a strong 16th harmonic on top of the saw's own.
        const double h16 = harmonic(gritty, hz, 16) / harmonic(clean, hz, 16);
        std::printf("4-bit saw: 16th harmonic %.1f× the 12-bit saw's\n", h16);
        assert(h16 > 3);
    }

    // Click-free: jumping the cutoff from closed to open is no steeper than the open sound itself.
    {
        const auto p = params(SidChip::kPulse, 1, SidChip::kLowPass, 128);
        const auto steepest = [&](int cutoff) {
            const auto x = render(custom(60, 0, cutoff), 196.f, p, 9600, 4800);
            float worst = 0;
            for (size_t i = 1; i < x.size(); ++i) worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
            return worst;
        };
        const float steady = std::max(steepest(20), steepest(255));
        Synth s;
        s.start(custom(60, 0, 20));
        s.update(custom(60, 0, 20), p);
        float last = 0, worst = 0;
        for (int i = 0; i < 9600; ++i) last = s.next(196.f, kRate);
        s.update(custom(60, 0, 255), p);
        for (int i = 0; i < 4800; ++i) {
            const float v = s.next(196.f, kRate);
            worst = std::max(worst, std::fabs(v - last));
            last = v;
        }
        std::printf("Cutoff jump: steepest step %.3f, steady %.3f\n", worst, steady);
        assert(worst < 1.15f * steady);
    }

    // Reproducible: the same note renders the same samples (the noise included).
    {
        const auto x = render(preset_code(kSidChipModel, 6), 220.f, default_synth_params(), 9600, 0);
        const auto y = render(preset_code(kSidChipModel, 6), 220.f, default_synth_params(), 9600, 0);
        assert(x == y);
    }

    std::puts("PASS: SID CHIP registered (Vintage family, 3 macros, 8 parameters, 16 sounds, stepping), all sounds bounded "
              "within 9 dB, textbook harmonics per wave, band-limited on E7, arp chords and speed, filter cut and "
              "resonance, 6581 saturation, pitch-clocked noise, sync and ring, DAC grit, click-free cutoff jumps, "
              "reproducible");
    return 0;
}
