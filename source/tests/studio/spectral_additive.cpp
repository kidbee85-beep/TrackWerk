// SP ADDITIVE (model 116, Studio > Synth > Spectral): measured through studio::Synth at 48 kHz with
// an 8192-point Hann FFT (pt-kissfft). Each check names the property a musician would hear.
#include "kiss_fftr.h"
#include "synth.h"
#include "synth_params.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// Power per FFT bin of the first kFft samples.
std::vector<double> spectrum(const std::vector<float>& x) {
    kiss_fftr_cfg cfg = kiss_fftr_alloc(kFft, 0, nullptr, nullptr);
    std::vector<float> w(kFft);
    for (int i = 0; i < kFft; ++i) w[size_t(i)] = x[size_t(i)] * (.5f - .5f * std::cos(6.2831853f * i / (kFft - 1)));
    std::vector<kiss_fft_cpx> bins(kFft / 2 + 1);
    kiss_fftr(cfg, w.data(), bins.data());
    kiss_fftr_free(cfg);
    std::vector<double> p(bins.size());
    for (size_t k = 0; k < bins.size(); ++k) p[k] = double(bins[k].r) * bins[k].r + double(bins[k].i) * bins[k].i;
    return p;
}
int bin_of(float hz) { return int(std::lround(hz * kFft / kRate)); }
double band(const std::vector<double>& p, float hz, int width = 3) {
    double e = 0; const int c = bin_of(hz);
    for (int k = std::max(0, c - width); k <= std::min(int(p.size()) - 1, c + width); ++k) e += p[size_t(k)];
    return e;
}
double total(const std::vector<double>& p) { double e = 0; for (double v : p) e += v; return e; }
double centroid(const std::vector<double>& p) {
    double num = 0, den = 0;
    for (size_t k = 1; k < p.size(); ++k) { num += k * kRate / kFft * p[k]; den += p[k]; }
    return num / den;
}
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double db(double ratio) { return 10 * std::log10(std::max(ratio, 1e-30)); }

SynthParams params_with(int index, int value) { auto p = default_synth_params(); p[size_t(index)] = value; return p; }
int custom(int a, int b, int c) { return custom_code(kSpectralAdditiveModel, a, b, c); }

}  // namespace

int main() {
    // Registration: the first Spectral model, three macros, six parameters, sixteen named sounds.
    assert(kSpectralFirstModel + kSpectralModelCount == kVoiceFirstModel && model_count() > kVoiceFirstModel);
    assert(std::string(model_name(kSpectralAdditiveModel)) == "SP ADDITIVE" && !synth_needs_strike(kSpectralAdditiveModel));
    assert(std::string(macro_name(kSpectralAdditiveModel, 0)) == "Brightness" &&
           std::string(macro_name(kSpectralAdditiveModel, 1)) == "Spectral comb" &&
           std::string(macro_name(kSpectralAdditiveModel, 2)) == "Stretch");
    assert(synth_param_count(kSpectralAdditiveModel) == 6 && std::string(synth_param_name(kSpectralAdditiveModel, 2)) == "Partials");
    assert(synth_param_value(kSpectralAdditiveModel, 2, 255) == "128 partials" && synth_param_value(kSpectralAdditiveModel, 4, 0) == "Off");
    assert(preset_name(preset_code(kSpectralAdditiveModel, 0)) == "SP ADDITIVE / Glass Organ");

    const auto defaults = default_synth_params();
    // Every named sound speaks, stays bounded and sits in one loudness range.
    double quietest = 1, loudest = 0;
    for (int v = 0; v < variation_count(); ++v) {
        const auto x = render(preset_code(kSpectralAdditiveModel, v), 220.f, defaults, 24000, 2400);
        double peak = 0; for (float s : x) peak = std::fmax(peak, std::fabs(s));
        const double level = rms(x);
        assert(peak < 1.5 && level > .05);
        quietest = std::min(quietest, level); loudest = std::max(loudest, level);
    }
    assert(db(loudest * loudest / (quietest * quietest)) < 9.0);

    // Harmonic at Stretch 0: ≥ 99.9 % of the energy sits on multiples of the note.
    {
        const auto p = spectrum(render(custom(150, 0, 0), 220.f, params_with(5, 0), kFft, 4800));
        double harmonic = 0; for (int k = 1; k * 220.f < kRate / 2; ++k) harmonic += band(p, 220.f * k);
        assert(harmonic / total(p) > .999);
    }
    // Brightness moves the spectral centroid up by more than an octave, loudness stays within 3 dB.
    {
        const auto dark = render(custom(0, 0, 0), 220.f, params_with(5, 0), kFft, 4800);
        const auto bright = render(custom(255, 0, 0), 220.f, params_with(5, 0), kFft, 4800);
        assert(centroid(spectrum(bright)) > 2 * centroid(spectrum(dark)));
        assert(std::fabs(db(rms(bright) * rms(bright) / (rms(dark) * rms(dark)))) < 3.0);
    }
    // Even harmonics 0: a hollow, odd-only tone (even partials ≥ 50 dB down).
    {
        auto p0 = default_synth_params(); p0[3] = 0; p0[5] = 0;
        const auto p = spectrum(render(custom(200, 0, 0), 220.f, p0, kFft, 4800));
        double odd = 0, even = 0;
        for (int k = 1; k * 220.f < 20000.f; ++k) (k % 2 ? odd : even) += band(p, 220.f * k);
        assert(db(even / odd) < -50.0);
    }
    // Stretch 1: the 10th partial moves from 1100 Hz to 1100·sqrt(1 + 0.0025·100) ≈ 1230 Hz (bell-like).
    // The harmonic position is read in its single bin: the 9th partial (≈1084 Hz) is 3 bins away.
    {
        const auto p = spectrum(render(custom(220, 0, 255), 110.f, params_with(5, 0), kFft, 4800));
        const float stretched = 1100.f * std::sqrt(1.f + .0025f * 100.f);
        int peak = bin_of(stretched) - 4;
        for (int k = peak; k <= bin_of(stretched) + 4; ++k) if (p[size_t(k)] > p[size_t(peak)]) peak = k;
        assert(std::abs(peak - bin_of(stretched)) <= 1);
        assert(band(p, 1100.f, 0) < .01 * band(p, stretched, 2));
    }
    // A high note: partials beyond 0.42 · 48 kHz are never rendered, so nothing folds back. Any
    // spectral peak away from a harmonic (window leakage aside) would be an alias: none above -80 dB.
    {
        const float hz = 3700.f;
        const auto p = spectrum(render(custom(255, 0, 0), hz, params_with(5, 0), kFft, 4800));
        double strongest = 0; for (double v : p) strongest = std::max(strongest, v);
        int aliases = 0;
        for (int k = 2; k + 1 < int(p.size()); ++k) {
            if (!(p[size_t(k)] > p[size_t(k - 1)] && p[size_t(k)] >= p[size_t(k + 1)])) continue;
            if (db(p[size_t(k)] / strongest) < -80.0) continue;
            const float f = k * kRate / kFft, nearest = hz * std::round(f / hz);
            if (std::fabs(f - nearest) > 6 * kRate / kFft) ++aliases;
        }
        assert(aliases == 0);
    }
    // The spectral comb carves the spectrum: with B up, some harmonics drop ≥ 20 dB below their
    // neighbours (with B at 0 no such notches exist).
    {
        const auto flat = spectrum(render(custom(200, 0, 0), 110.f, params_with(5, 0), kFft, 4800));
        const auto comb = spectrum(render(custom(200, 255, 0), 110.f, params_with(5, 0), kFft, 4800));
        int notches = 0;
        for (int k = 2; k < 60; ++k) {
            const double rel = band(comb, 110.f * k) / band(flat, 110.f * k);
            if (db(rel) < -20.0) ++notches;
        }
        assert(notches >= 5);
    }
    // Moving a macro mid-note is click-free: around the switch no sample-to-sample step exceeds what
    // the louder-moving of the two steady sounds produces on its own (phase-continuous partials,
    // levels ramped across the block).
    {
        auto steady_step = [&](int code) {
            const auto x = render(code, 220.f, defaults, 4800, 4800);
            float m = 0; for (size_t i = 1; i < x.size(); ++i) m = std::max(m, std::fabs(x[i] - x[i - 1]));
            return m;
        };
        for (int to : {custom(255, 0, 0), custom(40, 255, 0), custom(40, 0, 255), custom(255, 255, 255)}) {
            const int from = custom(40, 0, 0);
            Synth s; s.start(from); s.update(from, defaults);
            float previous = 0, switchStep = 0;
            for (int i = 0; i < 4900; ++i) {
                if (i == 4800) s.update(to, defaults);
                const float v = s.next(220.f, kRate);
                if (i >= 4790) switchStep = std::max(switchStep, std::fabs(v - previous));
                previous = v;
            }
            assert(switchStep <= 1.25f * std::max(steady_step(from), steady_step(to)));
        }
    }
    // Shimmer moves the level of the partials over time; without it a held note is stationary.
    {
        auto still = default_synth_params(); still[5] = 0;
        auto alive = default_synth_params(); alive[5] = 255;
        auto drift = [&](const SynthParams& p) {
            const auto x = render(custom(160, 0, 0), 220.f, p, 48000, 4800);
            const std::vector<float> first(x.begin(), x.begin() + kFft), last(x.end() - kFft, x.end());
            const auto a = spectrum(first), b = spectrum(last);
            double change = 0; for (int k = 1; k <= 40; ++k) change += std::fabs(db(band(a, 220.f * k) / band(b, 220.f * k)));
            return change / 40;
        };
        assert(drift(still) < .1 && drift(alive) > 1.0);
    }
    std::puts("PASS: SP ADDITIVE registered (3 macros, 6 parameters, 16 named sounds), harmonic at stretch 0, brightness +1 octave centroid at equal loudness, odd-only at even 0, bell stretch, no aliasing at 3.7 kHz, comb notches, click-free macro moves, shimmer motion");
    return 0;
}
