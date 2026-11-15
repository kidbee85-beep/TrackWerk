// ptfont-atlas-check: every typeface (the built-in default and the sixteen demo faces) drawn through
// the public Canvas API at every grid scale and label size, with clipping, must hash exactly as it
// did with the byte-per-pixel atlases (recorded before they were packed to 1 bit per pixel). Any
// change to the atlas storage or the glyph loops that moves a single pixel fails here.
#include "ui/canvas.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include "ui/demo_fonts.h"
using namespace pt::ui;
static uint64_t hash(const Canvas& c) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) h = (h ^ c.pixels()[i]) * 1099511628211ull;
    return h;
}
static const uint64_t kGolden[17] = {
    0x2f36a8e194e2ac76ull,
    0xb0d88ffa4f856872ull,
    0xedf9e0a0177a5bfeull,
    0x4a4a65772c809b29ull,
    0xc2ea6a830b5bbbd4ull,
    0x8cb931dbc655e641ull,
    0x3ef5a417f8e8ae09ull,
    0xafbbe69493601c5cull,
    0xd9f74cfb1dd17674ull,
    0xbaabc527a75cb420ull,
    0x3d898e2bfee5d2f3ull,
    0xa4145ff422dacde6ull,
    0x09a9b7a6467dd0f5ull,
    0x3d9d79e9ad5e27eeull,
    0xba027aef27e242bdull,
    0xe5c29a9de118bb89ull,
    0xd54fc31cca7ff277ull
};

int main() {
    std::string ascii;
    for (int c = 32; c < 127; ++c) ascii.push_back(static_cast<char>(c));
    const std::string latin = "äöüÄÖÜß éèàç ñ ø å € «»";
    for (int style = 0; style <= 16; ++style) {
        Canvas c;
        c.set_font_style(style);
        c.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xFF101010u);
        int y = 2;
        for (int scale : {1, 2, 3}) {
            for (size_t k = 0; k < ascii.size(); k += 40) {
                c.draw_text(ascii.substr(k, 40), 2, y, 0xFFE0E0E0u, 1, scale);
                y += 6 * scale + 2;
            }
        }
        for (int size : {10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 24}) {
            c.draw_label(ascii.substr(0, 48), 2, y, 0xC0F0A040u, size); y += size + 3;
            c.draw_label(ascii.substr(48) + latin, 2, y, 0xFF40C0F0u, size); y += size + 3;
        }
        // partial clipping through the middle of glyphs
        c.set_clip(30, 10, 200, 37);
        c.draw_label("Clip ABCxyz 0123", 20, 20, 0xFFFFFFFFu, 18);
        c.draw_text("CLIP 42", 20, 30, 0xFFFFFFFFu, 1, 3);
        c.reset_clip();
        const uint64_t h = hash(c);
        if (h != kGolden[style]) {
            std::printf("FAIL: style %d hashes %016llx, expected %016llx\n", style,
                        static_cast<unsigned long long>(h), static_cast<unsigned long long>(kGolden[style]));
            return 1;
        }
    }
    // The title size (26 and up): the 14-px atlas at twice the size — twice the width, and every
    // atlas pixel a uniform 2x2 block, in every typeface.
    for (int style = 0; style <= 16; ++style) {
        Canvas c;
        c.set_font_style(style);
        c.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xFF000000u);
        const std::string title = "SCENE PRESETS 0123";
        if (Canvas::label_width(title, 28) != 2 * Canvas::label_width(title, 12) ||
            Canvas::label_width(title, 26) != Canvas::label_width(title, 28)) {
            std::printf("FAIL: style %d title width\n", style);
            return 1;
        }
        c.draw_label(title, 10, 10, 0xFFFFFFFFu, 28);
        bool ink = false;
        for (int y = 10; y < 10 + 28; y += 2)
            for (int x = 10; x < 10 + Canvas::label_width(title, 28); x += 2) {
                const auto p = c.pixels()[y * DESIGN_W + x];
                ink |= p != 0xFF000000u;
                if (c.pixels()[y * DESIGN_W + x + 1] != p || c.pixels()[(y + 1) * DESIGN_W + x] != p ||
                    c.pixels()[(y + 1) * DESIGN_W + x + 1] != p) {
                    std::printf("FAIL: style %d title pixel at %d,%d is not a 2x2 block\n", style, x, y);
                    return 1;
                }
            }
        if (!ink) { std::printf("FAIL: style %d title drew nothing\n", style); return 1; }
    }
    static_assert(pt::ui::detail::demoFontAtlasBytes <= 192u * 1024u, "1-bit typeface atlases stay small");
    std::printf("PASS: 17 typefaces pixel-identical to the byte atlases at grid scales 1-3 and label sizes 10-24 with clipping; title size 26+ = 14-px atlas in 2x2 blocks; %u-byte 1-bit atlases\n",
                pt::ui::detail::demoFontAtlasBytes);
    return 0;
}
