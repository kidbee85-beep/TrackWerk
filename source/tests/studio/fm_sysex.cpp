// FM SYSEX (model 122): the DX7 SysEx reader (studio/fm_sysex.h) and the voice (fm_sysex_voice.h),
// measured through studio::Synth at 48 kHz. The banks here are built in the test, byte by byte from
// the format, so no third-party voice data is needed.
#include "fm_sysex.h"
#include "kiss_fftr.h"
#include "plaits/dsp/fm/patch.h"
#include "synth.h"
#include "synth_params.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace studio;
using fm_sysex::PackedVoice;

namespace {

constexpr float kRate = 48000.f;
constexpr int kFft = 8192;

// One packed voice: algorithm 32 (six carriers); only OP1 sounds, at `coarse` × the note, with the
// given decay and velocity sensitivity. Operator 1 is the last of the six in the packed data.
PackedVoice sine_voice(const char* name, int coarse = 1, int decayRate = 99, int sustainLevel = 99, int velocitySens = 0,
                       int algorithm = 31, int op2Level = 0) {
    PackedVoice v{};
    for (int op = 0; op < 6; ++op) {
        uint8_t* o = v.data() + op * 17;
        const bool carrier = op == 5, modulator = op == 4;
        for (int i = 0; i < 4; ++i) o[i] = 99;                       // rates
        o[1] = uint8_t(decayRate);
        o[4] = 99; o[5] = uint8_t(sustainLevel); o[6] = uint8_t(sustainLevel); o[7] = 0;   // levels
        o[8] = 39;                                                     // break point
        o[12] = uint8_t(0 | 7 << 3);                                   // rate scaling 0, detune centre
        o[13] = uint8_t(0 | (carrier ? velocitySens : 0) << 2);
        o[14] = uint8_t(carrier ? 99 : modulator ? op2Level : 0);
        o[15] = uint8_t(0 | (carrier ? coarse : 1) << 1);
        o[16] = 0;
    }
    for (int i = 0; i < 4; ++i) { v[size_t(102 + i)] = 99; v[size_t(106 + i)] = 50; }   // flat pitch envelope
    v[110] = uint8_t(algorithm);
    v[111] = uint8_t(0 | 1 << 3);                                      // feedback 0, key sync
    v[112] = 35; v[113] = 0; v[114] = 0; v[115] = 0; v[116] = 0;
    v[117] = 24;                                                       // no transpose
    for (int i = 0; i < 10; ++i) v[size_t(118 + i)] = uint8_t(name[i] ? name[i] : ' ');
    return v;
}
std::vector<uint8_t> bank_message(const std::vector<PackedVoice>& voices, int device = 0) {
    std::vector<uint8_t> m = {0xF0, 0x43, uint8_t(device), 0x09, 0x20, 0x00};
    for (int v = 0; v < 32; ++v) {
        const auto& voice = voices[size_t(v) % voices.size()];
        m.insert(m.end(), voice.begin(), voice.end());
    }
    m.push_back(fm_sysex::checksum(m.data() + 6, 4096));
    m.push_back(0xF7);
    return m;
}

std::vector<float> render_voice(const PackedVoice* voice, int code, float hz, int frames, int skip = 0,
                                const SynthParams& params = default_synth_params()) {
    Synth s;
    s.start(code);
    s.set_fm_voice(voice ? voice->data() : nullptr);
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
double rms(const std::vector<float>& x, size_t from = 0, size_t to = 0) {
    if (!to) to = x.size();
    double e = 0;
    for (size_t i = from; i < to; ++i) e += double(x[i]) * x[i];
    return std::sqrt(e / double(to - from));
}
double frequency(const std::vector<float>& x) {
    double first = -1, last = -1;
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] <= 0 && x[i] > 0) {
            const double t = double(i - 1) - x[i - 1] / (x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            ++crossings;
        }
    return (crossings - 1) * kRate / (last - first);
}
int custom(int brightness, int envelope, int velocity) { return custom_code(kFmSysexModel, brightness, envelope, velocity); }

}  // namespace

int main() {
    // ── The reader ────────────────────────────────────────────────────────────────────────────
    std::vector<PackedVoice> voices;
    for (int i = 0; i < 32; ++i) {
        char name[11];
        std::snprintf(name, sizeof name, "SINE %02d  ", i + 1);
        voices.push_back(sine_voice(name, 1 + i % 4));
    }
    auto message = bank_message(voices, 5);
    assert(message.size() == size_t(fm_sysex::kBankMessageSize));
    {
        const auto bank = fm_sysex::parse(message.data(), message.size());
        assert(bank.count == 32 && bank.checksumOk);
        for (int i = 0; i < 32; ++i) assert(bank.voices[size_t(i)] == voices[size_t(i)]);
        assert(fm_sysex::voice_name(bank.voices[3]) == "SINE 04");
    }
    // Leading bytes (another message, a header) are skipped; the first bank is found.
    {
        std::vector<uint8_t> file = {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7, 0x00, 0x41};
        file.insert(file.end(), message.begin(), message.end());
        assert(fm_sysex::parse(file.data(), file.size()).count == 32);
    }
    // A wrong checksum is reported, the data still loads (every field is range-checked on unpacking).
    {
        auto wrong = message;
        wrong[wrong.size() - 2] ^= 0x01;
        const auto bank = fm_sysex::parse(wrong.data(), wrong.size());
        assert(bank.count == 32 && !bank.checksumOk);
    }
    // Rejected: an 8-bit data byte, a missing F7, a truncated message, another manufacturer, too large.
    {
        auto eightBit = message;
        eightBit[100] = 0x80;
        assert(fm_sysex::parse(eightBit.data(), eightBit.size()).count == 0);
        auto noEnd = message;
        noEnd.back() = 0x00;
        assert(fm_sysex::parse(noEnd.data(), noEnd.size()).count == 0);
        assert(fm_sysex::parse(message.data(), message.size() - 1).count == 0);
        auto roland = message;
        roland[1] = 0x41;
        assert(fm_sysex::parse(roland.data(), roland.size()).count == 0);
        std::vector<uint8_t> huge(fm_sysex::kMaxFileSize + 1, 0);
        std::copy(message.begin(), message.end(), huge.begin());
        assert(fm_sysex::parse(huge.data(), huge.size()).count == 0);
        assert(fm_sysex::parse(nullptr, 0).count == 0);
    }
    // A single-voice message (unpacked, 155 bytes) is packed into the bank layout: every field lands
    // where Plaits' unpacker reads it.
    {
        std::vector<uint8_t> vced(155, 0);
        for (int op = 0; op < 6; ++op) {
            uint8_t* o = vced.data() + op * 21;
            for (int i = 0; i < 4; ++i) { o[i] = uint8_t(10 + op + i); o[4 + i] = uint8_t(50 + op + i); }
            o[8] = uint8_t(30 + op); o[9] = 11; o[10] = 12; o[11] = 2; o[12] = 3; o[13] = 5; o[14] = 1; o[15] = 6;
            o[16] = uint8_t(90 - op); o[17] = 1; o[18] = uint8_t(op + 2); o[19] = 33; o[20] = 9;
        }
        uint8_t* g = vced.data() + 126;
        for (int i = 0; i < 8; ++i) g[i] = uint8_t(40 + i);
        g[8] = 21; g[9] = 6; g[10] = 1; g[11] = 44; g[12] = 12; g[13] = 7; g[14] = 8; g[15] = 1; g[16] = 3; g[17] = 5; g[18] = 30;
        const char* name = "PACKED ONE";
        for (int i = 0; i < 10; ++i) g[19 + i] = uint8_t(name[i]);
        std::vector<uint8_t> m = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
        m.insert(m.end(), vced.begin(), vced.end());
        m.push_back(fm_sysex::checksum(vced.data(), vced.size()));
        m.push_back(0xF7);
        const auto bank = fm_sysex::parse(m.data(), m.size());
        assert(bank.count == 1 && bank.checksumOk && fm_sysex::voice_name(bank.voices[0]) == "PACKED ONE");
        plaits::fm::Patch patch{};
        patch.Unpack(bank.voices[0].data());
        for (int op = 0; op < 6; ++op) {
            const auto& o = patch.op[op];
            assert(o.envelope.rate[2] == 12 + op && o.envelope.level[3] == 53 + op && o.keyboard_scaling.break_point == 30 + op);
            assert(o.keyboard_scaling.left_curve == 2 && o.keyboard_scaling.right_curve == 3 && o.rate_scaling == 5);
            assert(o.amp_mod_sensitivity == 1 && o.velocity_sensitivity == 6 && o.level == 90 - op && o.mode == 1);
            assert(o.coarse == op + 2 && o.fine == 33 && o.detune == 9);
        }
        assert(patch.pitch_envelope.rate[1] == 41 && patch.pitch_envelope.level[3] == 47);
        assert(patch.algorithm == 21 && patch.feedback == 6 && patch.reset_phase == 1);
        assert(patch.modulations.rate == 44 && patch.modulations.delay == 12 && patch.modulations.pitch_mod_depth == 7 &&
               patch.modulations.amp_mod_depth == 8 && patch.modulations.reset_phase == 1 && patch.modulations.waveform == 3 &&
               patch.modulations.pitch_mod_sensitivity == 5 && patch.transpose == 30);
    }
    // The project's hex form: exact round trip, anything else refused.
    {
        PackedVoice back{};
        assert(fm_sysex::from_hex(fm_sysex::to_hex(voices[7]), back) && back == voices[7]);
        PackedVoice untouched = voices[0];
        std::string hex = fm_sysex::to_hex(voices[7]);
        assert(!fm_sysex::from_hex(hex.substr(1), untouched) && untouched == voices[0]);
        hex[10] = 'g';
        assert(!fm_sysex::from_hex(hex, untouched));
        hex = fm_sysex::to_hex(voices[7]);
        hex[0] = '8';   // 0x8… is not a 7-bit byte
        assert(!fm_sysex::from_hex(hex, untouched) && untouched == voices[0]);
    }
    {
        PackedVoice v = voices[0];
        for (int i = 0; i < 10; ++i) v[size_t(118 + i)] = ' ';
        assert(fm_sysex::voice_name(v) == "(no name)");
        v[118] = 7;
        v[119] = 'A';
        assert(fm_sysex::voice_name(v) == "?A");
    }

    // ── Registration ──────────────────────────────────────────────────────────────────────────
    assert(model_count() == kVintageFirstModel + kVintageModelCount && kFmSysexModel == kDcoPolyModel + 1);
    assert(std::string(model_name(kFmSysexModel)) == "FM SYSEX" && is_vintage_model(kFmSysexModel));
    assert(std::string(macro_name(kFmSysexModel, 0)) == "Brightness" && std::string(macro_name(kFmSysexModel, 2)) == "Velocity");
    assert(synth_param_count(kFmSysexModel) == 4 && std::string(synth_param_name(kFmSysexModel, kFmBankParam)) == "Bank" &&
           std::string(synth_param_name(kFmSysexModel, kFmVoiceParam)) == "Voice");
    assert(synth_param_value(kFmSysexModel, kFmBankParam, fm_index_value(1)) == "Built-in B" &&
           synth_param_value(kFmSysexModel, kFmBankParam, fm_index_value(3)) == "Bank 1" &&
           synth_param_value(kFmSysexModel, kFmVoiceParam, fm_index_value(31)) == "Voice 32");
    assert(synth_param_adjust(kFmSysexModel, kFmVoiceParam, fm_index_value(31), 1, false) == fm_index_value(31) &&
           synth_param_adjust(kFmSysexModel, kFmVoiceParam, fm_index_value(4), 1, false) == fm_index_value(5));
    assert(preset_name(preset_code(kFmSysexModel, 0)) == "FM SYSEX / Solid Bass" && !synth_needs_strike(kFmSysexModel));

    // ── The voice ────────────────────────────────────────────────────────────────────────────
    const int plain = custom(128, 128, 255);
    // In tune at exactly 48 kHz: A4 within half a cent, and the coarse ratio sets the partial.
    {
        const auto x = render_voice(&voices[0], plain, 440.f, 48000, 4800);
        const double hz = frequency(x), cents = 1200 * std::log2(hz / 440.0);
        const auto p = spectrum(x);
        const double h2 = range(p, 870, 890) / range(p, 430, 450);
        std::printf("Sine voice on A4: %.3f Hz (%+.2f cents), H2/H1 %.1e\n", hz, cents, h2);
        assert(std::fabs(cents) < .5 && h2 < 1e-3);
        const auto x3 = render_voice(&voices[2], plain, 220.f, kFft, 4800);   // coarse 3
        const auto p3 = spectrum(x3);
        assert(range(p3, 650, 670) / total(p3) > .9);
    }
    // Velocity: a voice with full velocity sensitivity is louder at a hard key than at a soft one.
    {
        const auto sens = sine_voice("VELOCITY  ", 1, 99, 99, 7);
        const double soft = rms(render_voice(&sens, custom(128, 128, 40), 440.f, 9600, 4800));
        const double hard = rms(render_voice(&sens, custom(128, 128, 255), 440.f, 9600, 4800));
        std::printf("Velocity: rms %.4f soft, %.4f hard\n", soft, hard);
        assert(hard > 2 * soft);
    }
    // Brightness: a two-operator FM voice (OP2 modulates OP1) gains upper partials as it rises.
    {
        const auto fm2 = sine_voice("TWO OP FM ", 1, 99, 99, 0, 0, 80);   // algorithm 1: OP2 → OP1
        const auto dark = spectrum(render_voice(&fm2, custom(20, 128, 255), 220.f, kFft, 4800));
        const auto bright = spectrum(render_voice(&fm2, custom(240, 128, 255), 220.f, kFft, 4800));
        const double cd = centroid(dark, 50, 20000), cb = centroid(bright, 50, 20000);
        std::printf("Brightness on a 2-op voice: centroid %.0f Hz low, %.0f Hz high\n", cd, cb);
        assert(cb > 1.5 * cd);
    }
    // The envelope: a decaying voice fades while the note is held; the Envelope macro shortens it.
    {
        const auto pluck = sine_voice("PLUCK     ", 1, 40, 0);
        const auto x = render_voice(&pluck, custom(128, 128, 255), 440.f, 48000);
        const double start = rms(x, 480, 2400), later = rms(x, 36000, 48000);
        const auto fast = render_voice(&pluck, custom(128, 20, 255), 440.f, 48000);
        const double fastLater = rms(fast, 12000, 24000), slowSame = rms(x, 12000, 24000);
        std::printf("Envelope: rms %.4f at the attack, %.5f after 0.75 s; at 0.25–0.5 s %.5f short, %.5f as stored\n", start,
                    later, fastLater, slowSame);
        assert(later < start / 4 && fastLater < slowSame / 2);
    }
    // A new note opens the gate cleanly: the first block is silent, then the attack begins.
    {
        const auto x = render_voice(&voices[0], plain, 440.f, 480);
        for (int i = 0; i < 24; ++i) assert(x[size_t(i)] == 0.f);
        assert(rms(x, 240, 480) > .05);
    }
    // Without a voice of its own the model plays the built-in banks: by its Bank/Voice parameters, or
    // the named sound's voice — sample for sample the same as handing over those bytes.
    {
        PackedVoice builtIn{};
        fm_builtin_voice(1, 0, builtIn.data());
        auto params = default_synth_params();
        params[kFmBankParam] = fm_index_value(1);
        params[kFmVoiceParam] = fm_index_value(0);
        const auto byParams = render_voice(nullptr, plain, 220.f, 9600, 0, params);
        const auto byBytes = render_voice(&builtIn, plain, 220.f, 9600);
        assert(byParams == byBytes && rms(byBytes) > .01);
        const auto named = render_voice(nullptr, preset_code(kFmSysexModel, 1), 220.f, 9600);   // Electric Piano = B/1
        const auto namedBytes = render_voice(&builtIn, preset_code(kFmSysexModel, 1), 220.f, 9600);
        assert(named == namedBytes);
        // A file bank's index without its bytes falls back to built-in A rather than reading nothing.
        params[kFmBankParam] = fm_index_value(7);
        PackedVoice a0{};
        fm_builtin_voice(0, 0, a0.data());
        assert(render_voice(nullptr, plain, 220.f, 4800, 0, params) == render_voice(&a0, plain, 220.f, 4800));
    }
    // All sixteen named sounds play and stay bounded.
    {
        double quietest = 1, loudest = 0;
        for (int v = 0; v < variation_count(); ++v) {
            const auto x = render_voice(nullptr, preset_code(kFmSysexModel, v), 220.f, 24000);
            double peak = 0;
            for (float s : x) peak = std::max(peak, double(std::fabs(s)));
            assert(peak < 1.0);
            const double level = rms(x, 0, 12000);
            quietest = std::min(quietest, level);
            loudest = std::max(loudest, level);
        }
        std::printf("16 sounds on A3, first 0.25 s: rms %.3f … %.3f\n", quietest, loudest);
        assert(quietest > .01);
    }
    // Changing the voice while a note sounds: no NaN, no jump larger than the louder voice makes itself.
    {
        Synth s;
        s.start(plain);
        s.set_fm_voice(voices[0].data());
        s.update(plain, default_synth_params());
        float last = 0, worst = 0;
        for (int i = 0; i < 4800; ++i) last = s.next(220.f, kRate);
        s.set_fm_voice(voices[1].data());   // coarse 2
        for (int i = 0; i < 4800; ++i) {
            const float v = s.next(220.f, kRate);
            assert(std::isfinite(v));
            worst = std::max(worst, std::fabs(v - last));
            last = v;
        }
        assert(worst < .3f);
    }

    std::puts("PASS: FM SYSEX reads DX7 banks and single voices (checksum, 7-bit, F7, size, leading bytes, every field "
              "placed as Plaits unpacks it, hex round trip), registered in Vintage, in tune at 48 kHz, coarse ratio, "
              "velocity, brightness, envelope, clean gate, built-in banks and named sounds as their bytes, bounded, "
              "voice changes without NaN");
    return 0;
}
