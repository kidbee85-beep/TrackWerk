#pragma once

#include <array>

#include "ui/theme.h"

namespace pt::ui {

// A look: the frame, texture, stamps, layout and animation a scene is drawn with (canvas.cpp,
// scene_layout.h, scene_animation.h). The first eight families each have their own look; later
// families pair their own palettes, typefaces and materials with one of these looks.
enum class SceneFamily { UNSCII, ASCII, DOS, TERMINAL, ANSI, RASTER, CRACKTRO, VECTOR };
inline constexpr int kSceneLookCount = 8;

// Families in the picker. ⚠️ Append only: a theme stores its scene as `sceneChrome` = index + 1.
inline constexpr int kSceneFamilyCount = 10;
inline constexpr int kScenesPerFamily = 4;
inline constexpr int kSceneCount = kSceneFamilyCount * kScenesPerFamily;

struct SceneLook {
    SceneFamily family;
    int variant;   // 0…3: the look's density / ornament step
};

struct ScenePreset {
    const char* name;
    Argb ground, panel, cursor, ink, selection;
    int font, material;
    StepGridStyle grid;
};

inline constexpr std::array<ScenePreset, kSceneCount> kScenePresets{{
    {"UNSCII / BLOCK PARTY", 0xff141631,0xff2c315b,0xffb4edff,0xfff5eaff,0xffffafdc,0,16,StepGridStyle::DOTS},
    {"UNSCII / GLYPH LAB",   0xff161220,0xff39264b,0xffffcc83,0xffffeed9,0xffbaffbc,8,5,StepGridStyle::GRID},
    {"UNSCII / TELETEXT",    0xff0d1030,0xff1e2854,0xffffee5f,0xfff4f6ff,0xff65f9ff,10,7,StepGridStyle::BEATS},
    {"UNSCII / VIEWDATA",    0xff09191e,0xff123840,0xff88ffe9,0xffe2fff9,0xffb8d8ff,7,9,StepGridStyle::DOTS},
    {"ASCII / README.NFO",   0xff111112,0xff252527,0xfff0f0ed,0xffe9e9e4,0xffc1c2bc,7,7,StepGridStyle::OFF},
    {"ASCII / INK ON PAPER", 0xffeee4ce,0xffddd1b7,0xff1e2a31,0xff252d2d,0xff3c4852,0,3,StepGridStyle::DOTS},
    {"ASCII / DOT MATRIX",  0xff18231b,0xff273c2d,0xffc5edac,0xffe8f8d5,0xffc8e0ce,9,9,StepGridStyle::DOTS},
    {"ASCII / LOGLINE",     0xff1a1c22,0xff2c303d,0xffd2e6f5,0xffe9f0f2,0xffbcbef3,7,7,StepGridStyle::BEATS},
    {"DOS / COMMAND",       0xff081b58,0xff153476,0xff75f6ee,0xfff1f9ff,0xffffea81,0,7,StepGridStyle::GRID},
    {"DOS / SETUP UTILITY", 0xff17204d,0xff313d79,0xfff7f7ff,0xffe9ebff,0xff98d6ff,8,7,StepGridStyle::GRID},
    {"DOS / AMBER VGA",     0xff211404,0xff412905,0xffffca56,0xffffedc1,0xffefa566,15,13,StepGridStyle::BEATS},
    {"DOS / TURBO MONO",    0xff15191d,0xff303941,0xffe4f2f7,0xffe6f4fd,0xffb2d2ed,11,5,StepGridStyle::GRID},
    {"TERM / GREEN PHOSPHOR",0xff0b1b0c,0xff152f17,0xff9fff83,0xffd5ffca,0xffa6e3ae,0,13,StepGridStyle::BEATS},
    {"TERM / AMBER SHELL",  0xff1b1308,0xff34250e,0xffffc56f,0xffffe9bb,0xffffa686,15,13,StepGridStyle::OFF},
    {"TERM / ICE CONSOLE",  0xff0a1924,0xff183447,0xff9ae8ff,0xffdff6ff,0xffb8bcff,8,7,StepGridStyle::GRID},
    {"TERM / VT PAPER",     0xffe5f1ea,0xffccdcd4,0xff174134,0xff18342a,0xff384b50,7,3,StepGridStyle::OFF},
    {"ANSI / NIGHT BBS",    0xff201035,0xff442261,0xff7cfff1,0xfff6d9ff,0xffffb5ec,0,16,StepGridStyle::GRID},
    {"ANSI / DOOR GAME",    0xff2d1229,0xff4b1d42,0xffffd36c,0xffffeee9,0xfffba5c2,8,15,StepGridStyle::DOTS},
    {"ANSI / BLUE BOARD",   0xff0b1b47,0xff1a3571,0xffa6dcff,0xffe8efff,0xffe9a8ff,11,5,StepGridStyle::BEATS},
    {"ANSI / MODEM GLOW",   0xff1b1831,0xff382c51,0xffe8baff,0xfff8eaff,0xff9afbe4,7,13,StepGridStyle::DOTS},
    {"RASTER / COPPER",     0xff24172a,0xff47293c,0xffffcba2,0xffffefdc,0xffbfc8ff,0,13,StepGridStyle::BEATS},
    {"RASTER / PINK PLASMA",0xff2a0e2a,0xff481b48,0xffffa6e8,0xffffd9fb,0xffc9bcff,8,12,StepGridStyle::GRID},
    {"RASTER / BLUE HOUR",  0xff07172d,0xff183056,0xff9dfbff,0xffe3f4ff,0xffcbb8ff,12,14,StepGridStyle::DOTS},
    {"RASTER / SUN DISC",   0xff251a10,0xff44301b,0xfff9d689,0xfffff2d2,0xffffb3a2,10,10,StepGridStyle::BEATS},
    {"INTRO / CRUNCH",      0xff23171b,0xff43262f,0xffffa57d,0xffffe7da,0xfff5d786,8,12,StepGridStyle::DOTS},
    {"INTRO / BAD SECTOR",  0xff1e1710,0xff3d2d1a,0xfff7e49b,0xfffff1cd,0xffc8f4a9,15,9,StepGridStyle::GRID},
    {"INTRO / BYTE DRIFT",  0xff13172c,0xff272d51,0xffadbefb,0xfff1ebff,0xfffea9d4,0,17,StepGridStyle::BEATS},
    {"INTRO / DISK MAG",    0xff281e17,0xff453527,0xffebca9e,0xfffff0d9,0xffa4e2e2,13,3,StepGridStyle::OFF},
    {"VECTOR / WIREFRAME",  0xff0b1a24,0xff1c3445,0xff84eedf,0xffe0fbff,0xffb2c9ff,8,14,StepGridStyle::GRID},
    {"VECTOR / ORBITAL",    0xff161525,0xff2e2a45,0xffc4b3ff,0xfff0eaff,0xff9df0e0,16,16,StepGridStyle::DOTS},
    {"VECTOR / ISOMETRIC",  0xff11221f,0xff243d36,0xffc1f0a9,0xffedf9de,0xff9be4e6,9,15,StepGridStyle::BEATS},
    {"VECTOR / ACID GRID",  0xff171e0b,0xff303d14,0xffd9fa71,0xffeff8cd,0xff9fe5e2,11,17,StepGridStyle::GRID},
    // Daylight: light grounds and dark ink for a handheld screen outdoors.
    {"DAYLIGHT / NEWSPRINT",  0xfff3f1ea,0xffdedacd,0xff151515,0xff161616,0xffb3261e,13,3,StepGridStyle::DOTS},
    {"DAYLIGHT / SUNNY LCD",  0xffc6d1b0,0xffb2bd99,0xff1f2a14,0xff1b2411,0xff2a3b17,14,7,StepGridStyle::GRID},
    {"DAYLIGHT / WHITE PRINT",0xfff1f5fa,0xffd6e2f0,0xff0b3d91,0xff0e2440,0xff9a3006,11,5,StepGridStyle::GRID},
    {"DAYLIGHT / HIGH CONTRAST",0xffffffff,0xffe4e4e4,0xff000000,0xff000000,0xff0047ff,7,7,StepGridStyle::BEATS},
    // Hardware: the front panel of an instrument.
    {"HARDWARE / ALU OLED",   0xff0a0b0c,0xff2b2e32,0xffeef1f3,0xffdfe4e8,0xffff6a2b,8,1,StepGridStyle::BEATS},
    {"HARDWARE / ORANGE KEYS",0xff1a1b1d,0xff36383b,0xffff8a1f,0xfff2f2ee,0xff7fd1c7,12,11,StepGridStyle::GRID},
    {"HARDWARE / CREAM CASE", 0xffe8e0cc,0xffd2c6a9,0xff2b2b2b,0xff28241f,0xff8f2414,15,8,StepGridStyle::DOTS},
    {"HARDWARE / BUNKA SIGNAL",0xff0f1418,0xff1e2a33,0xff4fe3c1,0xffe9f3f1,0xfff7b84b,16,17,StepGridStyle::DOTS},
}};

// The look of each preset. The first 32 are their own family's; later ones borrow one.
inline constexpr SceneLook scene_look(int index) {
    constexpr SceneLook added[] = {
        {SceneFamily::ASCII, 1}, {SceneFamily::TERMINAL, 3}, {SceneFamily::VECTOR, 0}, {SceneFamily::DOS, 0},
        {SceneFamily::DOS, 3}, {SceneFamily::ANSI, 2}, {SceneFamily::RASTER, 3}, {SceneFamily::UNSCII, 1},
    };
    static_assert(sizeof(added) / sizeof(added[0]) == kSceneCount - kSceneLookCount * kScenesPerFamily);
    if (index < 0) index = 0;
    if (index < kSceneLookCount * kScenesPerFamily)
        return {static_cast<SceneFamily>(index / kScenesPerFamily), index % kScenesPerFamily};
    return added[index < kSceneCount ? index - kSceneLookCount * kScenesPerFamily : 0];
}

// Used to update untouched factory typography saved by the first scene release.
inline constexpr std::array<int, kSceneCount> kOriginalSceneFonts{{
    4, 2, 4, 10, 1, 13, 9, 7, 2, 4, 15, 11, 1, 15, 14, 7,
    4, 2, 4, 10, 2, 6, 10, 4, 4, 5, 6, 13, 3, 11, 3, 3,
    13, 14, 11, 7, 8, 12, 15, 16,   // added with their current typefaces: nothing to migrate
}};

inline const char* scene_family_name(int family) {
    constexpr const char* names[] = {"UNSCII", "ASCII", "DOS", "TERMINAL", "ANSI BBS", "RASTER", "CRACK INTRO", "VECTOR",
                                     "DAYLIGHT", "HARDWARE"};
    static_assert(sizeof(names) / sizeof(names[0]) == kSceneFamilyCount);
    return names[family];
}

inline Theme scene_preset(int index, VisualizerType visualizer) {
    const auto& p = kScenePresets[static_cast<size_t>(index)];
    Theme t = theme_palette(p.name, p.ground, p.panel, p.cursor, p.ink, p.selection);
    t.textValue = p.ink;
    t.textParam = mix_rgb(p.ink, p.panel, 30);
    t.textEmpty = mix_rgb(p.ink, p.panel, 48);
    t.textCursor = p.cursor;
    t.rowPlayback = p.cursor;
    t.vizCenterLine = mix_rgb(p.cursor, p.ground, 66);
    t.meterBorder = mix_rgb(p.ink, p.panel, 56);
    t.surfaceStyle = p.material;
    t.fontOverride = p.font;
    t.sceneChrome = index + 1;
    t.visualizerType = visualizer;
    t.chainGrid = p.grid;
    t.phraseGrid = p.grid;
    derive_borrowed_colors(t);
    t.textPlayhead = p.cursor;
    t.eqFill = mix_rgb(p.ink, p.ground, 73);
    return t;
}

}  // namespace pt::ui
