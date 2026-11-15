#pragma once

#include <cstdint>

#include "ui/buttons.h"
#include "ui/canvas.h"
#include "ui/scene_presets.h"

namespace pt::ui {

inline int scene_picker_start(const Theme& theme) {
    return theme.sceneChrome > 0 && theme.sceneChrome <= kSceneCount ? theme.sceneChrome - 1 : 0;
}

bool scene_picker_button(Button button, int& index, Theme& theme);
// `ms` drives the live preview (a playing row walking the preview, the family's own animation);
// equal `ms`, equal picture.
void draw_scene_picker(Canvas& canvas, int index, const Theme& current, uint64_t ms = 0);

}  // namespace pt::ui
