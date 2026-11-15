#pragma once

#include <array>

#include "ui/scene_presets.h"

namespace pt::ui {

struct SceneLayout {
    bool railLeft = false;
    bool scopeBottom = false;
    bool splitSampler = false;
};

inline SceneLayout scene_layout(const Theme& theme) {
    if (theme.sceneChrome < 1 || theme.sceneChrome > kSceneCount) return {};
    // Order follows the scene families. The four looks in a family share its spatial design.
    static constexpr std::array<SceneLayout, kSceneLookCount> layouts{{
        {true,  false, true },  // Unscii
        {false, true,  false},  // ASCII
        {false, false, false},  // DOS
        {true,  true,  true },  // Terminal
        {true,  false, false},  // ANSI
        {false, true,  true },  // Raster
        {false, false, true },  // Crack intro
        {true,  true,  false},  // Vector
    }};
    return layouts[static_cast<size_t>(scene_look(theme.sceneChrome - 1).family)];
}

}  // namespace pt::ui
