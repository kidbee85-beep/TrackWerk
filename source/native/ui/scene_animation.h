#pragma once
#include "ui/scene_presets.h"
// Scene animations: small demoscene effects that run only while the sequencer plays, each in the
// family whose idiom it is. Draw-only and deterministic for a given time — a headless screenshot at
// the same `ms` is the same picture. They add no redraw of their own: playback already redraws every
// frame (shell: `audible`), and a stopped tracker shows the static Scene artwork as before.
//   RASTER       raster bars sweeping behind the scope trace, flashing on the beat
//   CRACK INTRO  a sine scroller along the foot of the scope strip
//   VECTOR       a rotating wireframe cube beside the TrackWerk mark in the track rail
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/theme.h"

namespace pt::ui::scene_animation {

enum Family { RASTER = 5, CRACKTRO = 6, VECTOR = 7 };

inline int family_of(const Theme& t) {
    return t.sceneChrome > 0 ? static_cast<int>(scene_look(t.sceneChrome - 1).family) : -1;
}

// 1 on the beat, falling to 0 just before the next one.
inline float beat_pulse(uint64_t ms, int bpm) {
    const double beatMs = 60000.0 / std::clamp(bpm, 20, 999);
    const double phase = std::fmod(double(ms), beatMs) / beatMs;
    return float((1.0 - phase) * (1.0 - phase));
}

// Three copper bars, each a 9-line gradient, travelling up and down the strip on offset sines.
inline void raster_bars(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, int bpm) {
    const float pulse = beat_pulse(ms, bpm);
    const Argb inks[3] = {t.vizWave, t.textTitle, t.rowCursor};
    constexpr int kBar = 9;
    for (int bar = 0; bar < 3; ++bar) {
        const float centre = .5f + .38f * std::sin(float(ms) * .0021f + bar * 2.1f);
        const int top = y + int(centre * (h - kBar));
        for (int line = 0; line < kBar; ++line) {
            const float d = std::fabs(line - (kBar - 1) * .5f) / ((kBar - 1) * .5f);
            const float glow = 1.f - d * d;
            const Argb ink = mix_rgb(inks[bar], 0xFFFFFFFFu, int(30 * glow * pulse));
            c.fill_rect(x, top + line, w, 1, with_alpha(ink, (.20f + .22f * pulse) * glow));
        }
    }
}

inline const std::string& scroll_text() {
    static const std::string text =
        "TRACKWERK  *  KID BUNKA  *  SYNTHESIS / SAMPLES / SEQUENCES  *  "
        "GREETINGS TO EVERY TRACKER, SCENER AND POCKET MUSICIAN  *  ";
    return text;
}

// The classic cracktro scroller: one glyph at a time on a travelling sine, clipped to its box.
inline void sine_scroller(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms) {
    const std::string& text = scroll_text();
    static const int loop = Canvas::label_width(text, 12);
    const int offset = int((ms * 60 / 1000) % uint64_t(loop));   // 60 px per second
    Canvas::ClipScope clip(c, x, y, w, h);
    int penX = x + w - offset;
    while (penX > x - loop) penX -= loop;
    for (int pass = 0; pass < 3 && penX < x + w; ++pass) {
        for (char ch : text) {
            const std::string glyph(1, ch);
            const int advance = Canvas::label_width(glyph, 12);
            if (penX + advance >= x && penX < x + w && ch != ' ') {
                const float wave = std::sin(penX * .045f + float(ms) * .0045f);
                const int gy = y + (h - 14) / 2 + int(wave * (h / 2 - 8));
                c.draw_label(glyph, penX + 1, gy + 1, with_alpha(t.background, .70f), 12);   // drop shadow
                c.draw_label(glyph, penX, gy, mix_rgb(t.textTitle, t.vizWave, int(50 + 50 * wave)), 12);
            }
            penX += advance;
        }
    }
}

// A unit cube turning on two axes, projected with a little perspective; it swells on the beat.
inline void wire_cube(Canvas& c, int cx, int cy, float size, Argb ink, uint64_t ms, int bpm) {
    const float a = float(ms) * .0011f, b = float(ms) * .0007f;
    const float scale = size * (1.f + .12f * beat_pulse(ms, bpm));
    int px[8], py[8];
    for (int i = 0; i < 8; ++i) {
        float vx = (i & 1) ? 1.f : -1.f, vy = (i & 2) ? 1.f : -1.f, vz = (i & 4) ? 1.f : -1.f;
        const float x1 = vx * std::cos(a) - vz * std::sin(a), z1 = vx * std::sin(a) + vz * std::cos(a);
        const float y1 = vy * std::cos(b) - z1 * std::sin(b), z2 = vy * std::sin(b) + z1 * std::cos(b);
        const float persp = 3.f / (3.f + z2);
        px[i] = cx + int(x1 * scale * persp);
        py[i] = cy + int(y1 * scale * persp);
    }
    for (int i = 0; i < 8; ++i)
        for (int bit : {1, 2, 4})
            if (!(i & bit)) c.line(px[i], py[i], px[i | bit], py[i | bit], ink);
}

// ── The other five families, in the track rail's 60×20 mark strip ──────────────────────────────

// UNSCII: the dot matrix becomes a marquee — a wave of lit dots travelling right.
// ANSI:   the same matrix with BBS palette cycling, each column a step further round the colours.
inline void dot_matrix(Canvas& c, int x, int y, const Theme& t, uint64_t ms, int bpm, bool cycle) {
    const float pulse = beat_pulse(ms, bpm);
    const Argb palette[3] = {t.rowCursor, t.vizWave, t.textTitle};
    for (int i = 0; i < 11; ++i)
        for (int row = 0; row < 3; ++row) {
            const float wave = std::sin(i * .7f - float(ms) * .006f + row * .9f);
            const bool lit = wave > .2f - .5f * pulse;
            const Argb ink = cycle ? palette[(i + row + int(ms / 120)) % 3] : t.rowCursor;
            c.fill_rect(x + i * 5, y + 2 + row * 5, 3, 3, with_alpha(ink, lit ? .9f : .18f));
        }
}

// ASCII: six text-mode bars that jump on the beat and settle, like a typed VU meter.
inline void ascii_meter(Canvas& c, int x, int y, const Theme& t, uint64_t ms, int bpm) {
    const float pulse = beat_pulse(ms, bpm);
    for (int i = 0; i < 6; ++i) {
        const float level = .25f + .75f * pulse * (.55f + .45f * std::sin(i * 1.7f + float(ms) * .003f));
        c.fill_rect(x, y + i * 3, std::max(2, int(38 * level)), 1, with_alpha(t.rowCursor, .55f + .4f * level));
    }
}

// DOS: an installer's progress bar of seven blocks — where the playhead is in its phrase.
inline void dos_progress(Canvas& c, int x, int y, const Theme& t, int step, int steps) {
    const int filled = steps > 0 && step >= 0 ? (step * 7) / steps + 1 : 0;
    c.stroke_rect(x - 2, y, 7 * 7 + 3, 13, with_alpha(t.textParam, .55f));
    for (int i = 0; i < 7; ++i)
        c.fill_rect(x + i * 7, y + 2, 5, 9, with_alpha(t.rowCursor, i < filled ? .9f : .15f));
}

// TERMINAL: a prompt whose cursor blinks on the beat.
inline void terminal_prompt(Canvas& c, int x, int y, const Theme& t, uint64_t ms, int bpm) {
    c.draw_text("> RUN", x, y + 3, with_alpha(t.rowCursor, .85f), 1, 2);
    if (beat_pulse(ms, bpm) > .5f) c.fill_rect(x + Canvas::text_width("> RUN", 1, 2) + 3, y + 3, 6, 14, t.rowCursor);
}

// One 60×20 strip in the family's idiom, for any place that shows the Scene while playing (the
// Studio heading): all eight families, Raster and Crack intro in miniature. `step` feeds DOS.
inline void family_strip(Canvas& c, int x, int y, const Theme& t, uint64_t ms, int bpm, int step) {
    switch (family_of(t)) {
        case 0: dot_matrix(c, x, y, t, ms, bpm, false); break;
        case 4: dot_matrix(c, x, y, t, ms, bpm, true); break;
        case 1: ascii_meter(c, x, y, t, ms, bpm); break;
        case 2: dos_progress(c, x + 2, y + 3, t, step, 16); break;
        case 3: terminal_prompt(c, x, y, t, ms, bpm); break;
        case RASTER: raster_bars(c, x, y, 60, 20, t, ms, bpm); break;
        case CRACKTRO: sine_scroller(c, x, y, 60, 20, t, ms); break;
        case VECTOR: wire_cube(c, x + 28, y + 10, 8.f, with_alpha(t.rowCursor, .85f), ms, bpm); break;
        default: break;
    }
}

}  // namespace pt::ui::scene_animation
