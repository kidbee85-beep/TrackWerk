// FM SYSEX in a song: the voice an instrument carries survives saving and loading, reaches the
// callback through the instrument's synth row, changes a ringing note without allocation, and the
// Studio's bank list reads .syx files from the SysEx folder. Run directory: argv[1] (made fresh).
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/fm_sysex.h"
#include "studio/synth.h"
#include "ui/app_state.h"
#include "ui/fm_library.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {
std::atomic<bool> audit{false};
std::atomic<int> allocations{0};
}  // namespace
void* operator new(std::size_t bytes) {
    if (audit.load()) ++allocations;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
using songcore::json;
using studio::fm_sysex::PackedVoice;
constexpr int kBlock = 256;

PackedVoice sine_voice(const char* name, int coarse) {
    PackedVoice v{};
    for (int op = 0; op < 6; ++op) {
        uint8_t* o = v.data() + op * 17;
        for (int i = 0; i < 4; ++i) o[i] = 99;
        o[4] = 99; o[5] = 99; o[6] = 99; o[7] = 0; o[8] = 39;
        o[12] = uint8_t(7 << 3);
        o[14] = uint8_t(op == 5 ? 99 : 0);
        o[15] = uint8_t((op == 5 ? coarse : 1) << 1);
    }
    for (int i = 0; i < 4; ++i) { v[size_t(102 + i)] = 99; v[size_t(106 + i)] = 50; }
    v[110] = 31; v[111] = 1 << 3; v[112] = 35; v[117] = 24;
    for (int i = 0; i < 10; ++i) v[size_t(118 + i)] = uint8_t(name[i] ? name[i] : ' ');
    return v;
}

struct Fixture {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    explicit Fixture(const std::optional<PackedVoice>& voice) {
        engine->setDeviceSampleRate(48000);
        auto p = songcore::make_default_project();
        p.tracks[0].chainRefs = {0};
        p.chains[0].phraseRefs[0] = 0;
        p.phrases[0].steps[0].note = songcore::Note::C4();
        p.phrases[0].steps[0].instrument = 0;
        auto& ins = p.instruments[0];
        ins.sampleFilePath = studio::custom_uri(studio::kFmSysexModel, 128, 128, 255);
        ins.loopMode = "fwd";
        ins.volume = 170;
        ins.modSlots = std::vector<songcore::ModSlot>(4);
        ins.fmVoice = voice;
        assert(host.push_project(songcore::serialize_project(p)));
        assert(host.load_media("").failed == 0);
        host.push_params();
    }
};
std::vector<float> render(AudioEngine& engine, int blocks, bool noAllocation = false) {
    std::vector<float> out;
    out.reserve(size_t(blocks * kBlock * 2));
    float data[kBlock * 2];
    if (noAllocation) { allocations = 0; audit = true; }
    for (int i = 0; i < blocks; ++i) {
        engine.processLiveBlock(data, kBlock, 2, 48000);
        for (float s : data) { assert(std::isfinite(s)); out.push_back(s); }
    }
    audit = false;
    if (noAllocation) assert(allocations == 0);
    return out;
}
// Mono zero-crossing frequency over the second half of a stereo buffer.
double frequency(const std::vector<float>& v) {
    double first = -1, last = -1;
    int crossings = 0;
    for (size_t i = v.size() / 2 + 2; i < v.size(); i += 2) {
        const double a = v[i - 2] + v[i - 1], b = v[i] + v[i + 1];
        if (a <= 0 && b > 0) {
            const double t = double(i / 2 - 1) - a / (b - a);
            if (first < 0) first = t;
            last = t;
            ++crossings;
        }
    }
    assert(crossings > 2);
    return (crossings - 1) * 48000.0 / (last - first);
}
double energy(const std::vector<float>& v) { double e = 0; for (float x : v) e += double(x) * x; return e; }
std::vector<uint8_t> bank_file(const std::vector<PackedVoice>& voices) {
    std::vector<uint8_t> m = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    for (int v = 0; v < 32; ++v) m.insert(m.end(), voices[size_t(v) % voices.size()].begin(), voices[size_t(v) % voices.size()].end());
    m.push_back(studio::fm_sysex::checksum(m.data() + 6, 4096));
    m.push_back(0xF7);
    return m;
}
void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path dir = argv[1];
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto octave = sine_voice("OCTAVE UP ", 2), unison = sine_voice("UNISON    ", 1);

    // ── The project keeps the voice ───────────────────────────────────────────────────────────
    {
        auto p = songcore::make_default_project();
        p.instruments[3].fmVoice = octave;
        const std::string text = songcore::serialize_project(p);
        assert(text.find("\"fmVoice\":\"" + studio::fm_sysex::to_hex(octave) + "\"") != std::string::npos);
        const auto back = songcore::parse_project(json::parse(text));
        assert(back.instruments[3].fmVoice && *back.instruments[3].fmVoice == octave);
        assert(!back.instruments[2].fmVoice && text.find("\"fmVoice\"", text.find("\"fmVoice\"") + 1) == std::string::npos);
        // A damaged voice is dropped, not half-read; the rest of the project loads.
        auto j = json::parse(text);
        for (auto& ins : j["instruments"])
            if (ins.contains("fmVoice")) ins["fmVoice"] = studio::fm_sysex::to_hex(octave).substr(2);
        const auto damaged = songcore::parse_project(j);
        assert(!damaged.instruments[3].fmVoice);
        // An instrument preset (.pti) carries it too.
        songcore::InstrumentPreset ip;
        ip.instrument = p.instruments[3];
        const auto pti = songcore::serialize_instrument_preset(ip);
        assert(pti.find(studio::fm_sysex::to_hex(octave)) != std::string::npos);
    }

    // ── The callback plays the instrument's voice ──────────────────────────────────────────────
    double withVoice = 0, builtIn = 0;
    {
        Fixture f(octave);
        f.host.play_song(0);
        f.host.poll();
        withVoice = frequency(render(*f.engine, 40));
        Fixture g(unison);
        g.host.play_song(0);
        g.host.poll();
        const double plain = frequency(render(*g.engine, 40));
        std::printf("C4 note: %.2f Hz with the octave voice, %.2f Hz with the unison voice\n", withVoice, plain);
        assert(std::fabs(withVoice / plain - 2.0) < .002);
        assert(std::fabs(plain - 261.63) < 1.0);
        Fixture h(std::nullopt);
        h.host.play_song(0);
        h.host.poll();
        builtIn = energy(render(*h.engine, 40));
        assert(builtIn > 1e-3);   // no voice of its own: the built-in bank still sounds
    }

    // ── Changing the voice of a ringing note: heard at once, no allocation in the callback ──────
    {
        Fixture f(unison);
        f.host.play_song(0);
        f.host.poll();
        const double before = frequency(render(*f.engine, 40));
        const std::optional<std::array<uint8_t, 128>> next = octave;
        const auto uri = *f.host.project().instruments[0].sampleFilePath;
        assert(f.host.update_studio_synth(0, uri, f.host.project().instruments[0].studioSynthParams, &next));
        const double after = frequency(render(*f.engine, 40, true));
        std::printf("Ringing note: %.2f Hz, after the voice change %.2f Hz\n", before, after);
        assert(std::fabs(after / before - 2.0) < .002);
        assert(f.host.project().instruments[0].fmVoice == next);
        // Clearing it (an empty optional) returns to the built-in banks; nullptr leaves it alone.
        const std::optional<std::array<uint8_t, 128>> none;
        assert(f.host.update_studio_synth(0, uri, f.host.project().instruments[0].studioSynthParams, nullptr));
        assert(f.host.project().instruments[0].fmVoice == next);
        assert(f.host.update_studio_synth(0, uri, f.host.project().instruments[0].studioSynthParams, &none));
        assert(!f.host.project().instruments[0].fmVoice);
        render(*f.engine, 2);
        assert(f.engine->liveStudioFmVoice(0) == nullptr);
    }

    // ── Instrument presets with a voice: a valid one loads, a damaged one is refused whole ─────
    {
        Fixture f(std::nullopt);
        songcore::InstrumentPreset ip;
        ip.instrument = f.host.project().instruments[0];
        ip.instrument.fmVoice = octave;
        const auto good = dir / "good.pti";
        { std::ofstream(good) << songcore::serialize_instrument_preset(ip); }
        assert(f.host.load_instrument_preset(1, good.string()));
        assert(f.host.project().instruments[1].fmVoice == ip.instrument.fmVoice);
        auto j = json::parse(songcore::serialize_instrument_preset(ip));
        j["instrument"]["fmVoice"] = "zz";
        const auto bad = dir / "bad.pti";
        { std::ofstream(bad) << j.dump(); }
        const auto previous = f.host.project().instruments[2].fmVoice;
        assert(!f.host.load_instrument_preset(2, bad.string()));
        assert(f.host.project().instruments[2].fmVoice == previous);
    }

    // ── The Studio's bank list: the SysEx folder, by name ────────────────────────────────────
    {
        pt::ui::StdFileSystem fs((dir / "app").string());
        const std::filesystem::path sysex = fs.make_app_folder("SysEx");
        write(sysex / "b-second.syx", bank_file({octave}));
        write(sysex / "a-first.SYX", bank_file({unison, octave}));
        write(sysex / "c-broken.syx", std::vector<uint8_t>(200, 0x42));
        write(sysex / "notes.txt", bank_file({octave}));
        pt::ui::FmLibrary library;
        pt::ui::fm_library_scan(library, fs);
        assert(library.scanned && library.files.size() == 2 && library.skipped == 1 && library.badChecksums == 0);
        assert(pt::ui::fm_library_bank_count(library) == 5);
        assert(pt::ui::fm_library_bank_label(library, 0) == "Built-in A" && pt::ui::fm_library_bank_label(library, 3) == "a-first" &&
               pt::ui::fm_library_bank_label(library, 4) == "b-second");
        PackedVoice v{};
        assert(pt::ui::fm_library_voice(library, 3, 1, v) && v == octave);
        assert(pt::ui::fm_library_voice(library, 4, 31, v) && v == octave);
        assert(!pt::ui::fm_library_voice(library, 5, 0, v) && !pt::ui::fm_library_voice(library, 3, 32, v));
        PackedVoice builtIn{};
        studio::fm_builtin_voice(2, 5, builtIn.data());
        assert(pt::ui::fm_library_voice(library, 2, 5, v) && v == builtIn);
        assert(pt::ui::fm_library_voice_count(library, 4) == 32 && pt::ui::fm_library_voice_count(library, 9) == 0);
    }

    // ── The Studio: stepping Bank and Voice on a live-bound instrument ────────────────────────────
    {
        pt::ui::StdFileSystem fs((dir / "studio").string());
        const std::filesystem::path sysex = fs.make_app_folder("SysEx");
        write(sysex / "chip-banks.syx", bank_file({unison, octave}));
        Fixture f(std::nullopt);
        pt::ui::AppState s;
        s.project = &f.host.edit_project();
        s.studioOpen = true;
        s.studioTab = 0;
        s.currentInstrument = 0;
        pt::ui::open_studio_synth(s, f.host.project().instruments[0]);
        pt::ui::studio_synth_sync_target(s);
        assert(s.studioModel == studio::kFmSysexModel && pt::ui::studio_synth_live(s));
        s.studioSynthPage = 1;                               // Output tone, Drive, Bank
        assert(pt::ui::studio_synth_control_value(s, 2) == "Built-in A");
        for (int i = 0; i < 3; ++i) pt::ui::studio_synth_edit(s, f.host, *f.engine, 3 + 2, 1, false, &fs);
        assert(pt::ui::studio_synth_control_value(s, 2) == "chip-banks");
        assert(s.studioDraft.fmVoice && *s.studioDraft.fmVoice == unison);
        assert(f.host.project().instruments[0].fmVoice == s.studioDraft.fmVoice);   // live: stored at once
        pt::ui::studio_synth_edit(s, f.host, *f.engine, 3 + 2, 1, false, &fs);    // no further bank: stays
        assert(pt::ui::studio_synth_control_value(s, 2) == "chip-banks");
        s.studioSynthPage = 2;                               // Voice
        assert(pt::ui::studio_synth_control_value(s, 0) == "1 UNISON");
        pt::ui::studio_synth_edit(s, f.host, *f.engine, 3, 1, false, &fs);
        assert(pt::ui::studio_synth_control_value(s, 0) == "2 OCTAVE UP");
        const std::optional<std::array<uint8_t, 128>> expected = octave;
        assert(f.host.project().instruments[0].fmVoice == expected);
        // Back to a built-in bank: the copy is dropped, the built-in voice plays from the parameters.
        s.studioSynthPage = 1;
        pt::ui::studio_synth_edit(s, f.host, *f.engine, 3 + 2, -1, false, &fs);
        assert(pt::ui::studio_synth_control_value(s, 2) == "Built-in C" && !s.studioDraft.fmVoice);
        assert(!f.host.project().instruments[0].fmVoice);
        std::printf("Studio: Built-in A → chip-banks (voice copied into the song) → Built-in C (copy dropped)\n");
    }

    std::puts("PASS: FM SYSEX voice saved with the project and .pti (damaged hex dropped or refused), played by the "
              "callback (octave voice an octave up, built-in banks without one), changed on a ringing note without "
              "allocation, cleared and kept as asked; SysEx folder banks listed by name, broken files skipped; Studio steps Bank/Voice and copies a file voice into the live instrument, dropping it again for a built-in bank");
    return 0;
}
