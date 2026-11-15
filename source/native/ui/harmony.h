#pragma once

#include "ui/buttons.h"
#include "songcore/model.h"
#include <array>
#include <cstdint>
#include <string>

class AudioEngine;
namespace songcore { class SongcoreHost; }
namespace pt::ui {
class Canvas;
struct AppState;
struct HarmonySlot {
    int note = 48;
    int chord = 0;
    bool locked = false;
};

struct HarmonyState {
    std::array<HarmonySlot, 8> slots{};
    int count = 4, key = 0, style = 0, preset = 0, cursor = 0, slot = 0;
    int strum = 0, instrument = -1, sounding = -1, loopSlot = 0;
    std::array<int, 2> secret{};
    int sparkKind = 0;
    uint32_t seed = 1;
    int64_t nextFrame = 0;
    uint64_t sparkUntil = 0;
    bool initialized = false, loop = false, held = false, write = false, confirm = false;
    std::string status;
};

void harmony_load(HarmonyState&, bool keepLocks);
void harmony_stop(HarmonyState&, songcore::SongcoreHost&);
bool harmony_button(const ButtonEvent&, AppState&, songcore::SongcoreHost&, AudioEngine&);
bool harmony_tick(AppState&, songcore::SongcoreHost&, AudioEngine&, uint64_t now);
void harmony_draw(Canvas&, const AppState&);
bool harmony_supported(const songcore::Instrument&);
bool harmony_fill_phrase(const HarmonyState&, songcore::Phrase&);
}
