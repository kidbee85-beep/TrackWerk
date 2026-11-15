// Stage-2 sound checks: block-boundary interpolation (K1), decayed struck voices (K7), constant
// rack latency (K3). K4 is checked on the engine path in handover_fixes.cpp. K2 (Braids from zeroed state) is
// not checked here: Synth::State is large enough that malloc maps fresh zero pages on macOS and glibc.
#include "native_clock.h"
#include "rack.h"
#include "synth.h"
#include "synth_params.h"
#include "kiss_fft.h"
#include "kiss_fftr.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace studio;

namespace {

// A ramp rendered in native blocks: sample n of the stream has value n.
struct RampSource {
    float out[48]{};
    int size = 16, block = 0;
    void refill() { for (int j = 0; j < size; ++j) out[j] = float(block * size + j); ++block; }
};

void clock_checks() {
    // Upsampling (Elements 32 kHz → 48 kHz) and near-unity (48 kHz → 44.1 kHz): a linear input must
    // come out linear, including across every block boundary. Expected value: position − 1 (latency).
    for (double advance : {2.0 / 3.0, 48000.0 / 44100.0, 32000.0 / 44100.0, 1.0}) {
        for (int size : {16, 24}) {
            RampSource src; src.size = size;
            NativeClock clock; clock.restart(size);
            double position = 0;
            for (int n = 0; n < 2000; ++n) {
                const float v = clock.next(src.out, src.size, advance, [&] { src.refill(); });
                if (advance <= 1.0) {
                    if (position >= 1.0) assert(std::fabs(v - float(position - 1.0)) < 1e-3f);
                } else if (n > 2) {
                    // Box filter over [position, position + advance): mean of the ramp there, minus 1.
                    const double expected = position + advance / 2.0 - 1.0;
                    assert(std::fabs(v - float(expected)) < 0.51f);   // centre of unit-weight reads
                }
                position += advance;
            }
        }
    }
    // Decimation (Braids 96 kHz → 48 kHz): consecutive outputs step by exactly `advance`.
    RampSource src; src.size = 24; NativeClock clock; clock.restart(24);
    float previous = 0;
    for (int n = 0; n < 500; ++n) {
        const float v = clock.next(src.out, src.size, 2.0, [&] { src.refill(); });
        if (n > 2) assert(std::fabs((v - previous) - 2.f) < 1e-3f);
        previous = v;
    }
}

std::vector<float> render(Synth& s, float hz, int frames, float rate = 48000) {
    std::vector<float> out(static_cast<size_t>(frames));
    for (auto& v : out) v = s.next(hz, rate);
    return out;
}

void decay_checks() {
    // KICK (Plaits 21) is struck: it rings out and is then reported as decayed.
    Synth kick; const int kickCode = preset_code(21, 0);
    kick.start(kickCode); kick.update(kickCode, default_synth_params());
    int decayedAt = -1;
    for (int n = 0; n < 48000 * 6; ++n) { kick.next(55.f, 48000); if (decayedAt < 0 && kick.decayed()) decayedAt = n; }
    assert(decayedAt > int(Synth::kDecayedSeconds * 48000) && decayedAt < 48000 * 5);
    kick.start(kickCode);
    assert(!kick.decayed());   // a new note starts afresh

    // A sustained model is never reported, nor a bowed Elements articulation that keeps sounding.
    for (int code : {preset_code(8, 0), preset_code(78, 3)}) {
        Synth s; s.start(code); s.update(code, default_synth_params());
        for (int n = 0; n < 48000 * 4; ++n) { s.next(220.f, 48000); assert(!s.decayed()); }
    }
}

// The 8-bit model fields keep every code written up to 1.1.0-dev.2 (7-bit model) unchanged and
// leave room for 256 models.
void code_format_checks() {
    static_assert(kMaxModels == 256 && kSpectralFirstModel == 116);
    for (int model = 0; model < model_count(); ++model) {
        for (int v = 0; v < variation_count(); ++v) {
            const int code = preset_code(model, v);
            const int legacy = v < 4 ? model * 4 + v : (1 << 29) | (model << 4) | v;   // the dev.2 formula
            assert(code == legacy && valid_code(code) && preset_model(code) == model && preset_variation(code) == v);
            assert(uri_code(uri(code).c_str()) == code);
        }
        for (int a : {0, 128, 255}) {
            const int code = custom_code(model, a, 255 - a, a / 2);
            const int legacy = (1 << 30) | model | (a / 2 << 7) | ((255 - a) / 2 << 14) | (a / 4 << 21);
            assert(code == legacy && valid_code(code) && preset_model(code) == model);
            assert(uri_code(custom_uri(model, a, 255 - a, a / 2).c_str()) == code);
        }
    }
    // Room above 127: the encoders round-trip models the dev.2 format could not hold.
    for (int model : {127, 128, 200, 255}) {
        assert(custom_model(encode_custom(model, 1, 2, 3)) == model);
        assert(custom_macro(encode_custom(model, 1, 2, 3), 2) == 3);
        assert(extended_model(encode_extended(model, 7)) == model);
    }
    // Unregistered models above the current count stay invalid.
    assert(!valid_code(encode_custom(model_count(), 0, 0, 0)) && !valid_code(encode_custom(200, 0, 0, 0)));
    assert(!valid_code(encode_extended(200, 5)));
}

// pt-mutable carries KissFFT for the spectral engines. The vendored copy is trimmed: the real
// forward transform (kiss_fftr) and the complex transform both ways (kiss_fft) exist, kiss_fftri does
// not, so a spectral engine resynthesises through the complex inverse.
void fft_checks() {
    constexpr int n = 512;
    kiss_fftr_cfg real = kiss_fftr_alloc(n, 0, nullptr, nullptr);
    kiss_fft_cfg inverse = kiss_fft_alloc(n, 1, nullptr, nullptr);
    assert(real && inverse);
    std::vector<float> x(n);
    std::vector<kiss_fft_cpx> half(n / 2 + 1), full(n), back(n);
    for (int i = 0; i < n; ++i) x[i] = std::sin(2 * 3.14159265f * 17 * i / n) + .25f * std::sin(2 * 3.14159265f * 90 * i / n);
    kiss_fftr(real, x.data(), half.data());
    int loudest = 0;
    for (int k = 1; k <= n / 2; ++k) if (std::hypot(half[k].r, half[k].i) > std::hypot(half[loudest].r, half[loudest].i)) loudest = k;
    assert(loudest == 17);
    for (int k = 0; k <= n / 2; ++k) full[size_t(k)] = half[size_t(k)];
    for (int k = 1; k < n / 2; ++k) full[size_t(n - k)] = {half[size_t(k)].r, -half[size_t(k)].i};   // Hermitian
    kiss_fft(inverse, full.data(), back.data());
    // This KissFFT's inverse config scales by 1/n itself (kiss_fft.h).
    for (int i = 0; i < n; ++i) assert(std::fabs(back[size_t(i)].r - x[size_t(i)]) < 1e-4f && std::fabs(back[size_t(i)].i) < 1e-4f);
    kiss_fftr_free(real); kiss_fft_free(inverse);

    // kiss_fftr against the complex transform of the same input, every bin, several sizes. Up to
    // 1.1.0-dev.2 every bin above N/4 was mirrored from the wrong side.
    for (int size : {4, 8, 64, 512, 2048}) {
        kiss_fftr_cfg r = kiss_fftr_alloc(size, 0, nullptr, nullptr);
        kiss_fft_cfg c = kiss_fft_alloc(size, 0, nullptr, nullptr);
        std::vector<float> in(static_cast<size_t>(size), 0.f);
        std::vector<kiss_fft_cpx> viaReal(static_cast<size_t>(size / 2 + 1)), cin(static_cast<size_t>(size)), viaComplex(static_cast<size_t>(size));
        unsigned seed = 12345u + unsigned(size);
        for (int i = 0; i < size; ++i) { seed = seed * 1664525u + 1013904223u; in[size_t(i)] = float(seed >> 8) / float(1 << 24) - .5f; cin[size_t(i)] = {in[size_t(i)], 0.f}; }
        kiss_fftr(r, in.data(), viaReal.data());
        kiss_fft(c, cin.data(), viaComplex.data());
        for (int k = 0; k <= size / 2; ++k) {
            const float tolerance = 1e-4f * std::sqrt(float(size));
            assert(std::fabs(viaReal[size_t(k)].r - viaComplex[size_t(k)].r) < tolerance);
            assert(std::fabs(viaReal[size_t(k)].i - viaComplex[size_t(k)].i) < tolerance);
        }
        kiss_fftr_free(r); kiss_fft_free(c);
    }
}

int rack_peak(int fx, int mix) {
    Rack rack;
    rack.control[ENABLE].store(1); rack.control[FX].store(fx); rack.control[FX_MIX].store(mix);
    std::vector<float> buf(2 * 4096, 0.f);
    buf[2 * 1000] = buf[2 * 1000 + 1] = .5f;
    for (int at = 0; at < 4096; at += 64) rack.process(buf.data() + 2 * at, 64, 2, 48000);
    int peak = 0;
    for (int i = 0; i < 4096; ++i) if (std::fabs(buf[2 * i]) > std::fabs(buf[2 * peak])) peak = i;
    return peak;
}

void rack_checks() {
    // FX off, an effect at 0 % mix: the same 48-frame delay as a running effect, so turning MIX or
    // FX never moves the signal in time.
    assert(rack_peak(0, 0) == 1048);
    assert(rack_peak(7, 0) == 1048);
    assert(rack_peak(16, 0) == 1048);
    assert(rack_peak(0, 255) == 1048);
}

}  // namespace

int main() {
    clock_checks();
    decay_checks();
    rack_checks();
    fft_checks();
    code_format_checks();
    std::puts("PASS: block-boundary interpolation at 32/44.1/48/96 kHz, struck voices release after 1.5 s of silence, constant 48-frame rack latency, kiss_fftr equal to the complex FFT in every bin, KissFFT round trip in pt-mutable, stable codes with 8-bit model fields");
    return 0;
}
