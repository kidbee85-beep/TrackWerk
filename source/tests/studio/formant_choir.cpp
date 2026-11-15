// FORMANT CHOIR (model 119, Studio > Synth > Voice): measured through studio::Synth at 48 kHz with an
// 8192-point Hann FFT (pt-kissfft). Each check names the property a singer or listener would hear.
#include "formant_choir.h"
#include "kiss_fftr.h"
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
double centroid(const std::vector<double>& p, float lo, float hi) {
    double num = 0, den = 0;
    for (int k = std::max(1, bin_of(lo)); k <= std::min(int(p.size()) - 1, bin_of(hi)); ++k) {
        num += k * kRate / kFft * p[size_t(k)];
        den += p[size_t(k)];
    }
    return num / den;
}
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double db(double powerRatio) { return 10 * std::log10(std::max(powerRatio, 1e-30)); }

// Share of the energy around harmonics 2…8 that sits within ±4 Hz of the exact harmonic.
double line_sharpness(const std::vector<double>& p, float hz) {
    double inside = 0, around = 0;
    for (int h = 2; h <= 8; ++h) {
        inside += range(p, hz * h - 4.f, hz * h + 4.f);
        around += range(p, hz * h - hz * .45f, hz * h + hz * .45f);
    }
    return inside / around;
}
// Energy between the harmonics (a band around each midpoint) relative to the whole, below 4 kHz.
double between_share(const std::vector<double>& p, float hz) {
    double between = 0;
    for (float f = hz * 1.5f; f < 4000.f; f += hz) between += range(p, f - hz * .2f, f + hz * .2f);
    return between / range(p, 0, 4000);
}

int voice_value(int voice) { return voice * 64 + 32; }
SynthParams params(int voice, int singers, int vibrato) {
    auto p = default_synth_params();
    p[2] = voice_value(voice);
    p[3] = ((singers - 1) * 255 + 6) / 7;
    p[4] = vibrato;
    return p;
}
int custom(int vowel, int ensemble, int breath) { return custom_code(kFormantChoirModel, vowel, ensemble, breath); }
constexpr int kA = 0, kE = 64, kI = 128, kO = 191, kU = 255;

}  // namespace

int main() {
    // Registration: the Voice family's first model, three macros, six parameters, sixteen named sounds.
    assert(kVoiceFirstModel == kSpectralFirstModel + kSpectralModelCount && kFormantChoirModel == kVoiceFirstModel);
    assert(kVoiceFirstModel + kVoiceModelCount == kVintageFirstModel && model_count() > kVintageFirstModel && is_voice_model(kFormantChoirModel) &&
           !is_spectral_model(kFormantChoirModel));
    assert(std::string(model_name(kFormantChoirModel)) == "FORMANT CHOIR");
    assert(std::string(macro_name(kFormantChoirModel, 0)) == "Vowel" &&
           std::string(macro_name(kFormantChoirModel, 1)) == "Ensemble" &&
           std::string(macro_name(kFormantChoirModel, 2)) == "Breath");
    assert(synth_param_count(kFormantChoirModel) == 6 && std::string(synth_param_name(kFormantChoirModel, 2)) == "Voice" &&
           std::string(synth_param_name(kFormantChoirModel, 3)) == "Singers" &&
           std::string(synth_param_name(kFormantChoirModel, 5)) == "Vibrato rate");
    assert(synth_param_value(kFormantChoirModel, 2, voice_value(3)) == "Soprano" &&
           synth_param_value(kFormantChoirModel, 2, -1) == "Factory" &&
           synth_param_value(kFormantChoirModel, 3, 0) == "1 singer" &&
           synth_param_value(kFormantChoirModel, 3, 255) == "8 singers" &&
           synth_param_value(kFormantChoirModel, 4, 255) == "80 cents" &&
           synth_param_value(kFormantChoirModel, 5, 0) == "3.5 Hz");
    // One press, one voice / one singer; both ends held; every singer count reachable.
    assert(synth_param_adjust(kFormantChoirModel, 2, voice_value(1), 1, false) == voice_value(2));
    assert(synth_param_adjust(kFormantChoirModel, 2, voice_value(3), 1, true) == voice_value(3));
    assert(synth_param_adjust(kFormantChoirModel, 2, voice_value(0), -1, false) == voice_value(0));
    for (int n = 1, v = 0; n <= 8; ++n) {
        assert(synth_param_value(kFormantChoirModel, 3, v) == std::to_string(n) + (n == 1 ? " singer" : " singers"));
        v = synth_param_adjust(kFormantChoirModel, 3, v, 1, false);
    }
    assert(preset_name(preset_code(kFormantChoirModel, 0)) == "FORMANT CHOIR / Ah Choir");
    assert(!synth_needs_strike(kFormantChoirModel));

    // Every named sound sings, stays bounded and sits in one loudness range.
    {
        double quietest = 1, loudest = 0;
        for (int v = 0; v < variation_count(); ++v) {
            const auto x = render(preset_code(kFormantChoirModel, v), 196.f, default_synth_params(), 24000, 4800);
            double peak = 0;
            for (float s : x) peak = std::max(peak, double(std::fabs(s)));
            assert(peak < 1.0);
            quietest = std::min(quietest, rms(x));
            loudest = std::max(loudest, rms(x));
        }
        std::printf("16 sounds at G3: rms %.3f … %.3f (%.1f dB)\n", quietest, loudest, db(loudest * loudest / (quietest * quietest)));
        assert(quietest > .05 && db(loudest * loudest / (quietest * quietest)) < 6.0);
    }

    // The vowels sit where they belong: a solo tenor on A2 — A has its first formant high (≈ 650 Hz),
    // I low (≈ 290 Hz) with the second far up (≈ 1870 Hz), U keeps both low.
    {
        const auto solo = params(1, 1, 0);
        const auto a = spectrum(render(custom(kA, 0, 0), 110.f, solo, kFft, 9600));
        const auto i = spectrum(render(custom(kI, 0, 0), 110.f, solo, kFft, 9600));
        const auto u = spectrum(render(custom(kU, 0, 0), 110.f, solo, kFft, 9600));
        const double f1A = centroid(a, 150, 1000), f1I = centroid(i, 150, 1000), f1U = centroid(u, 150, 1000);
        const double f2ShareI = range(i, 1600, 2200) / total(i), f2ShareA = range(a, 1600, 2200) / total(a),
                     f2ShareU = range(u, 1600, 2200) / total(u);
        std::printf("Tenor on A2: F1 centroid A %.0f / I %.0f / U %.0f Hz, energy 1.6–2.2 kHz A %.4f / I %.4f / U %.4f\n",
                    f1A, f1I, f1U, f2ShareA, f2ShareI, f2ShareU);
        assert(f1A > 1.5 * f1I && f1A > 1.4 * f1U);
        assert(f2ShareI > 3 * f2ShareU);
    }

    // Between two vowels the formants glide: halfway from A to E (F1 650 → 400 Hz) F1 lies between.
    {
        const auto solo = params(1, 1, 0);
        const double atA = centroid(spectrum(render(custom(kA, 0, 0), 110.f, solo, kFft, 9600)), 150, 1000);
        const double half = centroid(spectrum(render(custom(kE / 2, 0, 0), 110.f, solo, kFft, 9600)), 150, 1000);
        const double atE = centroid(spectrum(render(custom(kE, 0, 0), 110.f, solo, kFft, 9600)), 150, 1000);
        assert(atA > half && half > atE);
    }

    // Voice types: a soprano's A has its first formant higher than a bass's A (≈ 800 against 600 Hz).
    {
        const auto bass = spectrum(render(custom(kA, 0, 0), 220.f, params(0, 1, 0), kFft, 9600));
        const auto soprano = spectrum(render(custom(kA, 0, 0), 220.f, params(3, 1, 0), kFft, 9600));
        const double f1Bass = centroid(bass, 300, 1100), f1Soprano = centroid(soprano, 300, 1100);
        std::printf("A on A3: F1 centroid bass %.0f, soprano %.0f Hz\n", f1Bass, f1Soprano);
        assert(f1Soprano > 1.15 * f1Bass);
    }

    // Loudness holds across the vowels and across the range of a voice.
    {
        double lo = 1, hi = 0;
        for (int v : {kA, kE, kI, kO, kU}) {
            const double r = rms(render(custom(v, 128, 40), 196.f, params(1, 8, 64), 24000, 4800));
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        std::printf("Across vowels: %.1f dB\n", db(hi * hi / (lo * lo)));
        assert(db(hi * hi / (lo * lo)) < 3.0);
        lo = 1, hi = 0;
        for (float hz : {98.f, 196.f, 392.f, 784.f}) {
            const double r = rms(render(custom(kO, 128, 40), hz, params(1, 8, 64), 24000, 4800));
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        std::printf("Across G2 … G5: %.1f dB\n", db(hi * hi / (lo * lo)));
        assert(db(hi * hi / (lo * lo)) < 4.0);
    }

    // Ensemble: a solo voice without vibrato has sharp harmonic lines; the full choir smears them.
    {
        const auto solo = spectrum(render(custom(kA, 0, 0), 196.f, params(1, 8, 0), kFft, 24000));
        const auto choir = spectrum(render(custom(kA, 255, 0), 196.f, params(1, 8, 0), kFft, 24000));
        const double sharpSolo = line_sharpness(solo, 196.f), sharpChoir = line_sharpness(choir, 196.f);
        std::printf("Line sharpness at G3: solo %.3f, full choir %.3f\n", sharpSolo, sharpChoir);
        assert(sharpSolo > .9 && sharpChoir < .7);
    }

    // Vibrato: none keeps the solo pitch still; full depth sways it.
    {
        const auto still = spectrum(render(custom(kA, 0, 0), 196.f, params(1, 1, 0), kFft, 24000));
        const auto sway = spectrum(render(custom(kA, 0, 0), 196.f, params(1, 1, 255), kFft, 24000));
        assert(line_sharpness(still, 196.f) > .9 && line_sharpness(sway, 196.f) < .5);
    }

    // Breath: air fills the gaps between the harmonics.
    {
        const auto dry = spectrum(render(custom(kA, 0, 0), 196.f, params(1, 1, 0), kFft, 9600));
        const auto airy = spectrum(render(custom(kA, 0, 255), 196.f, params(1, 1, 0), kFft, 9600));
        const double shareDry = between_share(dry, 196.f), shareAiry = between_share(airy, 196.f);
        std::printf("Energy between harmonics: breath 0 %.5f, breath 255 %.5f\n", shareDry, shareAiry);
        assert(shareAiry > 20 * shareDry);
    }

    // High notes stay clean: a soprano on C6 keeps almost nothing above 12 kHz.
    {
        const auto p = spectrum(render(custom(kA, 255, 40), 1046.5f, params(3, 8, 64), kFft, 9600));
        const double high = range(p, 12000, 23999) / total(p);
        std::printf("Soprano C6: energy above 12 kHz %.2e\n", high);
        assert(high < 1e-3);
    }

    // Click-free: jumping the vowel from A to U is no steeper than either vowel on its own.
    {
        const auto p = params(1, 8, 64);
        const auto steepest = [&](int vowel) {
            const auto x = render(custom(vowel, 128, 40), 196.f, p, 9600, 9600);
            float worst = 0;
            for (size_t i = 1; i < x.size(); ++i) worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
            return worst;
        };
        const float steady = std::max(steepest(kA), steepest(kU));
        Synth s;
        s.start(custom(kA, 128, 40));
        s.update(custom(kA, 128, 40), p);
        float last = 0, worst = 0;
        for (int i = 0; i < 9600; ++i) last = s.next(196.f, kRate);
        s.update(custom(kU, 128, 40), p);
        for (int i = 0; i < 4800; ++i) {
            const float v = s.next(196.f, kRate);
            worst = std::max(worst, std::fabs(v - last));
            last = v;
        }
        std::printf("Vowel jump A → U: steepest step %.3f, steady vowels %.3f\n", worst, steady);
        assert(worst < 1.15f * steady);
    }

    // Reproducible: the same note renders the same samples.
    {
        const auto x = render(preset_code(kFormantChoirModel, 5), 220.f, default_synth_params(), 9600, 0);
        const auto y = render(preset_code(kFormantChoirModel, 5), 220.f, default_synth_params(), 9600, 0);
        assert(x == y);
    }

    // Each named sound sings with its own voice: "Monks" (bass) sits lower than "Soprano Ah".
    {
        const auto monks = spectrum(render(preset_code(kFormantChoirModel, 2), 196.f, default_synth_params(), kFft, 9600));
        const auto soprano = spectrum(render(preset_code(kFormantChoirModel, 3), 196.f, default_synth_params(), kFft, 9600));
        assert(centroid(soprano, 300, 1100) > 1.15 * centroid(monks, 300, 1100));
    }

    std::puts("PASS: FORMANT CHOIR registered (Voice family, 3 macros, 6 parameters, 16 sounds, voice and singer "
              "stepping), all sounds bounded within 6 dB, vowels at their formants and gliding between, voice types "
              "ordered, loudness steady across vowels and range, ensemble and vibrato move the pitch, breath fills "
              "the gaps, clean high notes, click-free vowel jumps, reproducible");
    return 0;
}
