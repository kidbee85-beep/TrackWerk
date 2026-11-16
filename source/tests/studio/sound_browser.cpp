// ptsound-browser-check: the sound browser (TrackWerk 1.2, stage 1) — the Studio's PRESET picker with
// a MODEL tab (the model's sixteen presets, as before) and tabs over the factory instrument presets
// the app ships (Presets/Instruments/PocketStudio_MegaPack, the real 2048 .pti files are copied in):
// FAV, RECENT, ALL and the categories. Covered, against Codex' acceptance list for stage 1:
//  1. a playing song keeps playing; previewing other sounds and B leaves the project byte-identical
//  2. A uses the sound in exactly the marked slot; B after a use puts the slot back (empty or synth
//     slot), A again keeps it; a sample slot asks first and says there is no way back
//  3. stars, recent and the preview level survive save → load; older files read; damaged keys dropped
//  4. an empty tab, the first and last row, a full favourites bank
//  5. native images of the browser in dark, light and monochrome themes
//  6. no preview while a resample runs; focus loss ends a held preview
// and Codex' three edge cases: the root note A plays is owned and ends; wrong-typed .pti files are
// refused by preview and LOAD alike; the preview level is louder up to the sound's ceiling and says so.
// usage: ptsound-browser-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/layout.h"
#include "ui/settings_store.h"
#include "ui/sound_browser.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include "ui/studio_synth_picker.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#ifndef PT_TREE
#error "PT_TREE must name the source tree (for the factory presets)"
#endif

using namespace pt::ui;
namespace fsys = std::filesystem;

namespace {

void u16(std::ofstream& f, unsigned v) { char b[] = {char(v), char(v >> 8)}; f.write(b, 2); }
void u32(std::ofstream& f, unsigned v) { u16(f, v); u16(f, v >> 16); }
void wave(const fsys::path& path) {
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4); u32(f, 36 + 1024); f.write("WAVEfmt ", 8);
    u32(f, 16); u16(f, 1); u16(f, 1); u32(f, 48000); u32(f, 96000); u16(f, 2); u16(f, 16);
    f.write("data", 4); u32(f, 1024);
    for (int i = 0; i < 512; ++i) u16(f, unsigned(int(std::sin(i * .08) * 10000)));
}

void shot(const fsys::path& path, AppState& s) {
    Canvas c;
    TrackerLayout layout;
    layout.draw(c, s);
    std::ofstream f(path, std::ios::binary);
    f << "P6\n640 480\n255\n";
    for (int i = 0; i < 640 * 480; ++i) {
        const auto p = c.pixels()[i];
        const char rgb[] = {static_cast<char>(p >> 16), static_cast<char>(p >> 8), static_cast<char>(p)};
        f.write(rgb, 3);
    }
}

// The instrument folder can move under a running app (Android SET_HOME / FORGET_ROOT): `folder`
// stands for that, as in Codex' sound-independent.cpp.
struct SwitchFs : StdFileSystem {
    std::string folder;
    explicit SwitchFs(const std::string& home) : StdFileSystem(home) {}
    std::string instruments_directory() override {
        return folder.empty() ? StdFileSystem::instruments_directory() : folder;
    }
};

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState s;
    SwitchFs fs;
    ButtonMods held;
    explicit Rig(const fsys::path& home, bool withPack) : fs(home.string()) {
        assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
        s.project = &host.edit_project();
        if (withPack) {
            const fsys::path from = fsys::path(PT_TREE) / "factory/Presets/Instruments" / kSoundPackFolder;
            fsys::copy(from, fsys::path(fs.instruments_directory()) / kSoundPackFolder, fsys::copy_options::recursive);
        }
    }
    void send(Button b, ButtonAction a) { studio_button({b, a, held}, s, host, *engine, fs); }
    void tap(Button b) { send(b, ButtonAction::PRESSED); send(b, ButtonAction::RELEASED); }
    void a() { held.a = true; send(Button::A, ButtonAction::PRESSED); held.a = false; send(Button::A, ButtonAction::RELEASED); }
    void a_with(Button b) {   // A held, then the D-pad: A + Left / Right / Up / Down
        held.a = true;
        send(Button::A, ButtonAction::PRESSED);
        tap(b);
        held.a = false;
        send(Button::A, ButtonAction::RELEASED);
    }
    void b() { send(Button::B, ButtonAction::PRESSED); send(Button::B, ButtonAction::RELEASED); }
    void l_down() { send(Button::L_SHIFT, ButtonAction::PRESSED); held.l = true; }
    void l_up() { held.l = false; send(Button::L_SHIFT, ButtonAction::RELEASED); }
    // The Studio's synth page on `slot`, then A on PRESET opens the browser.
    void open_browser(int slot) {
        s.currentInstrument = slot;
        open_studio_synth(s, host.project().instruments[static_cast<size_t>(slot)]);
        s.studioRow = 2;
        a();
        assert(studio_synth_picker_open(s) && s.studioSynthPicker.mode == StudioSynthPickerMode::Preset);
    }
    void to_tab(SoundTab t) {
        while (s.studioSynthPicker.tab != t) {
            const int before = static_cast<int>(s.studioSynthPicker.tab);
            tap(static_cast<int>(t) > before ? Button::DPAD_RIGHT : Button::DPAD_LEFT);
            assert(static_cast<int>(s.studioSynthPicker.tab) != before);
        }
    }
    std::string bytes() const { return songcore::serialize_project(host.project()); }
    int row() const { return s.studioSynthPicker.rows[static_cast<size_t>(s.studioSynthPicker.tab)]; }
    // Audio blocks: finite always; true when something sounded.
    bool render(int blocks) {
        float audio[1024]{};
        double energy = 0;
        for (int b = 0; b < blocks; ++b) {
            engine->processLiveBlock(audio, 512, 2, 48000);
            for (const float x : audio) { assert(std::isfinite(x)); energy += double(x) * x; }
        }
        return energy > 1e-6;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || fsys::exists(argv[1])) {
        std::cerr << "usage: ptsound-browser-check <new run directory>\n";
        return 2;
    }
    const fsys::path dir = fsys::absolute(argv[1]);
    fsys::create_directories(dir / "images");

    // Keys: stable pack paths; anything else is refused.
    assert(sound_key_valid("Synths/01_Bass/Acid_Motion/Acid_Motion_Bite_Open.pti"));
    assert(sound_key_valid("Drums/01_Kicks-Sub/FM_Kick/FM_Kick_Short.pti"));
    for (const char* bad : {"../x.pti", "Synths/../../etc/passwd.pti", "/Synths/a/b/c.pti", "Synths/a/b/c.wav",
                            "Synths/a b/c/d.pti", "Other/a/b/c.pti", "Synths/a/c.pti", ""})
        assert(!sound_key_valid(bad));
    {
        const auto e = sound_entry_from_key("Synths/05_Plucks/Glass_Pick/Glass_Pick_Soft_Tight.pti");
        assert(e.family == "Glass Pick" && e.variant == "Soft Tight" && e.group == "PLUCK");
        assert(sound_entry_from_key("Drums/05_Open-Hats/X/X_A.pti").group == "OPEN-HATS");
    }

    Rig r(dir / "home", true);
    auto& s = r.s;
    auto& host = r.host;

    // Slot 00 becomes a live Studio synth (the Studio's APPLY), slot 01 a sample, slot 02 stays empty.
    s.studioOpen = true;
    s.studioTab = 0;
    r.open_browser(0);
    r.b();
    s.studioRow = studio_synth_apply_row(s);
    r.a();
    assert(studio_synth_live(s));
    host.stop_preview(true);
    wave(dir / "tone.wav");
    assert(host.load_sample(1, (dir / "tone.wav").string()));
    host.set_instrument_type(1, songcore::InstrumentType::SAMPLER);
    s.projectVersion = 7;

    // ── 1. Browse and listen while the song plays: nothing in the project moves ──
    host.play_song(0);
    const std::string before = r.bytes();
    r.open_browser(0);
    assert(s.studioSynthPicker.tab == SoundTab::MODEL);
    shot(dir / "images/1-model-tab.ppm", s);
    r.to_tab(SoundTab::ALL);
    assert(s.soundLibrary.found && s.soundLibrary.entries[static_cast<size_t>(SoundTab::ALL)].size() == 2048);
    int categories = 0;
    for (int t = static_cast<int>(SoundTab::BASS); t < kSoundTabCount; ++t) {
        const auto n = s.soundLibrary.entries[static_cast<size_t>(t)].size();
        assert(n > 0);
        categories += static_cast<int>(n);
    }
    assert(categories == 2048);   // every factory sound in exactly one category
    assert(s.soundLibrary.entries[static_cast<size_t>(SoundTab::DRUM)].size() == 768);
    r.to_tab(SoundTab::BASS);
    assert(s.soundLibrary.entries[static_cast<size_t>(SoundTab::BASS)].size() == 128);
    // First and last row: Up at the top stays, A + Down pages by 8, the end clamps.
    r.tap(Button::DPAD_UP);
    assert(r.row() == 0);
    r.a_with(Button::DPAD_DOWN);
    assert(r.row() == 8 && studio_synth_picker_open(s));   // a page, not a use
    for (int i = 0; i < 20; ++i) r.a_with(Button::DPAD_DOWN);
    assert(r.row() == 127);
    r.tap(Button::DPAD_DOWN);
    assert(r.row() == 127);
    for (int i = 0; i < 20; ++i) r.a_with(Button::DPAD_UP);
    assert(r.row() == 0);
    r.l_down();
    assert(s.studioSynthPicker.previewSounding && host.is_playing());
    assert(r.render(40));
    r.tap(Button::DPAD_DOWN);                         // held L follows the cursor
    assert(s.studioSynthPicker.previewSounding);
    r.l_up();
    assert(!s.studioSynthPicker.previewSounding);
    // The preview level: A + Left / Right, 10 % steps, bounded; louder previews stay finite.
    r.a_with(Button::DPAD_RIGHT);
    assert(s.settings.soundPreviewLevel == 110);
    for (int i = 0; i < 30; ++i) r.a_with(Button::DPAD_RIGHT);
    assert(s.settings.soundPreviewLevel == kSoundPreviewLevelMax);
    r.l_down();
    r.render(40);
    r.l_up();
    for (int i = 0; i < 30; ++i) r.a_with(Button::DPAD_LEFT);
    assert(s.settings.soundPreviewLevel == kSoundPreviewLevelMin);
    while (s.settings.soundPreviewLevel != 100) r.a_with(Button::DPAD_RIGHT);
    assert(r.bytes() == before && s.projectVersion == 7 && s.settings.soundRecent.empty());
    r.b();
    assert(!studio_synth_picker_open(s) && r.bytes() == before && s.projectVersion == 7);
    assert(host.is_playing());
    host.stop();

    // ── 2. A uses the sound in the marked slot; B puts the slot back; A again keeps it ──
    r.open_browser(0);
    assert(s.studioSynthPicker.tab == SoundTab::BASS && r.row() == 1);   // reopens where it was left
    const std::string key = s.soundLibrary.entries[static_cast<size_t>(SoundTab::BASS)][1].key;
    r.a();
    assert(studio_synth_picker_open(s) && s.studioSynthPicker.usedKey == key && s.studioSynthPicker.original);
    {
        // Slot 00 now holds exactly that .pti's sound.
        std::string text;
        assert(r.fs.read_file(sound_pack_root(r.fs) + "/" + key, text));
        const auto ip = songcore::parse_instrument_preset(songcore::json::parse(text));
        assert(host.project().instruments[0].sampleFilePath == ip.instrument.sampleFilePath);
        assert(host.project().instruments[0].filterCut == ip.instrument.filterCut && r.bytes() != before);
    }
    assert(host.project().instruments[1].sampleFilePath->find("tone.wav") != std::string::npos);   // only slot 00
    assert(s.projectVersion == 8 && s.settings.soundRecent == std::vector<std::string>{key});
    assert(studio_synth_live(s));                                        // the synth page follows the slot
    shot(dir / "images/2-used-undo-offered.ppm", s);
    r.b();                                                               // undo
    assert(!studio_synth_picker_open(s));
    assert(r.bytes() == before && s.projectVersion == 7 && s.settings.soundRecent.empty());
    r.open_browser(0);
    r.a();
    r.a();                                                               // A again: keep
    assert(!studio_synth_picker_open(s) && r.bytes() != before && s.settings.soundRecent.size() == 1);

    // An empty slot comes back empty.
    {
        const std::string emptyBefore = r.bytes();
        r.open_browser(2);
        r.tap(Button::DPAD_DOWN);
        r.a();
        assert(!songcore::instrument_is_free(host.project().instruments[2]));
        r.b();
        assert(r.bytes() == emptyBefore && songcore::instrument_is_free(host.project().instruments[2]));
    }

    // A sample slot: the first A asks (no change yet), B answers no and stays, A A replaces; then
    // B cannot bring the sample back and says the new sound stays.
    {
        const std::string sampleBefore = r.bytes();
        r.open_browser(1);
        r.a();
        assert(s.studioSynthPicker.confirmReplace && r.bytes() == sampleBefore);
        assert(s.studioMessage.find("no undo") != std::string::npos);
        shot(dir / "images/3-replace-sample-asks.ppm", s);
        r.b();
        assert(studio_synth_picker_open(s) && !s.studioSynthPicker.confirmReplace && r.bytes() == sampleBefore);
        r.a();
        r.a();
        assert(r.bytes() != sampleBefore && !s.studioSynthPicker.original);
        const std::string replaced = r.bytes();
        r.b();
        assert(!studio_synth_picker_open(s) && r.bytes() == replaced);
    }

    // Changing the slot while the browser is open closes it and keeps what was used.
    {
        r.open_browser(3);
        r.a();
        const std::string used = r.bytes();
        s.currentInstrument = 4;
        r.tap(Button::DPAD_DOWN);
        assert(!studio_synth_picker_open(s) && r.bytes() == used);
    }

    // ── 4. Stars: R toggles, FAV lists them, a full bank refuses the 65th; empty tabs say so ──
    s.settings.soundRecent.clear();
    const std::string kept = r.bytes();
    r.open_browser(0);
    r.to_tab(SoundTab::RECENT);
    shot(dir / "images/4-recent-empty.ppm", s);
    r.to_tab(SoundTab::BASS);
    r.tap(Button::R_SHIFT);
    assert(s.settings.soundFavorites.size() == 1 && s.studioMessage.rfind("Starred", 0) == 0);
    r.tap(Button::DPAD_DOWN);
    r.tap(Button::R_SHIFT);
    r.to_tab(SoundTab::FAV);
    assert(r.row() == 0);
    r.l_down();
    assert(s.studioSynthPicker.previewSounding);
    r.l_up();
    shot(dir / "images/5-favorites.ppm", s);
    r.tap(Button::R_SHIFT);                                              // remove from FAV itself
    assert(s.settings.soundFavorites.size() == 1);
    r.to_tab(SoundTab::MODEL);
    r.tap(Button::R_SHIFT);                                              // MODEL has no stars
    assert(s.settings.soundFavorites.size() == 1 && s.studioMessage.find("factory") != std::string::npos);
    r.to_tab(SoundTab::DRUM);
    for (int i = 0; s.settings.soundFavorites.size() < size_t(kSoundFavoritesMax) && i < 200; ++i) {
        r.tap(Button::R_SHIFT);
        r.tap(Button::DPAD_DOWN);
    }
    assert(int(s.settings.soundFavorites.size()) == kSoundFavoritesMax);
    r.tap(Button::R_SHIFT);
    assert(int(s.settings.soundFavorites.size()) == kSoundFavoritesMax && s.studioMessage.find("full") != std::string::npos);
    r.b();
    assert(r.bytes() == kept);   // stars are settings, never the song

    // ── 6. A resample hears the preview lane: no preview then. Focus loss ends a held preview. ──
    r.open_browser(0);
    r.to_tab(SoundTab::KEYS);
    s.resampleView.active = true;
    r.l_down();
    assert(!s.studioSynthPicker.previewSounding && s.studioMessage.find("RESAMPLE") != std::string::npos);
    r.l_up();
    s.resampleView.active = false;
    r.l_down();
    assert(s.studioSynthPicker.previewSounding);
    studio_synth_stop_audition(s, host);                                 // what the shell does on focus loss
    assert(!s.studioSynthPicker.previewSounding && !s.studioSynthPicker.previewHeld);
    r.held.l = false;
    r.b();

    // ── Codex, pre-review of stage 1 ──
    // (a) A with the transport stopped plays the slot's new sound at its root. That voice belongs to
    // the picker: B, A-keep, help, focus loss and the next button each end it — no unowned endless note.
    {
        // Silent once the pad's release tail has passed (it falls ~5x per 40 blocks; ~4 s): an owned
        // voice ends, an unowned one would ring on as in the control above.
        auto silent_after = [&] { r.render(380); return !r.render(20); };
        auto used_then = [&](const char* what, auto end) {
            r.open_browser(5);
            r.to_tab(SoundTab::PAD);
            assert(!host.is_playing() && !s.studioSynthPicker.previewHeld);
            r.a();
            assert(s.studioSynthPicker.previewSounding);
            assert(r.render(180) && r.render(20));   // the root note rings on by itself (the control)
            end();
            if (s.studioSynthPicker.previewSounding || !silent_after()) {
                std::cerr << "the root note after a use still sounds after " << what << "\n";
                assert(false);
            }
            if (studio_synth_picker_open(s)) r.b();
        };
        used_then("B (undo)", [&] { r.b(); });
        used_then("A (keep)", [&] { r.a(); });
        used_then("SELECT (help)", [&] {
            r.tap(Button::SELECT);
            s.studioHelp = false;
        });
        used_then("focus loss", [&] { studio_synth_stop_audition(s, host); });
        used_then("the next D-pad press", [&] { r.tap(Button::DPAD_DOWN); });
        used_then("slot change", [&] {
            s.currentInstrument = 6;
            r.tap(Button::DPAD_DOWN);
        });
        s.currentInstrument = 5;
    }

    // (b) Valid JSON with wrong types is refused by preview and by A alike — no exception, slot and
    // project untouched — with the checks INSTRUMENT > LOAD makes (parse_instrument_preset_checked).
    {
        const fsys::path badDir = fsys::path(sound_pack_root(r.fs)) / "Synths/01_Bass/Bad_Type";
        fsys::create_directories(badDir);
        const char* bad[] = {
            R"({"version":1,"instrument":{"sampleFilePath":42}})",
            R"({"version":1,"instrument":{"sampleFilePath":"/@mutable/p8-159-154-44.wav","studioSynthParams":"loud"}})",
            R"({"version":1,"instrument":{"sampleFilePath":"/@mutable/p8-159-154-44.wav","studioSynthParams":[999]}})",
            R"({"version":1,"instrument":{"sampleFilePath":"/@mutable/p8-159-154-44.wav","fmVoice":7}})",
            R"({"version":1,"instrument":"not an object"})",
            R"([1,2,3])",
            R"({"version":1,"instrument":{"sampleFilePath":"tone.wav"}})",
            R"({"version":1,"instrument":{"sampleFilePath":"/@mutable/not-a-code.wav"}})",
            R"({"version":1,"instr)",
        };
        s.settings.soundFavorites.clear();
        for (size_t i = 0; i < std::size(bad); ++i) {
            const std::string name = "Bad_Type_" + std::to_string(i) + ".pti";
            std::ofstream(badDir / name) << bad[i];
            s.settings.soundFavorites.push_back("Synths/01_Bass/Bad_Type/" + name);
        }
        const std::string start = r.bytes();
        const int version = s.projectVersion;
        r.open_browser(0);
        r.to_tab(SoundTab::FAV);
        for (size_t i = 0; i < std::size(bad); ++i) {
            while (r.row() != int(i)) r.tap(Button::DPAD_DOWN);
            r.l_down();
            assert(!s.studioSynthPicker.previewSounding && s.studioMessage.rfind("Cannot read", 0) == 0);
            r.l_up();
            r.a();
            assert(s.studioMessage.rfind("Cannot use", 0) == 0 && s.studioSynthPicker.usedKey.empty());
            assert(r.bytes() == start && s.projectVersion == version);
            // INSTRUMENT > LOAD refuses the same file and keeps the slot.
            assert(!host.load_instrument_preset(0, (badDir / ("Bad_Type_" + std::to_string(i) + ".pti")).string()));
            assert(r.bytes() == start);
        }
        r.b();
        s.settings.soundFavorites.clear();
        // A wrong-typed scalar (volume, mod slots, root) is the format's tolerant case, the same as in
        // a project file: the field takes its default. No exception, and preview and LOAD agree.
        songcore::InstrumentPreset lenient;
        assert(songcore::parse_instrument_preset_checked(
            R"({"version":1,"instrument":{"sampleFilePath":"/@mutable/p8-159-154-44.wav","volume":"loud","modSlots":"x","root":[]}})",
            lenient));
        assert(lenient.instrument.volume == songcore::Instrument(0).volume);
    }

    // (c) The level scales the sound's own volume, which tops out at 255. Below that ceiling a higher
    // level is really louder; above it the browser says where the sound peaks instead of pretending.
    {
        r.open_browser(0);
        r.to_tab(SoundTab::BASS);
        while (r.row() != 0) r.tap(Button::DPAD_UP);
        std::string text;
        assert(r.fs.read_file(sound_pack_root(r.fs) + "/" + s.soundLibrary.entries[size_t(SoundTab::BASS)][0].key, text));
        const int volume = songcore::parse_instrument_preset(songcore::json::parse(text)).instrument.volume;
        assert(volume > 0 && volume < 255);
        const int ceiling = std::min(kSoundPreviewLevelMax, 255 * 100 / volume);
        auto energy_at = [&](int level) {
            s.settings.soundPreviewLevel = level;
            r.l_down();
            float audio[1024]{};
            double e = 0;
            for (int b = 0; b < 60; ++b) {
                r.engine->processLiveBlock(audio, 512, 2, 48000);
                for (const float x : audio) e += double(x) * x;
            }
            r.l_up();
            r.render(20);
            return e;
        };
        const double at100 = energy_at(100);
        const double atCeiling = energy_at(ceiling / 10 * 10);
        assert(s.studioSynthPicker.previewCeiling == ceiling);
        std::cout << "preview energy at 100 %: " << at100 << ", at " << ceiling / 10 * 10 << " %: " << atCeiling << "\n";
        assert(atCeiling > at100 * 1.3);   // audibly louder up to the ceiling
        s.settings.soundPreviewLevel = 190;
        r.a_with(Button::DPAD_RIGHT);       // 200 %, past this sound's ceiling
        assert(s.settings.soundPreviewLevel == 200 && s.studioMessage.find("loudest from " + std::to_string(ceiling)) != std::string::npos);
        s.studioMessage.clear();
        shot(dir / "images/11-level-ceiling.ppm", s);
        while (s.settings.soundPreviewLevel != 100) r.a_with(Button::DPAD_LEFT);
        r.b();
    }

    // ── Codex, STUFE1-BEFUND-ORDNERWECHSEL: the catalog follows the instrument folder ──
    // Home A is the shipped pack; home B holds one other pad; home C has no pack at first. A → B →
    // C (missing, then the pack appears) → A: each tab lists the pack A would load from, previewing and
    // using reach that pack, stars and recent (relative keys) stay, and nothing moves in the project.
    {
        const std::string homeA = r.fs.instruments_directory();
        const fsys::path homeB = dir / "homeB/Instruments", homeC = dir / "homeC/Instruments";
        const std::string bKey = "Synths/03_Pads/B_Only/B_Only_Hold.pti";
        std::string padText;
        const auto& padsA = s.soundLibrary.entries[static_cast<size_t>(SoundTab::PAD)];
        const size_t padCountA = padsA.size();
        assert(padCountA > 1);
        assert(r.fs.read_file(homeA + "/" + kSoundPackFolder + "/" + padsA[0].key, padText));
        {
            // B's pad: a real factory pad with another volume, so the slot shows which one was used.
            auto j = songcore::json::parse(padText);
            j["instrument"]["volume"] = 201;
            fsys::create_directories((homeB / kSoundPackFolder / bKey).parent_path());
            std::ofstream(homeB / kSoundPackFolder / bKey) << j.dump();
        }
        fsys::create_directories(homeC);
        const auto favorites = s.settings.soundFavorites;
        const auto recent = s.settings.soundRecent;
        auto pads = [&] { return s.soundLibrary.entries[static_cast<size_t>(SoundTab::PAD)]; };

        r.open_browser(7);
        r.to_tab(SoundTab::PAD);
        assert(pads().size() == padCountA);
        r.b();

        r.fs.folder = homeB.string();                     // SET_HOME to B
        const std::string beforeB = r.bytes();
        r.open_browser(7);
        r.to_tab(SoundTab::PAD);
        assert(s.soundLibrary.root == (homeB / kSoundPackFolder).string());
        assert(pads().size() == 1 && pads()[0].key == bKey && pads()[0].family == "B Only");
        assert(s.soundLibrary.entries[static_cast<size_t>(SoundTab::ALL)].empty() ||
               s.soundLibrary.entries[static_cast<size_t>(SoundTab::ALL)].size() == 1);
        r.l_down();
        assert(s.studioSynthPicker.previewSounding);       // B's file, found where LOAD looks
        r.l_up();
        r.a();
        assert(s.studioSynthPicker.usedKey == bKey && host.project().instruments[7].volume == 201);
        shot(dir / "images/12-home-b.ppm", s);
        r.b();                                             // undo: the project as before
        assert(r.bytes() == beforeB);

        r.fs.folder = homeC.string();                      // a home without the pack
        r.open_browser(7);
        r.to_tab(SoundTab::PAD);
        assert(!s.soundLibrary.found && pads().empty());
        r.a();
        assert(r.bytes() == beforeB);
        // The pack becomes reachable while the browser is open: the next press finds it.
        fsys::create_directories((homeC / kSoundPackFolder / bKey).parent_path());
        fsys::copy_file(homeB / kSoundPackFolder / bKey, homeC / kSoundPackFolder / bKey);
        r.tap(Button::DPAD_DOWN);
        assert(s.soundLibrary.found && pads().size() == 1 && pads()[0].key == bKey);
        r.b();

        r.fs.folder.clear();                               // back to A
        r.open_browser(7);
        r.to_tab(SoundTab::PAD);
        assert(s.soundLibrary.root == homeA + "/" + kSoundPackFolder && pads().size() == padCountA);
        assert(s.settings.soundFavorites == favorites && s.settings.soundRecent == recent);
        r.b();
        assert(r.bytes() == beforeB);
        // The draw follows too: opened on PAD after a move, the first frame already shows the new pack.
        r.fs.folder = homeB.string();
        r.open_browser(7);
        assert(s.studioSynthPicker.tab == SoundTab::PAD && s.soundLibrary.root == (homeB / kSoundPackFolder).string() &&
               pads().size() == 1 && pads()[0].key == bKey);
        r.b();
        r.fs.folder.clear();
    }

    // ── 5. Native images: the category list in dark, light and monochrome ──
    s.settings.soundRecent = {key};
    r.open_browser(0);
    r.to_tab(SoundTab::KEYS);
    for (int i = 0; i < 5; ++i) r.tap(Button::DPAD_DOWN);
    s.studioMessage.clear();
    shot(dir / "images/6-keys-dark.ppm", s);
    s.theme = theme_light("DAYLIGHT", 0xFFF4F2EC, 0xFFE1DDD2, 0xFF1D4ED8, 0xFF1A1C20, 0xFF8A3A00);
    shot(dir / "images/7-keys-light.ppm", s);
    s.theme = theme_mono();
    shot(dir / "images/8-keys-mono.ppm", s);
    s.theme = theme_default();
    s.studioHelp = true;
    shot(dir / "images/9-help.ppm", s);
    s.studioHelp = false;
    r.b();

    // ── 3. Stars, recent and level survive save → load; damaged entries are dropped ──
    {
        Theme theme = theme_default();
        StdFileSystem plain((dir / "plain").string());
        assert(save_settings(plain, SettingsValues{}, theme));
        std::string json;
        assert(plain.read_file(plain.settings_path(), json));
        assert(json.find("soundFavorites") == std::string::npos && json.find("soundRecent") == std::string::npos &&
               json.find("soundPreviewLevel") == std::string::npos);   // an unused browser writes nothing
        SettingsValues back;
        Theme backTheme;
        assert(load_settings(plain, back, backTheme) && back.soundFavorites.empty() && back.soundPreviewLevel == 100);

        SettingsValues v;
        std::vector<SoundEntry> scratch;   // read through the API: a folder move above reset the cache
        const auto& drums = sound_tab_entries(s.soundLibrary, r.fs, SoundTab::DRUM, {}, {}, scratch);
        assert(drums.size() == 768);
        v.soundFavorites = {drums[0].key, drums[5].key, key};
        v.soundRecent = {key, drums[5].key};
        v.soundPreviewLevel = 70;
        StdFileSystem saved((dir / "saved").string());
        assert(save_settings(saved, v, theme));
        assert(load_settings(saved, back, backTheme));
        assert(back.soundFavorites == v.soundFavorites && back.soundRecent == v.soundRecent && back.soundPreviewLevel == 70);

        // A hand-damaged file: wrong types, unsafe paths, duplicates, an absurd level.
        std::string text;
        assert(saved.read_file(saved.settings_path(), text));
        auto j = songcore::json::parse(text);
        j["soundFavorites"] = songcore::json::array({key, key, "../../x.pti", 42, nullptr, "Synths/a/b/c.wav"});
        j["soundRecent"] = "not a list";
        j["soundPreviewLevel"] = 100000;
        std::ofstream(saved.settings_path()) << j.dump();
        assert(load_settings(saved, back, backTheme));
        assert(back.soundFavorites == std::vector<std::string>{key} && back.soundRecent.empty() &&
               back.soundPreviewLevel == kSoundPreviewLevelMax);
    }

    // ── A missing pack: the category tabs say where to go; a starred key whose file is gone says so ──
    {
        Rig bare(dir / "bare", false);
        bare.s.studioOpen = true;
        bare.s.settings.soundFavorites = {key};
        const std::string start = bare.bytes();
        bare.open_browser(0);
        bare.to_tab(SoundTab::BASS);
        assert(!bare.s.soundLibrary.found && bare.s.soundLibrary.entries[static_cast<size_t>(SoundTab::BASS)].empty());
        bare.a();
        assert(bare.bytes() == start);
        shot(dir / "images/10-pack-missing.ppm", bare.s);
        bare.to_tab(SoundTab::FAV);
        bare.l_down();
        assert(!bare.s.studioSynthPicker.previewSounding && bare.s.studioMessage.rfind("Cannot read", 0) == 0);
        bare.l_up();
        bare.a();
        assert(bare.bytes() == start && bare.s.studioMessage.rfind("Cannot use", 0) == 0);
    }

    std::cout << "ptsound-browser-check: the PRESET picker keeps its MODEL tab and adds FAV, RECENT, ALL and nine "
                 "categories over the 2048 shipped factory .pti (each in one category); L previews on the preview "
                 "lane only at a bounded level while the song plays, B leaves the project byte-identical; A uses the "
                 "sound in the marked slot, B undoes it (synth and empty slot), A again keeps it, a sample slot asks "
                 "first; slot change keeps; stars (max 64), recent (after a use) and the level round-trip, damaged "
                 "keys dropped, older files read; empty tabs, first/last row, paging; no preview during a resample, "
                 "focus loss ends it; missing pack and missing file refused; the root note after a use ends on B, keep, "
                 "help, focus loss, next button and slot change; wrong-typed .pti refused by preview and LOAD alike; "
                 "the level is louder up to the sound's ceiling and shows it beyond; the catalog follows a moved "
                 "instrument folder A -> B -> missing -> found -> A; 12 native images\n";
    return 0;
}
