// DCO POLY (model 121, Studio > Synth > Vintage): measured through studio::Synth at 48 kHz with an
// 8192-point Hann FFT (pt-kissfft). Each check names the property a player of the classic DCO
// polysynths would listen for.
#include "dco_poly.h"
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
double harmonic(const std::vector<double>& p, float hz, float n) { return range(p, hz * n - 15.f, hz * n + 15.f); }
double off_harmonic(const std::vector<double>& p, float hz) {
    double off = 0;
    for (size_t k = 1; k < p.size(); ++k) {
        const double f = k * kRate / kFft;
        const double nearest = std::max(1.0, std::round(f / hz)) * hz;
        if (std::fabs(f - nearest) > 20.0) off += p[k];
    }
    return off / total(p);
}
// Share of the energy around harmonics first…last within ±4 Hz of the exact harmonic.
double line_sharpness(const std::vector<double>& p, float hz, int first = 2, int last = 8) {
    double inside = 0, around = 0;
    for (int h = first; h <= last; ++h) {
        inside += range(p, hz * h - 4.f, hz * h + 4.f);
        around += range(p, hz * h - hz * .45f, hz * h + hz * .45f);
    }
    return inside / around;
}
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double db(double powerRatio) { return 10 * std::log10(std::max(powerRatio, 1e-30)); }

// Parameter rows: 2 Saw, 3 Pulse, 4 Pulse width, 5 PWM, 6 Sub, 7 Noise, 8 HPF, 9 Env amount,
// 10 Env decay, 11 Env sustain, 12 LFO rate, 13 Vibrato, 14 Key follow.
struct Patch {
    int saw = 255, pulse = 0, width = 0, pwm = 0, sub = 0, noise = 0, hpf = 1, env = 0, decay = 128, sustain = 255,
        lfo = 120, vibrato = 0, key = 0;
};
SynthParams params(const Patch& p) {
    auto s = default_synth_params();
    const int values[] = {p.saw, p.pulse, p.width, p.pwm, p.sub, p.noise, p.hpf * 64 + 32, p.env, p.decay, p.sustain,
                          p.lfo, p.vibrato, p.key};
    for (int i = 0; i < 13; ++i) s[size_t(2 + i)] = values[i];
    return s;
}
constexpr int kOff = 32, kI = 96, kII = 160, kBoth = 224;
int custom(int cutoff, int resonance, int chorus) { return custom_code(kDcoPolyModel, cutoff, resonance, chorus); }

}  // namespace

int main() {
    // Registration: the Vintage family's second model, three macros, fifteen parameters, sixteen sounds.
    assert(kDcoPolyModel == kSidChipModel + 1 && is_vintage_model(kDcoPolyModel) && is_vintage_model(kSidChipModel));
    assert(model_count() == kVintageFirstModel + kVintageModelCount && std::string(model_name(kDcoPolyModel)) == "DCO POLY");
    assert(std::string(macro_name(kDcoPolyModel, 0)) == "Cutoff" && std::string(macro_name(kDcoPolyModel, 1)) == "Resonance" &&
           std::string(macro_name(kDcoPolyModel, 2)) == "Chorus");
    assert(synth_param_count(kDcoPolyModel) == 15 && std::string(synth_param_name(kDcoPolyModel, 2)) == "Saw" &&
           std::string(synth_param_name(kDcoPolyModel, 8)) == "HPF" &&
           std::string(synth_param_name(kDcoPolyModel, 14)) == "Key follow");
    assert(synth_param_value(kDcoPolyModel, 8, 32) == "0 Bass boost" && synth_param_value(kDcoPolyModel, 8, 255) == "3" &&
           synth_param_value(kDcoPolyModel, 10, 0) == "0.01 s" && synth_param_value(kDcoPolyModel, 12, 0) == "0.10 Hz" &&
           synth_param_value(kDcoPolyModel, 13, 255) == "60 cents" && synth_param_value(kDcoPolyModel, 2, -1) == "Factory");
    assert(synth_param_adjust(kDcoPolyModel, 8, 32, 1, false) == 96 && synth_param_adjust(kDcoPolyModel, 8, 224, 1, true) == 224 &&
           synth_param_adjust(kDcoPolyModel, 8, 32, -1, false) == 32);
    assert(preset_name(preset_code(kDcoPolyModel, 0)) == "DCO POLY / Poly Strings" && !synth_needs_strike(kDcoPolyModel));

    // Every named sound plays, stays bounded and sits in one loudness range.
    {
        double quietest = 1, loudest = 0;
        for (int v = 0; v < variation_count(); ++v) {
            const auto x = render(preset_code(kDcoPolyModel, v), 196.f, default_synth_params(), 24000, 9600);
            double peak = 0;
            for (float s : x) peak = std::max(peak, double(std::fabs(s)));
            assert(peak < 1.0);
            quietest = std::min(quietest, rms(x));
            loudest = std::max(loudest, rms(x));
        }
        std::printf("16 sounds at G3: rms %.3f … %.3f (%.1f dB)\n", quietest, loudest, db(loudest * loudest / (quietest * quietest)));
        assert(quietest > .05 && db(loudest * loudest / (quietest * quietest)) < 9.0);
    }

    // The DCO (filter open): the saw's second harmonic is −6 dB, the square has none, the sub adds
    // the octave below.
    {
        const float hz = 220.f;
        Patch saw;
        Patch square;
        square.saw = 0;
        square.pulse = 255;
        Patch sub;
        sub.sub = 255;
        const auto ps = spectrum(render(custom(255, 0, kOff), hz, params(saw), kFft, 4800));
        const auto pq = spectrum(render(custom(255, 0, kOff), hz, params(square), kFft, 4800));
        const auto pb = spectrum(render(custom(255, 0, kOff), hz, params(sub), kFft, 4800));
        const double saw2 = harmonic(ps, hz, 2) / harmonic(ps, hz, 1), square2 = harmonic(pq, hz, 2) / harmonic(pq, hz, 1),
                     subShare = harmonic(pb, hz, .5f) / total(pb), plainSub = harmonic(ps, hz, .5f) / total(ps);
        std::printf("DCO at A3: saw H2/H1 %.3f, square H2/H1 %.4f, energy at 110 Hz with sub %.3f, without %.5f\n", saw2,
                    square2, subShare, plainSub);
        assert(saw2 > .2 && saw2 < .3 && square2 < .005 && subShare > .3 && plainSub < .001);
    }

    // Band-limited: a saw with pulse on E7 puts almost nothing between its harmonics.
    {
        Patch both;
        both.pulse = 200;
        both.width = 100;
        const double off = off_harmonic(spectrum(render(custom(255, 0, kOff), 2637.f, params(both), kFft, 4800)), 2637.f);
        std::printf("Fold-back on E7: %.1e of the energy off the harmonics\n", off);
        assert(off < 3e-3);
    }

    // The low-pass falls at four poles: an octave above the cutoff loses about 24 dB more than open.
    {
        const float hz = 100.f;
        const auto open = spectrum(render(custom(255, 0, kOff), hz, params(Patch{}), kFft, 9600));
        // Cutoff 20 Hz · 2^(10·c): c = 0.565 → ≈ 1 kHz.
        const auto closed = spectrum(render(custom(144, 0, kOff), hz, params(Patch{}), kFft, 9600));
        const double slopeOpen = db(harmonic(open, hz, 40) / harmonic(open, hz, 20));
        const double slopeClosed = db(harmonic(closed, hz, 40) / harmonic(closed, hz, 20));
        std::printf("2 kHz → 4 kHz: open %.1f dB, cutoff ≈ 1 kHz %.1f dB\n", slopeOpen, slopeClosed);
        assert(slopeClosed - slopeOpen < -18.0);
    }

    // Resonance lifts the region at the cutoff.
    {
        const float hz = 110.f;
        const auto flat = spectrum(render(custom(144, 0, kOff), hz, params(Patch{}), kFft, 9600));
        const auto peaked = spectrum(render(custom(144, 220, kOff), hz, params(Patch{}), kFft, 9600));
        const float fc = 20.f * std::exp2(10.f * 144 / 255.f);
        const double lift = (range(peaked, fc * .85f, fc * 1.18f) / total(peaked)) / (range(flat, fc * .85f, fc * 1.18f) / total(flat));
        std::printf("Resonance at %.0f Hz lifts the band %.1f×\n", fc, lift);
        assert(lift > 4.0);
    }

    // The envelope opens the filter at the attack and closes it as it decays.
    {
        Patch p;
        p.env = 255;
        p.decay = 140;   // ≈ 0.26 s
        p.sustain = 0;
        const auto x = render(custom(60, 0, kOff), 110.f, params(p), 48000, 0);
        const auto pe = spectrum(x, 0), pl = spectrum(x, 36000);
        const double early = range(pe, 1000, 12000) / total(pe), late = range(pl, 1000, 12000) / total(pl);
        std::printf("Envelope: energy above 1 kHz %.4f over the first 170 ms, %.6f after 0.75 s\n", early, late);
        assert(early > 20 * late);
    }

    // Key follow: with it a higher note keeps its fourth harmonic as strong as a lower one; without it
    // the fixed cutoff takes the higher note's fourth harmonic away.
    {
        Patch follow;
        follow.key = 255;
        const auto ratio = [&](const Patch& p) {
            const auto low = spectrum(render(custom(110, 0, kOff), 110.f, params(p), kFft, 4800));
            const auto high = spectrum(render(custom(110, 0, kOff), 440.f, params(p), kFft, 4800));
            return (harmonic(high, 440.f, 4) / harmonic(high, 440.f, 1)) / (harmonic(low, 110.f, 4) / harmonic(low, 110.f, 1));
        };
        const double with = ratio(follow), without = ratio(Patch{});
        std::printf("Key follow: H4/H1 on A4 against A2 %.2f with, %.4f without\n", with, without);
        assert(with > .3 && without < with / 10);
    }

    // HPF: position 3 removes the fundamental, position 0 lifts the bass against position 1.
    {
        const float hz = 80.f;
        Patch flat, boost, thin;
        boost.hpf = 0;
        thin.hpf = 3;
        const auto pf = spectrum(render(custom(255, 0, kOff), hz, params(flat), kFft, 4800));
        const auto pb = spectrum(render(custom(255, 0, kOff), hz, params(boost), kFft, 4800));
        const auto pt = spectrum(render(custom(255, 0, kOff), hz, params(thin), kFft, 4800));
        const double fundFlat = harmonic(pf, hz, 1) / total(pf), fundBoost = harmonic(pb, hz, 1) / total(pb),
                     fundThin = harmonic(pt, hz, 1) / total(pt);
        const double cut = harmonic(pt, hz, 1) / harmonic(pf, hz, 1);
        std::printf("HPF on 80 Hz: fundamental share 0 %.3f, 1 %.3f, 3 %.4f; position 3 keeps %.3f of its power\n",
                    fundBoost, fundFlat, fundThin, cut);
        assert(fundBoost > 1.1 * fundFlat && fundThin < fundFlat / 5 && cut < .05);
    }

    // Chorus: off keeps the harmonics sharp; I, II and I+II move them. The moving delay shifts every
    // partial by a fraction of its frequency, so it shows on the upper harmonics (10th…20th).
    {
        const float hz = 196.f;
        const auto sharp = [&](int chorus) {
            return line_sharpness(spectrum(render(custom(255, 0, chorus), hz, params(Patch{}), kFft, 9600)), hz, 10, 20);
        };
        const double off = sharp(kOff), one = sharp(kI), two = sharp(kII), both = sharp(kBoth);
        std::printf("Line sharpness at G3: chorus off %.3f, I %.3f, II %.3f, I+II %.3f\n", off, one, two, both);
        assert(off > .95 && one < .9 && two < .9 && both < .9);
    }

    // Vibrato and PWM move with the LFO.
    {
        const float hz = 196.f;
        Patch vib;
        vib.vibrato = 255;
        assert(line_sharpness(spectrum(render(custom(200, 0, kOff), hz, params(vib), kFft, 9600)), hz) < .6);
        Patch pwm;
        pwm.saw = 0;
        pwm.pulse = 255;
        pwm.pwm = 255;
        pwm.lfo = 60;
        const auto x = render(custom(255, 0, kOff), hz, params(pwm), 48000 * 2, 4800);
        double lo = 1, hi = 0;
        for (size_t from = 0; from + kFft <= x.size(); from += 8000) {
            const auto p = spectrum(x, from);
            const double h2 = harmonic(p, hz, 2) / harmonic(p, hz, 1);
            lo = std::min(lo, h2);
            hi = std::max(hi, h2);
        }
        std::printf("PWM: second harmonic swings %.4f … %.3f of the first\n", lo, hi);
        assert(hi > 20 * lo + .01);
    }

    // Click-free: switching the chorus from off to I+II and jumping the cutoff are no steeper than the sound.
    {
        const auto p = params(Patch{});
        const auto steepest = [&](int code) {
            const auto x = render(code, 196.f, p, 9600, 9600);
            float worst = 0;
            for (size_t i = 1; i < x.size(); ++i) worst = std::max(worst, std::fabs(x[i] - x[i - 1]));
            return worst;
        };
        const float steady = std::max({steepest(custom(60, 80, kOff)), steepest(custom(255, 80, kBoth))});
        Synth s;
        s.start(custom(60, 80, kOff));
        s.update(custom(60, 80, kOff), p);
        float last = 0, worst = 0;
        for (int i = 0; i < 9600; ++i) last = s.next(196.f, kRate);
        s.update(custom(255, 80, kBoth), p);
        for (int i = 0; i < 4800; ++i) {
            const float v = s.next(196.f, kRate);
            worst = std::max(worst, std::fabs(v - last));
            last = v;
        }
        std::printf("Cutoff and chorus jump: steepest step %.3f, steady %.3f\n", worst, steady);
        assert(worst < 1.15f * steady);
    }

    // Reproducible: the same note renders the same samples (noise included).
    {
        const auto x = render(preset_code(kDcoPolyModel, 10), 220.f, default_synth_params(), 9600, 0);
        const auto y = render(preset_code(kDcoPolyModel, 10), 220.f, default_synth_params(), 9600, 0);
        assert(x == y);
    }

    std::puts("PASS: DCO POLY registered (Vintage family, 3 macros, 15 parameters, 16 sounds, HPF stepping), all sounds "
              "bounded within 9 dB, DCO saw/square/sub, band-limited on E7, four-pole slope, resonance, filter envelope, "
              "key follow, HPF positions, chorus I/II/I+II, vibrato and PWM, click-free jumps, reproducible");
    return 0;
}
