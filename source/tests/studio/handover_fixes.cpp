// The three corrections from Codex's independent review of stages 1–3 (codex-pruefung/
// KORREKTURAUFTRAG-CLAUDE.md), each on the path the app really takes:
//   U2  settings.json stays right across the background save, failed writes and playback
//   K4  a live model change reaches the callback as one row: no new model on the old parameters
//   K3  re-enabling the master rack never replays frames from before the bypass
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/rack.h"
#include "studio/synth.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace pt::ui;

namespace {

// ── U2 ──────────────────────────────────────────────────────────────────────────────────────────
struct FlakyFileSystem : StdFileSystem {
    using StdFileSystem::StdFileSystem;
    bool failWrites = false;
    int writes = 0;
    bool write_file(const std::string& path, const std::string& content) override {
        ++writes;
        return !failWrites && StdFileSystem::write_file(path, content);
    }
};

int font_on_disk(StdFileSystem& fs) {
    SettingsValues v; Theme t;
    assert(load_settings(fs, v, t));
    return v.fontStyle;
}

void settings_checks(const std::filesystem::path& dir) {
    using W = SettingsWrite;
    FlakyFileSystem fs((dir / "u2").string());
    const Theme theme = theme_default();
    SettingsValues s; s.fontStyle = 3;
    SettingsAutosave autosave;   // the shell's single instance (BackgroundContext)

    // Saved state 3, as after an earlier session.
    assert(autosave.flush(fs, s, theme) == W::SAVED && font_on_disk(fs) == 3);
    assert(autosave.poll(fs, s, theme, 0, false) == W::UNCHANGED);

    // Codex's sequence: typeface 15 chosen, the app goes to the background before a poll sees it
    // (the watcher saves 15), comes back, typeface 3 again. The file must end on 3.
    s.fontStyle = 15;
    assert(autosave.flush(fs, s, theme) == W::SAVED && font_on_disk(fs) == 15);
    s.fontStyle = 3;
    assert(autosave.poll(fs, s, theme, 1000, false) == W::UNCHANGED);
    assert(autosave.poll(fs, s, theme, 2000, false) == W::SAVED);
    assert(font_on_disk(fs) == 3);

    // The same with the change seen by a poll first, then the background save, then the revert.
    s.fontStyle = 15;
    assert(autosave.poll(fs, s, theme, 3000, false) == W::UNCHANGED);
    assert(autosave.flush(fs, s, theme) == W::SAVED && font_on_disk(fs) == 15);
    s.fontStyle = 3;
    assert(autosave.poll(fs, s, theme, 4000, false) == W::UNCHANGED);
    assert(autosave.poll(fs, s, theme, 5000, false) == W::UNCHANGED);
    assert(autosave.poll(fs, s, theme, 6000, false) == W::SAVED && font_on_disk(fs) == 3);

    // A failed background save confirms nothing: the poll retries and lands the value.
    s.fontStyle = 9;
    fs.failWrites = true;
    assert(autosave.flush(fs, s, theme) == W::FAILED && font_on_disk(fs) == 3);
    assert(autosave.poll(fs, s, theme, 7000, false) == W::UNCHANGED);
    assert(autosave.poll(fs, s, theme, 9000, false) == W::FAILED && font_on_disk(fs) == 3);   // still failing
    fs.failWrites = false;
    assert(autosave.poll(fs, s, theme, 10000, false) == W::UNCHANGED);   // backs off one quiet period
    assert(autosave.poll(fs, s, theme, 11000, false) == W::SAVED && font_on_disk(fs) == 9);

    // Playback postpones the periodic write, however long; the first quiet check after it writes.
    s.fontStyle = 12;
    const int before = fs.writes;
    for (uint64_t t = 12000; t <= 30000; t += 1000) assert(autosave.poll(fs, s, theme, t, true) == W::UNCHANGED);
    assert(fs.writes == before && font_on_disk(fs) == 9);
    assert(autosave.poll(fs, s, theme, 31000, false) == W::SAVED && font_on_disk(fs) == 12);

    // An unchanged flush neither writes nor disturbs the cache.
    const int quiet = fs.writes;
    assert(autosave.flush(fs, s, theme) == W::UNCHANGED && fs.writes == quiet);
    assert(autosave.poll(fs, s, theme, 40000, false) == W::UNCHANGED && fs.writes == quiet);
}

// ── K4 ──────────────────────────────────────────────────────────────────────────────────────────
constexpr int kBlock = 256;
struct Fixture {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    explicit Fixture(const std::string& uri) {
        engine->setDeviceSampleRate(48000);
        auto p = songcore::make_default_project();
        auto& ins = p.instruments[0];
        ins.sampleFilePath = uri; ins.loopMode = "fwd"; ins.volume = 170;
        assert(host.push_project(songcore::serialize_project(p)));
        assert(host.load_media("").failed == 0);
        host.push_params();
        host.preview_instrument(0);
    }
    std::vector<float> render(int blocks) {
        std::vector<float> out; float data[kBlock * 2];
        for (int b = 0; b < blocks; ++b) {
            engine->processLiveBlock(data, kBlock, 2, 48000);
            out.insert(out.end(), data, data + kBlock * 2);
        }
        return out;
    }
};
double energy(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return e; }

void live_model_checks() {
    const std::string csaw = studio::custom_uri(24, 50, 80, 100);
    const int morph = studio::custom_code(25, 50, 80, 100);
    auto marked = studio::default_synth_params(); marked[0] = 0;   // Braids bit depth 4
    const int slot = 0;

    Fixture steady(csaw), raced(csaw), defaults(csaw);
    for (auto* f : {&steady, &raced, &defaults}) {
        assert(f->engine->setLiveStudioSynth(slot, studio::custom_code(24, 50, 80, 100), marked));
        assert(energy(f->render(6)) > 1e-3);
    }
    // The window Codex found: the callback already sees the new code in studioCodes but has not yet
    // taken the matching row. It must keep the CSAW voice with its parameters.
    raced.engine->studioCodes[slot].store(morph, std::memory_order_release);
    assert(raced.render(3) == steady.render(3));
    // Then the row arrives with the code: both switch identically, on the published parameters.
    assert(steady.engine->setLiveStudioSynth(slot, morph, marked));
    assert(raced.engine->setLiveStudioSynth(slot, morph, marked));
    assert(defaults.engine->setLiveStudioSynth(slot, morph, studio::default_synth_params()));
    const auto a = steady.render(8), b = raced.render(8), c = defaults.render(8);
    assert(a == b);
    assert(a != c);   // the new model plays the parameters published with it, not defaults

    // setLiveStudioCode alone (other control paths) moves the row too, keeping its parameters.
    Fixture codeOnly(csaw), viaRow(csaw);
    for (auto* f : {&codeOnly, &viaRow}) {
        assert(f->engine->setLiveStudioSynth(slot, studio::custom_code(24, 50, 80, 100), marked));
        f->render(4);
    }
    assert(codeOnly.engine->setLiveStudioCode(slot, morph));
    assert(viaRow.engine->setLiveStudioSynth(slot, morph, marked));
    assert(codeOnly.render(6) == viaRow.render(6));
    // Codex's follow-up: the slot's source is replaced (same slot, back to CSAW) after a live switch to
    // MORPH was synced but before the next sync. The synced row describes the previous source, so a
    // note on the new source must play CSAW on factory parameters, never MORPH from the old row.
    {
        AudioEngine e; e.setDeviceSampleRate(48000);
        const int csawCode = studio::preset_code(24, 0), morphCode = studio::preset_code(25, 0);
        float data[kBlock * 2];
        assert(e.loadSampleFromWavFile(slot, studio::uri(csawCode).c_str()) > 0);
        e.processLiveBlock(data, kBlock, 2, 48000);
        assert(e.liveStudioCode(slot) == csawCode);
        assert(e.setLiveStudioSynth(slot, morphCode, marked));
        assert(e.liveStudioCode(slot) == csawCode);   // a regular live edit: old voice until the sync
        e.processLiveBlock(data, kBlock, 2, 48000);
        assert(e.liveStudioCode(slot) == morphCode && e.liveStudioParams(slot) == marked);
        assert(e.loadSampleFromWavFile(slot, studio::uri(csawCode).c_str()) > 0);   // no sync after it
        assert(e.studioCodes[slot].load() == csawCode);
        assert(e.liveStudioCode(slot) == csawCode && e.liveStudioParams(slot) == studio::default_synth_params());
        e.processLiveBlock(data, kBlock, 2, 48000);
        assert(e.liveStudioCode(slot) == csawCode && e.liveStudioParams(slot) == studio::default_synth_params());
        // The same live edit after the reload behaves as before: old voice until its row is synced.
        assert(e.setLiveStudioSynth(slot, morphCode, marked));
        assert(e.liveStudioCode(slot) == csawCode);
        e.processLiveBlock(data, kBlock, 2, 48000);
        assert(e.liveStudioCode(slot) == morphCode && e.liveStudioParams(slot) == marked);
    }
    // A slot that is not a synth refuses both.
    assert(!codeOnly.engine->setLiveStudioSynth(7, morph, marked) && !codeOnly.engine->setLiveStudioCode(7, morph));
}

// ── K3 ──────────────────────────────────────────────────────────────────────────────────────────
void rack_checks() {
    using namespace studio;
    for (int fx : {0, 1, 7, 16}) {
        Rack rack;
        rack.control[FX].store(fx); rack.control[FX_MIX].store(fx ? 200 : 0); rack.control[ENABLE].store(1);
        std::vector<float> in(96, 0.f); in[0] = in[1] = .5f;
        rack.process(in.data(), 48, 2, 48000);
        rack.control[ENABLE].store(0);
        std::vector<float> bypass(9600, 0.f);
        rack.process(bypass.data(), 4800, 2, 48000);   // 100 ms silent bypass
        for (float v : bypass) assert(v == 0.f);
        rack.control[ENABLE].store(1);
        std::vector<float> after(2 * 4800, 0.f);       // 100 ms of silence after re-enabling
        for (int at = 0; at < 4800; at += 48) rack.process(after.data() + 2 * at, 48, 2, 48000);
        // Some effects sound on silence (Warps' internal carrier): compare with a rack that never
        // heard the impulse, switched on the same way. Equal means nothing from before the bypass.
        Rack fresh;
        fresh.control[FX].store(fx); fresh.control[FX_MIX].store(fx ? 200 : 0); fresh.control[ENABLE].store(1);
        std::vector<float> reference(2 * 4800, 0.f);
        for (int at = 0; at < 4800; at += 48) fresh.process(reference.data() + 2 * at, 48, 2, 48000);
        double peak = 0, diff = 0;
        for (size_t i = 0; i < after.size(); ++i) { peak = std::fmax(peak, std::fabs(after[i])); diff = std::fmax(diff, std::fabs(after[i] - reference[i])); }
        std::printf("K3 fx %2d: re-enabled peak %.6f, differs from a fresh rack by %.2e\n", fx, peak, diff);
        assert(diff < 1e-6);
        if (fx == 0) assert(peak < 1e-6);   // bare rack: silence stays silence
    }
    // Continuously enabled: still 48 frames of latency whatever FX and MIX say.
    for (int fx : {0, 16}) for (int mix : {0, 255}) {
        Rack rack;
        rack.control[ENABLE].store(1); rack.control[FX].store(fx); rack.control[FX_MIX].store(mix);
        std::vector<float> buf(2 * 4096, 0.f); buf[2 * 1000] = buf[2 * 1000 + 1] = .5f;
        for (int at = 0; at < 4096; at += 64) rack.process(buf.data() + 2 * at, 64, 2, 48000);
        if (fx == 0 || mix == 0) {
            int peak = 0; for (int i = 0; i < 4096; ++i) if (std::fabs(buf[2 * i]) > std::fabs(buf[2 * peak])) peak = i;
            assert(peak == 1048);
        }
    }
    // A fresh impulse after re-enabling passes normally, 48 frames late.
    Rack rack; rack.control[ENABLE].store(1);
    std::vector<float> warm(2 * 480, 0.f); rack.process(warm.data(), 480, 2, 48000);
    rack.control[ENABLE].store(0); rack.process(warm.data(), 480, 2, 48000);
    rack.control[ENABLE].store(1);
    std::vector<float> buf(2 * 480, 0.f); buf[2 * 100] = buf[2 * 100 + 1] = .5f;
    rack.process(buf.data(), 480, 2, 48000);
    assert(std::fabs(buf[2 * 148]) > .1f && std::fabs(buf[2 * 100]) < 1e-6f);
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto dir = std::filesystem::absolute(argv[1]);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    settings_checks(dir);
    live_model_checks();
    rack_checks();
    std::puts("PASS: U2 background save/revert/failure/playback keep settings.json right; K4 live model change waits for its row, old voice keeps its parameters, a reloaded source never takes the previous source's row; K3 re-enabled rack replays nothing, constant 48-frame latency");
    return 0;
}
