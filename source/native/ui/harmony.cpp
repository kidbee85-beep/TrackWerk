#include "ui/harmony.h"
#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "songcore/chords.h"
#include "songcore/effects.h"
#include "songcore/timing.h"
#include "songcore/host.h"
#include "studio/synth.h"
#include "audio-engine.h"
#include <algorithm>
#include <cmath>

namespace pt::ui {
namespace {
constexpr const char* kStyles[] = {"Jazz", "Soul", "Hip-Hop", "House", "Electro", "Ambient"};
constexpr const char* kNotes[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
struct Recipe {
    const char* name;
    std::array<int,4> roots;
    std::array<int,4> shapes;
};
constexpr Recipe kRecipes[][8] = {
    {{"Late Set",{2,7,0,9},{7,8,6,7}},{"Minor Turn",{2,7,0,8},{4,8,7,6}},
     {"Blue Room",{0,5,10,3},{7,8,6,6}},{"Cedar Steps",{0,9,2,7},{6,8,7,8}},
     {"Side Door",{5,10,0,0},{7,8,6,13}},{"After Hours",{0,3,8,7},{14,6,6,8}},
     {"Half Light",{0,6,5,7},{13,8,6,8}},{"Last Call",{9,2,7,0},{7,7,8,6}}},
    {{"Velvet Keys",{0,9,5,7},{6,7,6,8}},{"Porch Light",{0,4,5,2},{13,7,6,7}},
     {"Pocket Rhodes",{0,5,3,10},{14,7,6,8}},{"Sweet Return",{5,4,9,2},{6,7,7,7}},
     {"Small Hours",{0,10,8,7},{7,8,6,8}},{"Sunday Keys",{0,2,4,5},{6,7,7,6}},
     {"Warm Reel",{0,3,5,8},{7,6,7,6}},{"Open Window",{0,7,9,5},{11,3,7,6}}},
    {{"Dust Loop",{0,8,3,10},{7,6,6,8}},{"Spare Change",{0,0,5,7},{14,7,7,8}},
     {"Night Bus",{0,10,5,8},{7,6,7,6}},{"Paper Chops",{0,5,2,7},{7,6,7,8}},
     {"Back Alley",{0,3,6,5},{7,6,8,7}},{"Soft Focus",{0,5,9,4},{6,6,7,7}},
     {"Half Time",{0,8,5,7},{14,6,7,8}},{"Tape Prayer",{0,3,10,5},{12,6,6,7}}},
    {{"Basement Lights",{0,5,3,10},{7,7,6,3}},{"Lift",{0,7,9,5},{11,0,7,6}},
     {"Midnight Piano",{0,8,10,5},{7,6,3,7}},{"Late Platform",{0,2,5,7},{6,7,6,8}},
     {"Warehouse Keys",{0,3,8,10},{7,6,6,0}},{"Haze",{0,5,10,3},{14,7,6,13}},
     {"Morning Dub",{0,0,5,10},{7,14,7,3}},{"Long Fade",{0,9,5,2},{6,7,6,7}}},
    {{"Neon Grid",{0,3,5,10},{1,0,3,0}},{"Circuit Bend",{0,6,3,7},{12,4,2,8}},
     {"Cold Relay",{0,2,5,3},{1,2,3,0}},{"Square City",{0,10,8,7},{1,0,0,8}},
     {"Bit Glass",{0,4,7,11},{2,1,2,4}},{"Hard Reset",{0,5,0,6},{1,3,2,4}},
     {"Night Drive",{0,8,3,5},{1,11,0,3}},{"Off Axis",{0,1,5,7},{1,0,1,3}}},
    {{"Open Sky",{0,5,9,2},{11,13,12,7}},{"Soft Orbit",{0,3,8,10},{14,13,11,3}},
     {"Slow Tide",{0,2,7,5},{2,7,2,6}},{"Signal Fog",{0,8,5,0},{15,6,14,12}},
     {"Still Water",{0,5,4,2},{13,11,7,7}},{"Night Air",{0,10,8,3},{12,11,13,6}},
     {"Pale Moon",{0,7,5,9},{13,2,6,12}},{"Cloud Rings",{0,3,5,8},{15,13,7,11}}}
};
constexpr const char* kRows[] = {
    "TONIC","STYLE","PRESET","INSTRUMENT","LENGTH","CHORD SLOT",
    "ROOT","SHAPE","VOICING","STRUM","GENERATE","LOOP","WRITE PHRASE"
};
constexpr int kSlot = 5;
constexpr int kGenerate = 10;
constexpr int kLoop = 11;
constexpr int kWrite = 12;

int wrap(int n, int count) { return (n % count + count) % count; }

int closest_inversion(const HarmonySlot& previous, int note, int shape) {
    int from[4];
    songcore::chord_voicing(previous.chord, from);
    int result = 0, best = 1000;
    for (int inversion = 0; inversion < 4; ++inversion) {
        int to[4];
        songcore::chord_voicing(shape | inversion, to);
        int distance = 0;
        for (int i = 0; i < 4; ++i) distance += std::abs(previous.note + from[i] - note - to[i]);
        if (distance < best) { best = distance; result = inversion; }
    }
    return result;
}

bool available(int id, const songcore::SongcoreHost& host) {
    const auto& instruments = host.project().instruments;
    return id >= 0 && id < int(instruments.size()) && harmony_supported(instruments[id]) &&
           (instruments[id].instrumentType == songcore::InstrumentType::EXTERNAL ||
            host.instrument_media_ready(id));
}

int next_instrument(int id, int direction, const songcore::SongcoreHost& host) {
    const int count = host.project().instruments.size();
    for (int step = 1; step <= count; ++step) {
        const int candidate = wrap(id + direction * step, count);
        if (available(candidate, host)) return candidate;
    }
    return id;
}

void initialize(HarmonyState& h, int instrument, const songcore::SongcoreHost& host) {
    h.instrument = available(instrument,host) ? instrument : next_instrument(instrument,1,host);
    harmony_load(h, false);
    h.initialized = true;
}

void audition(HarmonyState& h, int slot, songcore::SongcoreHost& host, int64_t length) {
    const auto& entry = h.slots[slot];
    host.preview_note(h.instrument, songcore::note_from_midi(entry.note), length,
                      false, -1, entry.chord, h.strum);
    h.sounding = slot;
}

std::string root_name(int note) {
    return std::string(kNotes[note % 12]) + std::to_string(note / 12 - 1);
}
}

bool harmony_supported(const songcore::Instrument& instrument) {
    return !instrument.granularEnabled &&
           (!instrument.sampleFilePath || studio::uri_code(instrument.sampleFilePath->c_str()) < 0);
}

void harmony_load(HarmonyState& h, bool keepLocks) {
    const auto& recipe = kRecipes[h.style][h.preset];
    for (int i = 0; i < h.count; ++i) {
        auto& slot = h.slots[i];
        if (keepLocks && slot.locked) continue;
        const int recipeSlot = i % 4;
        const int note = 48 + wrap(h.key + recipe.roots[recipeSlot], 12);
        const int shape = recipe.shapes[recipeSlot] << 4;
        slot = {note, shape | (i ? closest_inversion(h.slots[i-1], note, shape) : 0), false};
    }
}

bool harmony_fill_phrase(const HarmonyState& h, songcore::Phrase& phrase) {
    if ((h.count != 4 && h.count != 8) || h.instrument < 0 ||
        h.instrument >= songcore::POOL_INSTRUMENTS || phrase.steps.size() != songcore::PHRASE_ROWS ||
        std::any_of(phrase.steps.begin(), phrase.steps.end(),
                    [](const auto& step) {
                        return step.note != songcore::Note::EMPTY() || step.instrument != 0 ||
                               step.volume != 0x7f || step.fx1Type || step.fx1Value ||
                               step.fx2Type || step.fx2Value || step.fx3Type || step.fx3Value;
                    })) return false;
    for (int i = 0; i < h.count; ++i) {
        auto& step = phrase.steps[i * songcore::PHRASE_ROWS / h.count];
        step.note = songcore::note_from_midi(h.slots[i].note);
        step.instrument = h.instrument;
        step.volume = 0x7f;
        step.fx1Type = songcore::FX_CHD;
        step.fx1Value = h.slots[i].chord;
        if (h.strum) { step.fx2Type = songcore::FX_ARC; step.fx2Value = h.strum; }
    }
    return true;
}

void harmony_stop(HarmonyState& h, songcore::SongcoreHost& host) {
    if (h.loop || h.held || h.sounding >= 0) host.stop_preview(true);
    h.loop = h.held = false;
    h.sounding = -1;
}

bool harmony_button(const ButtonEvent& event, AppState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    auto& h = s.harmony;
    if (!h.initialized) initialize(h,s.currentInstrument,host);
    if (event.action == ButtonAction::RELEASED) {
        if (event.button == Button::A && h.held) harmony_stop(h, host);
        // L locks the slot on its release, and only alone: studio.cpp swallows a shoulder release
        // that was part of a combo (L + SELECT, L + R + START).
        if (event.button == Button::L_SHIFT && h.cursor == kSlot && !h.held) {
            h.confirm = false;
            h.slots[h.slot].locked = !h.slots[h.slot].locked;
        }
        return true;
    }
    const auto button = event.button;
    if (button == Button::A) {
        if (h.held) return true;
        if (h.cursor == kGenerate) {
            harmony_stop(h, host);
            h.seed ^= h.seed << 13; h.seed ^= h.seed >> 17; h.seed ^= h.seed << 5;
            h.preset = (h.preset + 1 + h.seed % 7) % 8;
            harmony_load(h, true);
            const int change = 1 + ((h.seed >> 8) % (h.count - 2));
            auto& varied = h.slots[change];
            if (!varied.locked) {
                const bool ambient = h.style == 5;
                const int shape = (ambient ? 11 : 8) << 4;
                if (!ambient) varied.note = 48 + wrap(h.slots[change+1].note + 7,12);
                varied.chord = shape | closest_inversion(h.slots[change-1], varied.note, shape);
            }
            h.status = "Variation ready. Locked slots kept.";
        } else if (h.cursor == kLoop && h.loop) {
            harmony_stop(h, host);
        } else if (h.cursor == kSlot || h.cursor == kLoop || h.cursor == kWrite) {
            if (h.cursor == kWrite && !h.confirm) {
                h.confirm = true;
                h.status = "Press A again to fill the current empty phrase.";
                return true;
            }
            h.confirm = false;
            if (!available(h.instrument, host)) {
                h.status = "Choose a loaded sample, SoundFont or external MIDI instrument.";
                return true;
            }
            if (host.is_playing()) {
                h.status = "Stop the song before using Harmony.";
                return true;
            }
            if (h.cursor == kWrite) {
                harmony_stop(h, host);
                h.write = true;
            } else if (h.cursor == kLoop) {
                h.loop = true;
                h.loopSlot = 0;
                h.nextFrame = engine.getCurrentFrame();
            } else {
                harmony_stop(h, host);
                h.held = true;
                audition(h, h.slot, host, 0);
                constexpr int sequences[][4] = {{0,2,1,3}, {3,1,2,0}};
                for (int i = 0; i < 2; ++i) {
                    int& progress = h.secret[i];
                    progress = h.slot == sequences[i][progress] ? progress + 1 : int(h.slot == sequences[i][0]);
                    if (progress == 4) {
                        progress = 0;
                        h.sparkKind = i;
                        h.sparkUntil = 1;
                    }
                }
            }
        }
        return true;
    }
    h.confirm = false;
    if (h.held) return true;
    if (button == Button::DPAD_UP || button == Button::DPAD_DOWN) {
        h.cursor = wrap(h.cursor + (button == Button::DPAD_DOWN ? 1 : -1), std::size(kRows));
        return true;
    }
    if (button == Button::L_SHIFT) return true;   // acts on release (above)
    if (button != Button::DPAD_LEFT && button != Button::DPAD_RIGHT) return true;
    const int d = button == Button::DPAD_RIGHT ? 1 : -1;
    harmony_stop(h, host);
    auto& slot = h.slots[h.slot];
    switch (h.cursor) {
        case 0: {
            h.key = wrap(h.key+d,12);
            for (auto& entry : h.slots) entry.note = std::clamp(entry.note+d, 24, 83);
            break;
        }
        case 1: h.style = wrap(h.style+d, std::size(kStyles)); h.preset = 0; harmony_load(h,false); break;
        case 2: h.preset = wrap(h.preset+d,8); harmony_load(h,false); break;
        case 3: h.instrument = next_instrument(h.instrument,d,host); break;
        case 4: h.count = h.count == 4 ? 8 : 4; h.slot = std::min(h.slot,h.count-1); harmony_load(h,true); break;
        case kSlot: h.slot = wrap(h.slot+d,h.count); break;
        case 6: slot.note = std::clamp(slot.note+d,24,83); break;
        case 7: slot.chord = (wrap((slot.chord>>4)+d,16)<<4) | (slot.chord&15); break;
        case 8: slot.chord = (slot.chord&0xf0) | wrap((slot.chord&15)+d,16); break;
        case 9: h.strum = wrap(h.strum+d,5); break;
        default: break;
    }
    return true;
}

bool harmony_tick(AppState& s, songcore::SongcoreHost& host, AudioEngine& engine, uint64_t now) {
    auto& h = s.harmony;
    if (!s.studioOpen || s.studioTab != 8 || s.studioHelp || s.studioWorkspace.open || host.is_playing()) {
        harmony_stop(h, host);
        h.sparkUntil = 0;
        h.secret = {};
        return false;
    }
    if (!h.initialized) { initialize(h,s.currentInstrument,host); return true; }
    if (h.sparkUntil == 1) h.sparkUntil = now + 1700;
    if (h.loop && engine.getCurrentFrame() >= h.nextFrame) {
        const int steps = songcore::PHRASE_ROWS / h.count;
        const int64_t frames = songcore::frames_per_step(songcore::tempo_in_force(host.project()), engine.getSampleRate()) * steps;
        audition(h, h.loopSlot, host, std::max<int64_t>(1,frames * 9 / 10));
        h.nextFrame = engine.getCurrentFrame() + std::max<int64_t>(1,frames);
        h.loopSlot = (h.loopSlot + 1) % h.count;
    }
    if (h.sparkUntil && now >= h.sparkUntil) { h.sparkUntil = 0; return true; }
    return h.sparkUntil || h.loop || h.held;
}

namespace {
// A panel as on the other Studio pages: soft shadow, frame, a faint top highlight.
void harmony_panel(Canvas& c, int x, int y, int w, int h, const Theme& t, Argb edge) {
    c.panel_rect(x, y + 2, w, h, 0x27000000u, 0x27000000u, 6);
    c.panel_rect(x, y, w, h, t.rowEvery4th, edge, 6);
    c.fill_rect(x + 8, y + 1, w - 16, 1, with_alpha(t.textValue, .07f));
}

// The chord's degree in the key: "I", "ii", "bVII" … (lower case for minor and diminished chords).
std::string degree(int key, const HarmonySlot& slot) {
    static constexpr const char* names[] = {"I","bII","II","bIII","III","IV","#IV","V","bVI","VI","bVII","VII"};
    std::string d = names[wrap(slot.note - key, 12)];
    const std::string chord = songcore::chord_name(slot.chord);
    if (chord.find("MINOR") != std::string::npos || chord.find("DIMINISHED") != std::string::npos)
        for (auto& ch : d) if (ch != 'b' && ch != '#') ch = static_cast<char>(ch - 'A' + 'a');
    if (chord.find("DIMINISHED") != std::string::npos) d += "o";
    return d;
}

// One octave from C as a piano: seven white keys, five narrow black keys over them; the chord's
// pitch classes are lit.
void mini_keys(Canvas& c, int x, int y, int whiteW, const HarmonySlot& slot, const Theme& t, Argb lit) {
    int notes[4];
    songcore::chord_voicing(slot.chord, notes);
    bool on[12] = {};
    for (int n : notes) on[wrap(slot.note + n, 12)] = true;
    static constexpr int whites[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int blacks[5] = {1, 3, 6, 8, 10};
    static constexpr int blackAfter[5] = {0, 1, 3, 4, 5};   // the white key each black key follows
    const int h = 16, blackW = whiteW * 3 / 5, blackH = 10;
    for (int k = 0; k < 7; ++k)
        c.fill_rect(x + k * whiteW, y, whiteW - 1, h, on[whites[k]] ? lit : with_alpha(t.textValue, .78f));
    for (int k = 0; k < 5; ++k) {
        const int bx = x + (blackAfter[k] + 1) * whiteW - blackW / 2 - 1;
        c.fill_rect(bx - 1, y, blackW + 2, blackH + 1, t.rowEvery4th);
        c.fill_rect(bx, y, blackW, blackH, on[blacks[k]] ? lit : mix_rgb(t.background, t.textValue, 12));
    }
}

// A small padlock.
void padlock(Canvas& c, int x, int y, Argb ink) {
    c.fill_rect(x + 1, y, 5, 1, ink);
    c.fill_rect(x, y + 1, 1, 3, ink);
    c.fill_rect(x + 6, y + 1, 1, 3, ink);
    c.fill_rect(x - 1, y + 4, 9, 6, ink);
}
}  // namespace

void harmony_draw(Canvas& c, const AppState& s) {
    const auto& h = s.harmony;
    const auto& t = s.theme;
    const Argb accent = t.rowCursor, ink = t.textValue, muted = t.textParam;

    // ── Header ────────────────────────────────────────────────────────────────────────────────
    harmony_panel(c, 18, 104, 604, 48, t, with_alpha(muted, .2f));
    c.draw_label("HARMONY / CHORD MACHINE", 30, 112, accent, 12);
    const std::string heading = std::string(kStyles[h.style]) + "  /  " + kRecipes[h.style][h.preset].name +
                                "  /  Key " + kNotes[h.key];
    c.draw_label(Canvas::clip_label(heading, 470, 18), 30, 129, ink, 18);
    const bool sounding = h.sounding >= 0;
    const Argb pill = h.loop || sounding ? accent : muted;
    c.panel_rect(518, 109, 92, 23, recess(t.background, t.rowEvery4th, .72f), with_alpha(pill, .6f), 4);
    c.draw_label(h.loop ? "LOOP" : sounding ? "PREVIEW" : "READY", 530, 113, pill, 12);

    // ── Chord slots ───────────────────────────────────────────────────────────────────────────
    harmony_panel(c, 18, 160, 604, 96, t, with_alpha(muted, .18f));
    const bool compact = h.count > 4;
    const int perRow = 4, cardW = 143, cardH = compact ? 40 : 80;
    for (int i = 0; i < h.count; ++i) {
        const int x = 26 + i % perRow * 149, y = 168 + i / perRow * 44;
        const bool focus = i == h.slot;
        const auto& slot = h.slots[i];
        const Argb face = focus ? recess(t.background, accent, .16f) : recess(t.background, t.rowEvery4th, .8f);
        c.panel_rect(x, y, cardW, cardH, face, focus ? accent : with_alpha(muted, .3f), 4);
        if (focus && h.cursor == kSlot) c.mark_cursor(x, y, cardW, cardH);
        const std::string name = root_name(slot.note).substr(0, root_name(slot.note).size() - 1) + " " +
                                 songcore::chord_name(slot.chord);
        c.draw_label(degree(h.key, slot), x + 7, y + 4, focus ? accent : muted, 12);
        if (slot.locked) padlock(c, x + cardW - 15, y + 5, focus ? accent : muted);
        c.draw_label(Canvas::clip_label(name, cardW - 14, compact ? 12 : 14), x + 7, y + (compact ? 20 : 22),
                     focus ? ink : mix_rgb(ink, muted, 30), compact ? 12 : 14);
        if (!compact) mini_keys(c, x + 7, y + 46, 14, slot, t, focus ? accent : mix_rgb(accent, muted, 35));
        if (i == h.sounding) {
            const int width = 18 + std::abs((s.blinkPhaseMs / 8) % 120 - 60);
            c.fill_rect(x + 4, y + cardH - 5, std::min(width, cardW - 8), 3, accent);
        }
    }

    // ── Settings ──────────────────────────────────────────────────────────────────────────────
    const auto& slot = h.slots[h.slot];
    std::string instrumentName = "Load a sample or SoundFont";
    if (s.project && h.instrument >= 0 && h.instrument < int(s.project->instruments.size()))
        instrumentName = hex2(h.instrument) + "  " + s.project->instruments[h.instrument].name;
    const std::array<std::string,13> values{{
        kNotes[h.key],kStyles[h.style],kRecipes[h.style][h.preset].name,instrumentName,
        std::to_string(h.count) + " slots",std::to_string(h.slot+1),
        root_name(slot.note),songcore::chord_name(slot.chord),songcore::chord_layout_name(slot.chord),
        h.strum ? std::to_string(h.strum)+" ticks" : "Off",
        "A: New variation",h.loop?"A: Stop":"A: Preview",h.confirm?"A: Confirm":"A: Fill empty phrase"
    }};
    harmony_panel(c, 18, 264, 400, 146, t, with_alpha(muted, .18f));
    c.draw_label("SETTINGS", 30, 272, accent, 12);
    constexpr int visible = 6;
    const int rows = static_cast<int>(std::size(kRows));
    const int first = std::clamp(h.cursor - 2, 0, rows - visible);
    c.mark_scroll(40, 25, 290, 381, visible * 19, first * 19);   // the rows slide (shell/app.cpp)
    for (int i = first; i < first + visible; ++i) {
        const int y = 292 + (i - first) * 19;
        const bool current = i == h.cursor;
        if (current) c.panel_rect(25, y - 2, 380, 18, recess(t.background, accent, .16f), accent, 3);
        if (current && h.cursor != kSlot) c.mark_cursor(25, y - 2, 380, 18);
        const bool action = i >= kGenerate;
        c.draw_label(kRows[i], 32, y, current ? ink : muted, 12);
        c.draw_label(Canvas::clip_label(values[i], 210, 12), 400 - Canvas::label_width(Canvas::clip_label(values[i], 210, 12), 12),
                     y, action ? accent : current ? accent : ink, 12);
    }
    c.fill_rect(410, 292, 1, visible * 19, with_alpha(muted, .12f));
    c.fill_rect(410, 292 + c.tween(0x6000001u, first * (visible * 19 - 30) / std::max(1, rows - visible)), 1, 30,
                with_alpha(accent, .7f));   // the thumb glides with the rows

    // ── Voicing (and the two hidden sparks) ───────────────────────────────────────────────────
    harmony_panel(c, 428, 264, 194, 146, t, with_alpha(muted, .18f));
    if (!h.sparkUntil) {
        const auto& shown = h.sounding >= 0 ? h.slots[h.sounding] : slot;
        int notes[4];
        songcore::chord_voicing(shown.chord, notes);
        c.draw_label("VOICING", 440, 272, accent, 12);
        c.draw_label(Canvas::clip_label(songcore::chord_layout_name(shown.chord), 100, 12),
                     610 - Canvas::label_width(Canvas::clip_label(songcore::chord_layout_name(shown.chord), 100, 12), 12),
                     272, muted, 12);
        const int low = notes[0], span = std::max(1, notes[3] - low);
        for (int line = 0; line < 4; ++line) c.fill_rect(446, 300 + line * 16, 158, 1, with_alpha(muted, .12f));
        for (int i = 0; i < 4; ++i) {
            const int x = 458 + i * 44, y = 352 - (notes[i] - low) * 52 / span;
            const int pulse = h.sounding >= 0 ? (s.blinkPhaseMs / 46 + i) % 4 : 0;
            if (i) c.line(x - 44, 352 - (notes[i - 1] - low) * 52 / span, x, y, muted);
            c.fill_rect(x - 3 - pulse / 2, y - 3, 7 + pulse, 7, accent);
            c.draw_label(kNotes[wrap(shown.note + notes[i], 12)], x - 6, 372, muted, 11);
        }
    } else if (h.sparkKind == 0) {
        for (int i = 0; i < 16; ++i) {
            const int phase = (s.blinkPhaseMs / 7 + i * 19) % 92;
            c.fill_rect(438 + (i * 37) % 170, 272 + phase, 2 + i % 3, 2, accent);
        }
        c.draw_label("Kid Bunka / After Hours", 440, 386, accent, 11);
    } else {
        const int flight = (s.blinkPhaseMs / 12) % 160;
        for (int i = 0; i < 14; ++i) {
            const int px = 438 + (i * 43) % 172;
            const int py = 278 + (i * 31 + flight) % 90;
            c.fill_rect(px, py, 2, 2, muted);
        }
        const int x = 440 + flight, y = 310 + std::abs(flight - 80) / 3;
        c.line(x - 7, y + 4, x + 8, y - 3, muted);
        c.line(x - 5, y + 6, x + 8, y - 3, muted);
        c.fill_rect(x + 5, y - 5, 6, 5, accent);
        c.draw_label("MODEM COMET", 440, 386, accent, 12);
    }

    // ── Status and keys ───────────────────────────────────────────────────────────────────────
    c.draw_label(Canvas::clip_label(h.status.empty() ? "48 progression presets. Audition before writing." : h.status, 604, 12),
                 18, 443, muted, 12);
    c.draw_label("D-pad: Edit   Slot: hold A to hear, L to lock   Select: Help", 18, 461, t.textTitle, 12);
}
}
