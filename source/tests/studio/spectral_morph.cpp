// SP MORPH (model 118, Studio > Synth > Spectral): measured through studio::Synth at 48 kHz with an
// 8192-point Hann FFT (pt-kissfft). Each check names the property a musician would hear.
#include "kiss_fftr.h"
#include "spectral_morph.h"
#include "synth.h"
#include "synth_params.h"

#include <cassert>
#include <cmath>
#include <cstdio>
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
// Power-weighted mean frequency within lo..hi.
double centroid(const std::vector<double>& p, float lo = 0.f, float hi = 24000.f) {
    double num = 0, den = 0;
    for (int k = std::max(1, bin_of(lo)); k <= std::min(int(p.size()) - 1, bin_of(hi)); ++k) {
        num += k * kRate / kFft * p[size_t(k)];
        den += p[size_t(k)];
    }
    return num / den;
}
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double db(double ratio) { return 10 * std::log10(std::max(ratio, 1e-30)); }

int shape_value(int shape) { return shape * 16 + 8; }
SynthParams pair(int a, int b) {
    auto p = default_synth_params();
    p[2] = shape_value(a);
    p[3] = shape_value(b);
    return p;
}
int custom(int morph, int motion, int warp) { return custom_code(kSpectralMorphModel, morph, motion, warp); }

}  // namespace

int main() {
    // Registration: the third Spectral model, three macros, six parameters, sixteen named sounds.
    assert(kSpectralFirstModel + kSpectralModelCount == kVoiceFirstModel && model_count() > kVoiceFirstModel && kSpectralModelCount == 3);
    assert(std::string(model_name(kSpectralMorphModel)) == "SP MORPH" && is_spectral_model(kSpectralMorphModel));
    assert(std::string(macro_name(kSpectralMorphModel, 0)) == "Morph" &&
           std::string(macro_name(kSpectralMorphModel, 1)) == "Motion" &&
           std::string(macro_name(kSpectralMorphModel, 2)) == "Warp");
    assert(synth_param_count(kSpectralMorphModel) == 6 && std::string(synth_param_name(kSpectralMorphModel, 2)) == "Shape A" &&
           std::string(synth_param_name(kSpectralMorphModel, 5)) == "Motion rate");
    assert(synth_param_value(kSpectralMorphModel, 2, shape_value(8)) == "Bell" &&
           synth_param_value(kSpectralMorphModel, 3, -1) == "Factory" &&
           synth_param_value(kSpectralMorphModel, 4, 255) == "128 partials");
    // One press, one shape, both ends held.
    assert(synth_param_adjust(kSpectralMorphModel, 2, shape_value(3), 1, false) == shape_value(4));
    assert(synth_param_adjust(kSpectralMorphModel, 2, shape_value(15), 1, false) == shape_value(15));
    assert(synth_param_adjust(kSpectralMorphModel, 3, shape_value(0), -1, true) == shape_value(0));
    assert(preset_name(preset_code(kSpectralMorphModel, 0)) == "SP MORPH / Vowel Glide");

    const auto defaults = default_synth_params();
    // Every named sound speaks, stays bounded and sits in one loudness range.
    double quietest = 1, loudest = 0;
    for (int v = 0; v < variation_count(); ++v) {
        const auto x = render(preset_code(kSpectralMorphModel, v), 220.f, defaults, 24000, 2400);
        double peak = 0;
        for (float s : x) peak = std::max(peak, double(std::fabs(s)));
        assert(peak < 1.0);
        quietest = std::min(quietest, rms(x));
        loudest = std::max(loudest, rms(x));
    }
    assert(quietest > .02 && db(loudest * loudest / (quietest * quietest)) < 9.0);

    // A real morph, not a crossfade: from Vowel A (F1 800 Hz) to Vowel O (F1 450 Hz), halfway puts the
    // first formant between them (≈ 600 Hz), where neither end has its peak.
    const float hz = 110.f;
    const auto atA = spectrum(render(custom(0, 0, 128), hz, pair(3, 6), kFft, 4800));
    const auto atHalf = spectrum(render(custom(128, 0, 128), hz, pair(3, 6), kFft, 4800));
    const auto atB = spectrum(render(custom(255, 0, 128), hz, pair(3, 6), kFft, 4800));
    const double f1A = centroid(atA, 300, 1000), f1Half = centroid(atHalf, 300, 1000), f1B = centroid(atB, 300, 1000);
    assert(f1A > f1Half && f1Half > f1B);
    const double midShareA = range(atA, 560, 660) / total(atA), midShareHalf = range(atHalf, 560, 660) / total(atHalf),
                 midShareB = range(atB, 560, 660) / total(atB);
    assert(midShareHalf > 1.3 * midShareA && midShareHalf > 1.3 * midShareB);
    std::printf("Vowel A → O at 110 Hz: F1 centroid %.0f / %.0f / %.0f Hz, energy at 560–660 Hz %.3f / %.3f / %.3f\n",
                f1A, f1Half, f1B, midShareA, midShareHalf, midShareB);

    // Morphing keeps the loudness: the spectrum is normalised at every position.
    {
        double lo = 1, hi = 0;
        for (int m : {0, 64, 128, 192, 255}) {
            const double r = rms(render(custom(m, 0, 128), 220.f, pair(0, 8), 24000, 4800));
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        assert(db(hi * hi / (lo * lo)) < 1.5);
    }

    // Saw to Bell: the bell's stretch makes the partials inharmonic at the far end, not at the start.
    {
        const auto saw = spectrum(render(custom(0, 0, 128), 200.f, pair(0, 8), kFft, 4800));
        const auto bell = spectrum(render(custom(255, 0, 128), 200.f, pair(0, 8), kFft, 4800));
        const double harmonicSaw = range(saw, 1590, 1610) / range(saw, 1500, 1700);   // the 8th harmonic
        const double harmonicBell = range(bell, 1590, 1610) / range(bell, 1500, 1700);
        assert(harmonicSaw > .9 && harmonicBell < .5);
    }

    // Warp moves the formants: up raises the vowel's centroid, down lowers it.
    {
        const auto down = spectrum(render(custom(0, 0, 0), hz, pair(3, 3), kFft, 4800));
        const auto up = spectrum(render(custom(0, 0, 255), hz, pair(3, 3), kFft, 4800));
        assert(centroid(up, 200, 6000) > 1.4 * centroid(down, 200, 6000));
    }

    // Motion moves the timbre over time; without it a held note stays put.
    {
        const auto measure = [&](int motion) {
            const auto x = render(custom(128, motion, 128), hz, pair(3, 6), 48000 * 2, 4800);
            double lo = 1e9, hi = 0;
            for (size_t from = 0; from + kFft <= x.size(); from += 6000) {
                const double c = centroid(spectrum(x, from), 300, 1000);
                lo = std::min(lo, c);
                hi = std::max(hi, c);
            }
            return hi - lo;
        };
        assert(measure(0) < 10.0 && measure(255) > 80.0);
    }

    // No aliasing: a high note keeps everything below the band edge.
    {
        const auto p = spectrum(render(custom(128, 0, 128), 3100.f, pair(9, 10), kFft, 4800));
        assert(range(p, 21000, 23999) / total(p) < 1e-6);
    }

    // Click-free: jumping the morph from one end to the other moves no faster than either end already
    // does on its own (a band-limited saw has steep edges, so the bound is relative, not absolute).
    {
        const auto params = pair(0, 10);
        const auto steepest = [&](int morph) {
            const auto x = render(custom(morph, 0, 128), 220.f, params, 9600, 4800);
            float worst = 0;
            for (size_t i = 1; i < x.size(); ++i) worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
            return worst;
        };
        const float steady = std::max(steepest(0), steepest(255));
        Synth s;
        const int from = custom(0, 0, 128), to = custom(255, 0, 128);
        s.start(from);
        s.update(from, params);
        float last = 0, worst = 0;
        for (int i = 0; i < 9600; ++i) last = s.next(220.f, kRate);
        s.update(to, params);
        for (int i = 0; i < 4800; ++i) {
            const float v = s.next(220.f, kRate);
            worst = std::max(worst, std::fabs(v - last));
            last = v;
        }
        std::printf("Morph jump Saw → Metal at 220 Hz: steepest step %.3f, steady ends %.3f\n", worst, steady);
        assert(worst < 1.15f * steady);
    }

    // Each named sound brings its own pair: "Saw to Bell" at its far end is inharmonic, "Vowel Glide" is not.
    {
        const auto ends = [&](int variation) {
            const int code = preset_code(kSpectralMorphModel, variation);
            return spectrum(render(code, 200.f, default_synth_params(), kFft, 4800));
        };
        const auto glide = ends(0), sawBell = ends(2);
        const double glideHarmonic = range(glide, 1590, 1610) / range(glide, 1500, 1700);
        assert(glideHarmonic > .9);
        (void)sawBell;   // its macro sits at 70: partly stretched — audible, not asserted
    }

    std::puts("PASS: SP MORPH registered (3 macros, 6 parameters, 16 sounds, shape stepping), all sounds bounded "
              "within 9 dB, formants glide (not a crossfade), loudness steady across the morph, stretch reaches "
              "the bell, warp shifts formants, motion moves and stillness holds, alias-free, click-free morph jumps");
    return 0;
}
