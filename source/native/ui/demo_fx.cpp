#include "ui/demo_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/app_state.h"
#include "ui/brand_graphics.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/scene_animation.h"
#include "songcore/model.h"

namespace pt::ui::demo_fx {
namespace {

constexpr int kShowWaveformSize = 620;   // ui/modules/oscilloscope.h WAVEFORM_SIZE

uint32_t hash(uint32_t n) {
    n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16;
    return n;
}

// A 256-entry sine in 1/1024 steps: the plasma's inner loop stays integer.
const int16_t* sine_table() {
    static int16_t table[256];
    static const bool ready = [] {
        for (int i = 0; i < 256; ++i) table[i] = static_cast<int16_t>(std::lround(std::sin(i * 6.2831853 / 256) * 1024));
        return true;
    }();
    (void)ready;
    return table;
}

}  // namespace

Argb ramp(const Theme& t, int position) {
    position = std::clamp(position, 0, 255);
    const Argb stops[] = {t.background, t.rowEvery4th, t.rowCursor, t.textValue};
    const int along = position * 300 / 255;   // 0…300: three segments of 100
    const int segment = std::min(2, along / 100);
    return mix_rgb(stops[segment], stops[segment + 1], along - segment * 100);
}

void starfield(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, float speed, int stars) {
    const int cx = x + w / 2, cy = y + h / 2;
    const float travel = float(ms % 1000000) * (.00025f + .0009f * std::clamp(speed, 0.f, 1.f));
    for (int i = 0; i < stars; ++i) {
        const uint32_t r = hash(static_cast<uint32_t>(i) * 2654435761U + 17U);
        const float sx = (int(r & 1023) - 512) / 512.f, sy = (int((r >> 10) & 1023) - 512) / 512.f;
        float z = 1.f - std::fmod(float((r >> 20) & 1023) / 1024.f + travel, 1.f);   // 1 far … 0 near
        z = std::max(z, .04f);
        const int px = cx + int(sx * w * .5f / (z * 2.2f)), py = cy + int(sy * h * .5f / (z * 2.2f));
        if (px < x || px >= x + w - 2 || py < y || py >= y + h - 2) continue;
        const int bright = int((1.f - z) * 255);
        const int size = z < .25f ? 2 : 1;
        c.fill_rect(px, py, size, size, ramp(t, 110 + bright * 145 / 255));
        // A short streak behind the fast ones: motion that reads faster than the frame rate.
        if (z < .35f) c.fill_rect(px - (px - cx) / 18, py - (py - cy) / 18, 1, 1, with_alpha(ramp(t, 200), .5f));
    }
}

void plasma(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, int cell, float energy) {
    const int16_t* s = sine_table();
    const int phase = int(ms / 9), phase2 = int(ms / 13), lift = int(std::clamp(energy, 0.f, 1.f) * 60);
    cell = std::max(4, cell);
    for (int py = 0; py < h; py += cell) {
        const int gy = py / cell;
        for (int px = 0; px < w; px += cell) {
            const int gx = px / cell;
            int v = s[(gx * 6 + phase) & 255] + s[(gy * 7 - phase2) & 255] +
                    s[((gx + gy) * 4 + phase / 2) & 255] + s[((gx * gx + gy * gy) / 6 + phase2) & 255];
            v = (v + 4096) * 255 / 8192;                       // 0…255
            const int shade = s[(v + phase) & 255] / 8 + 128;  // cycled through the palette, 0…255
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py),
                        ramp(t, std::clamp(shade * 3 / 4 + lift, 0, 255)));
        }
    }
}

void copper(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, const float* energy, int count) {
    const int bars = count > 0 ? std::min(count, 8) : 5;
    for (int b = 0; b < bars; ++b) {
        const float e = energy && count > 0 ? std::clamp(energy[b], 0.f, 1.f) : .5f;
        const float wave = std::sin(float(ms) * .0021f + b * .9f);
        const int half = 4 + int(e * 9);
        const int centre = y + h / 2 + int(wave * (h / 2 - half - 2));
        for (int line = -half; line <= half; ++line) {
            const int shade = 255 - std::abs(line) * 255 / (half + 1);
            const Argb ink = ramp(t, 60 + shade * (120 + int(e * 75)) / 255);
            c.fill_rect(x, centre + line, w, 1, ink);
        }
    }
}

void scroller(Canvas& c, int x, int y, int w, int amplitude, const std::string& text, const Theme& t,
              uint64_t ms, int size, int pixelsPerSecond) {
    const int advance = std::max(6, Canvas::label_width("M", size));
    const int total = Canvas::label_width(text, size) + w;
    const int offset = int((ms * uint64_t(pixelsPerSecond) / 1000) % uint64_t(std::max(1, total)));
    Canvas::ClipScope clip(c, x, y - amplitude, w, amplitude * 2 + size + 8);
    int pen = x + w - offset;
    for (size_t i = 0; i < text.size(); ++i) {
        const std::string glyph(1, text[i]);
        const int gw = Canvas::label_width(glyph, size);
        if (pen > x - advance && pen < x + w) {
            const float a = (pen - x) * .025f + float(ms) * .004f;
            const int dy = int(std::sin(a) * amplitude);
            // A darker copy under each letter: depth, as the scrollers of the time had.
            c.draw_label(glyph, pen + 2, y + dy + 2, with_alpha(t.background, .9f), size);
            c.draw_label(glyph, pen + 1, y + dy + 1, with_alpha(t.background, .7f), size);
            c.draw_label(glyph, pen, y + dy, ramp(t, 225 + int(std::sin(a * 1.7f) * 30)), size);
        }
        pen += gw;
    }
}

void draw_attract(Canvas& c, const AppState& s, uint64_t ms) {
    const Theme& t = s.theme;
    c.fill_rect(0, 0, DESIGN_W, DESIGN_H, t.background);
    const int part = int((ms / kAttractPartMs) % 3);
    const uint64_t local = ms % kAttractPartMs;
    const int bpm = s.project ? songcore::tempo_in_force(*s.project) : 120;
    const std::string name = s.project && !s.project->name.empty() ? s.project->name : "UNTITLED";
    const std::string greets =
        "KID BUNKA PRESENTS ... TRACKWERK ... A POCKET TRACKER FOR HANDHELDS ... NOW LOADED: " + name +
        " ... " + std::to_string(bpm) + " BPM ... GREETINGS TO EVERY TRACKER MUSICIAN, CODER AND PIXEL ARTIST "
        "WHO KEPT THE SCENE ALIVE ... PRESS ANY KEY TO RETURN ...      ";

    if (part == 0) {
        // Part 1: a starfield, the TW mark rising out of it, the name in copper.
        starfield(c, 0, 0, DESIGN_W, DESIGN_H, t, ms, .35f, 220);
        const int rise = int(std::min<uint64_t>(local, 1200) * 60 / 1200);
        brand::mark(c, 256, 150 - rise + 60, 128, t.rowCursor, with_alpha(t.textTitle, .5f));
        const std::string title = "TRACKWERK";
        int pen = (DESIGN_W - Canvas::label_width(title, 28)) / 2;
        for (size_t i = 0; i < title.size(); ++i) {
            const std::string g(1, title[i]);
            const int wobble = int(std::sin(float(ms) * .004f + i * .6f) * 4);
            c.draw_label(g, pen + 2, 314 + wobble, with_alpha(t.background, .85f), 28);
            c.draw_label(g, pen, 312 + wobble, ramp(t, 215 + int(std::sin(float(ms) * .003f + i) * 40)), 28);
            pen += Canvas::label_width(g, 28);
        }
        scroller(c, 0, 400, DESIGN_W, 18, greets, t, ms, 18, 80);
    } else if (part == 1) {
        // Part 2: full-screen plasma, a copper band, the big sine scroller.
        plasma(c, 0, 0, DESIGN_W, DESIGN_H, t, ms, 8, .2f + .2f * scene_animation::beat_pulse(ms, bpm));
        copper(c, 0, 170, DESIGN_W, 140, t, ms);
        scroller(c, 0, 226, DESIGN_W, 34, greets, t, ms + 7000, 28, 110);
    } else {
        // Part 3: copper bars on the beat behind a spinning wireframe, the song's name on top.
        float energy[6];
        for (int b = 0; b < 6; ++b) energy[b] = .3f + .7f * scene_animation::beat_pulse(ms + b * 90, bpm);
        copper(c, 0, 0, DESIGN_W, DESIGN_H, t, ms, energy, 6);
        scene_animation::wire_cube(c, 320, 230, 80.f, t.textValue, ms, bpm);
        const std::string song = "NOW LOADED: " + name;
        c.draw_label(Canvas::clip_label(song, 600, 18), (DESIGN_W - std::min(600, Canvas::label_width(song, 18))) / 2,
                     40, t.textTitle, 18);
        scroller(c, 0, 420, DESIGN_W, 10, greets, t, ms + 14000, 18, 80);
    }
    // Scanlines over everything: the CRT the scene drew on.
    for (int py = 1; py < DESIGN_H; py += 2) c.fill_rect(0, py, DESIGN_W, 1, 0x22000000u);
    // A quiet fade-in at the start of each part.
    if (local < 400) c.fill_rect(0, 0, DESIGN_W, DESIGN_H, with_alpha(t.background, 1.f - float(local) / 400.f));
}

void draw_show(Canvas& c, const AppState& s, ShowPeaks* peaks) {
    const Theme& t = s.theme;
    const songcore::Project* p = s.project;
    const uint64_t ms = s.animationMs;
    const int bpm = p ? songcore::tempo_in_force(*p) : 120;

    // Per-track level from the OCTA feed (WAVEFORM_SIZE samples a track), 0…1.
    float level[8] = {};
    float mix = 0;
    for (int track = 0; track < 8; ++track) {
        if (!s.trackWaveforms || !((s.trackMask >> track) & 1)) continue;
        const float* wave = s.trackWaveforms + static_cast<size_t>(track) * kShowWaveformSize;
        double sum = 0;
        for (int i = 0; i < kShowWaveformSize; i += 2) sum += double(wave[i]) * wave[i];
        level[track] = std::min(1.f, float(std::sqrt(sum / (kShowWaveformSize / 2))) * 3.5f);
        mix = std::max(mix, level[track]);
    }

    // A dim plasma breathing with the loudest track, darkened so the columns stand out.
    plasma(c, 0, 0, DESIGN_W, DESIGN_H, t, ms, 10, mix * .6f);
    c.fill_rect(0, 0, DESIGN_W, DESIGN_H, with_alpha(t.background, .62f));

    // The song's name as a sine scroller along the top.
    const std::string name = p && !p->name.empty() ? p->name : "UNTITLED";
    const std::string banner = name + "  ...  " + std::to_string(bpm) + " BPM  ...  TRACKWERK  ...  ";
    scroller(c, 0, 34, DESIGN_W, 10, banner, t, ms, 28, 70);

    // Eight copper columns, one per track, rising with its level.
    constexpr int top = 112, bottom = 372, columnW = 62, gap = 12;
    const int left = (DESIGN_W - 8 * columnW - 7 * gap) / 2;
    for (int track = 0; track < 8; ++track) {
        const int x = left + track * (columnW + gap);
        const bool muted = p && (p->tracks[static_cast<size_t>(track)].mute ||
                                 (songcore::any_solo(*p) && !p->tracks[static_cast<size_t>(track)].solo));
        c.fill_rect(x, top, columnW, bottom - top, with_alpha(t.rowEvery4th, .55f));
        const int height = muted ? 0 : int(level[track] * (bottom - top - 8));
        for (int y = bottom - height; y < bottom; y += 3) {
            const int shade = 120 + (bottom - y) * 135 / (bottom - top);
            c.fill_rect(x + 3, y, columnW - 6, 2, ramp(t, shade));
        }
        if (height > 0) c.fill_rect(x + 1, bottom - height - 2, columnW - 2, 2, t.textValue);
        // The peak cap: it holds the loudest moment 450 ms, then falls about a column a second.
        if (peaks && ms != 0 && s.settings.sceneMotion) {
            float& peak = peaks->peak[track];
            const float now = muted ? 0.f : level[track];
            if (now >= peak) { peak = now; peaks->held[track] = ms; }
            else if (ms > peaks->held[track] + 450 && ms > peaks->last)
                peak = std::max(now, peak - float(ms - peaks->last) * .0009f);
            const int capH = int(peak * (bottom - top - 8));
            if (capH > height + 3) {
                c.fill_rect(x + 3, bottom - capH - 4, columnW - 6, 3, t.rowCursor);
                c.fill_rect(x + 3, bottom - capH - 6, columnW - 6, 2, with_alpha(t.rowCursor, .35f));
            }
        }
        const std::string number = std::to_string(track + 1);
        c.draw_label(number, x + (columnW - Canvas::label_width(number, 18)) / 2, bottom + 6,
                     muted ? t.textEmpty : t.textTitle, 18);
    }

    if (peaks) peaks->last = ms;

    // Position and transport in large type.
    int row = -1, step = -1;
    for (const auto& head : s.playheads) if (head.songRow >= 0) { row = head.songRow; step = head.step; break; }
    char where[48];
    if (row >= 0) std::snprintf(where, sizeof where, "ROW %02X  STEP %X", row, std::max(0, step));
    else std::snprintf(where, sizeof where, "%s", "READY");
    c.draw_label(where, 36, 418, t.textValue, 28);
    const std::string tempo = std::to_string(bpm) + " BPM";
    c.draw_label(tempo, DESIGN_W - 36 - Canvas::label_width(tempo, 28), 418, t.rowCursor, 28);
    const char* hint = s.isPlaying ? "START: STOP    B: BACK" : "START: PLAY    B: BACK";
    c.draw_label(hint, (DESIGN_W - Canvas::label_width(hint, 12)) / 2, 458, t.textParam, 12);
    // A flash on the one of every bar while playing.
    if (s.isPlaying && s.settings.sceneMotion) {
        const uint64_t barMs = 240000ull / static_cast<uint64_t>(std::max(1, bpm));
        const uint64_t into = ms % barMs;
        if (into < 90) c.fill_rect(0, 0, DESIGN_W, DESIGN_H, with_alpha(t.textValue, .10f * (1.f - into / 90.f)));
    }
    for (int py = 1; py < DESIGN_H; py += 2) c.fill_rect(0, py, DESIGN_W, 1, 0x1e000000u);
}

}  // namespace pt::ui::demo_fx
