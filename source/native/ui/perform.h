#pragma once
// PERFORM: a screen for playing live on a handheld. Eight tiles, each one control the song carries in
// Project::perform (songcore/model.h):
//   MUTE    a group of tracks, muted and unmuted together
//   FILTER  the master filter sweep (AudioEngine::setPerformFilter): low-pass left, high-pass right
//   MACRO   macro A, B or C of one synth instrument (Morph, Position, Cutoff …), live on the song
//   EMPTY   unused
// Below them, the CPU per track (AudioEngine::getLaneLoads) and the callback's load and dropouts, so
// a heavy engine shows before it breaks up.
//
// Buttons — only those every handheld has:
//   D-pad          choose a tile
//   A              MUTE: toggle the group · FILTER: off · MACRO: nothing
//   A + ↑/↓, ←/→   change the value by 16 / by 4 (MUTE: ↑ mutes, ↓ unmutes)
//   B + ←/→        the tile's kind: EMPTY → MUTE → FILTER → MACRO
//   B + ↑/↓        MACRO: the next / previous synth instrument
//   L + ←/→        MUTE: the track cursor · MACRO: macro A, B, C
//   L + A          MUTE: the track under the cursor in or out of the group
// R + D-pad navigates as everywhere; START plays and stops the song.
#include <cstdint>
#include <string>

#include "songcore/model.h"

class AudioEngine;
namespace songcore { class SongcoreHost; }

namespace pt::ui {

class Canvas;
struct AppState;

// What the engine reports, copied in by the engine feed while PERFORM is on screen.
struct PerformState {
    int cursor = 0;            // 0…7: top row 0…3, bottom row 4…7
    int trackCursor = 0;       // MUTE setup: 0…7
    int laneLoad[10] = {};     // tenths of a percent: tracks 1…8, preview, buses + master
    int meanLoad = 0, worstLoad = 0;
    int64_t lateBlocks = 0;    // since launch
    int64_t lateAtOpen = -1;   // lateBlocks when PERFORM was last opened; −1 = not yet read
    int filter = 128;
    long long polledMs = 0;
};

enum class PerformAction {
    TAP, VALUE_UP, VALUE_DOWN, VALUE_LEFT, VALUE_RIGHT,
    KIND_NEXT, KIND_PREVIOUS, INSTRUMENT_NEXT, INSTRUMENT_PREVIOUS,
    SUB_LEFT, SUB_RIGHT, TRACK_TOGGLE,
};

struct PerformResult {
    bool handled = false;
    bool edited = false;       // the song changed (layout or a synth macro): mark it modified
    std::string status;        // a line for the status bar; empty = none
};

// Applies `action` to the tile under the cursor.
PerformResult perform_action(AppState&, songcore::SongcoreHost&, PerformAction);

// The tile's current value 0…255 (FILTER, a bound MACRO), or −1.
int perform_slot_value(const songcore::Project&, int filter, int slot);
// "MUTE 1-2", "FILTER", "MORPH", "EMPTY" …
std::string perform_slot_title(const songcore::Project&, int slot);
// The second line: "MUTED" / "playing", "LP 40 %", "05 SP MORPH" …
std::string perform_slot_detail(const songcore::Project&, int filter, int slot);
// True when every track of a MUTE tile is muted.
bool perform_group_muted(const songcore::Project&, int slot);

// The CPU warning level of a load in tenths of a percent: 0 fine, 1 high (≥ 70 %), 2 critical (≥ 90 %).
int perform_load_level(int load);

void draw_perform(Canvas&, int x, int y, const AppState&);

}  // namespace pt::ui
