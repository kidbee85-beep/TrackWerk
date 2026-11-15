#pragma once
// Drum Lab's Map style: a rhythm map in the manner of Mutable Instruments' Grids — the patterns are
// TrackWerk's own, not Grids' data. Nine nodes on a 3 × 3 map:
//   X  straight (four on the floor) → backbeat (hip hop, funk) → broken (breakbeat, drum & bass)
//   Y  sparse / half-time → standard → busy
// Each node gives every lane and 16th step a level 0…255: core hits 230, secondary 175, ghost notes
// 110, and a small metric weight everywhere else (quarters 30, eighths 20, sixteenths 10). The map
// position blends the four surrounding nodes (and the nearest keep a share of their own hits, see level());
// a lane plays the steps whose level clears its density
// threshold, so raising the density adds hits in musical order: core from about 10 %, secondary from
// about 32 %, ghost notes from about 57 %, then the remaining grid. Pure functions; no state.
#include <algorithm>
#include <cstdint>
#include <initializer_list>

namespace pt::ui::drum_map {

inline constexpr int kLanes = 8;        // Kick, Snare, Closed Hat, Open Hat, Perc 1, Perc 2, Tom, FX
inline constexpr int kCore = 230, kSecondary = 175, kGhost = 110;

constexpr uint16_t steps(std::initializer_list<int> list) {
    uint16_t m = 0;
    for (int s : list) m = uint16_t(m | (1u << s));
    return m;
}
inline constexpr uint16_t kEighths = 0x5555, kOffSixteenths = 0xAAAA, kQuarters = 0x1111;

struct Lane { uint16_t core, secondary, ghost; };
struct Node { Lane lanes[kLanes]; };

// [y][x]
inline constexpr Node kNodes[3][3] = {
    {   // sparse
        {{{steps({0, 8}), steps({4, 12}), steps({14})}, {0, steps({12}), steps({4})},
          {steps({2, 6, 10, 14}), 0, steps({8})}, {0, steps({14}), 0}, {0, steps({7}), steps({15})},
          {0, 0, steps({11})}, {0, 0, 0}, {steps({0}), 0, 0}}},                                   // minimal
        {{{steps({0}), steps({10}), steps({3, 7})}, {steps({8}), 0, steps({14})},
          {kQuarters, steps({2, 10}), steps({6, 14})}, {0, 0, steps({12})}, {0, steps({6}), steps({15})},
          {0, 0, steps({3})}, {0, 0, steps({15})}, {steps({0}), 0, 0}}},                           // half-time
        {{{steps({0}), steps({11}), steps({6})}, {steps({8}), 0, steps({15})},
          {0, steps({4, 12}), steps({2, 6, 10, 14})}, {0, steps({6}), steps({14})}, {0, steps({3, 13}), 0},
          {0, 0, steps({9})}, {0, steps({14}), 0}, {steps({0}), steps({8}), 0}}},                 // dub, half step
    },
    {   // standard
        {{{kQuarters, 0, steps({14})}, {steps({4, 12}), 0, 0},
          {steps({2, 6, 10, 14}), steps({0, 8}), steps({4, 12})}, {steps({2, 10}), steps({6, 14}), 0},
          {0, steps({3, 11}), steps({7, 15})}, {0, 0, steps({5, 13})}, {0, 0, steps({15})},
          {steps({0}), 0, 0}}},                                                                    // house
        {{{steps({0, 10}), steps({7}), steps({3, 15})}, {steps({4, 12}), 0, steps({9, 15})},
          {kEighths, 0, steps({5, 13})}, {0, steps({14}), 0}, {0, steps({11}), steps({3})},
          {0, 0, steps({7, 15})}, {0, 0, steps({13, 15})}, {steps({0}), 0, 0}}},                  // hip hop, funk
        {{{steps({0, 10}), steps({6, 11}), steps({3})}, {steps({4, 12}), 0, steps({7, 9, 15})},
          {kEighths, 0, 0}, {0, steps({10}), steps({14})}, {0, steps({3, 15}), steps({7})},
          {0, 0, steps({11})}, {0, steps({14}), steps({15})}, {steps({0}), 0, 0}}},               // breakbeat
    },
    {   // busy
        {{{kQuarters, 0, steps({15})}, {0, steps({4, 12}), steps({10})},
          {0, steps({2, 6, 10, 14}), kOffSixteenths}, {steps({2, 6, 10, 14}), 0, 0},
          {steps({3, 11}), steps({7, 15}), 0}, {0, steps({5, 13}), steps({1, 9})},
          {0, 0, steps({14, 15})}, {steps({0}), steps({8}), 0}}},                                  // driving techno
        {{{steps({0, 7, 10}), steps({3, 13}), steps({15})}, {steps({4, 12}), 0, steps({2, 7, 9, 11, 14})},
          {kEighths, kOffSixteenths, 0}, {0, steps({6}), steps({14})}, {0, steps({5, 11}), steps({1, 9})},
          {0, steps({15}), steps({3, 7})}, {0, steps({13, 14, 15}), 0}, {steps({0}), 0, 0}}},     // busy funk
        {{{steps({0, 10}), steps({6}), steps({3, 13})}, {steps({4, 12}), 0, steps({2, 7, 9, 11, 14, 15})},
          {kEighths, kOffSixteenths, 0}, {0, steps({14}), steps({6})}, {0, steps({3, 11, 15}), steps({7})},
          {0, steps({9}), steps({1, 5, 13})}, {0, steps({14, 15}), steps({12})},
          {steps({0}), steps({8}), 0}}},                                                           // drum & bass
    },
};

inline constexpr int node_level(const Node& node, int lane, int step) {
    const Lane& l = node.lanes[lane];
    const uint16_t b = uint16_t(1u << step);
    if (l.core & b) return kCore;
    if (l.secondary & b) return kSecondary;
    if (l.ghost & b) return kGhost;
    return step % 4 == 0 ? 30 : step % 2 == 0 ? 20 : 10;
}

// The blended level at map position x, y (0…100 each) for lane 0…7 and step 0…15.
inline int level(int x, int y, int lane, int step) {
    lane = std::clamp(lane, 0, kLanes - 1);
    step = ((step % 16) + 16) % 16;
    const int fx = std::clamp(x, 0, 100) * 2, fy = std::clamp(y, 0, 100) * 2;   // 0…200: node every 100
    const int ix = std::min(fx / 100, 1), iy = std::min(fy / 100, 1);
    const int tx = fx - ix * 100, ty = fy - iy * 100;                              // 0…100
    const int a = node_level(kNodes[iy][ix], lane, step), b = node_level(kNodes[iy][ix + 1], lane, step);
    const int c = node_level(kNodes[iy + 1][ix], lane, step), d = node_level(kNodes[iy + 1][ix + 1], lane, step);
    const int top = a * (100 - tx) + b * tx, bottom = c * (100 - tx) + d * tx;
    const int blend = (top * (100 - ty) + bottom * ty + 5000) / 10000;
    // A plain blend thins out hits only one neighbour has. The nearest nodes keep a share of their own
    // level as well: weight × 2, at most all of it — a node at half weight or more shows its hits in
    // full, so between two nodes both rhythms are there and the density decides how much of each.
    const int weights[4] = {(100 - tx) * (100 - ty), tx * (100 - ty), (100 - tx) * ty, tx * ty};   // sum 10000
    const int levels[4] = {a, b, c, d};
    int peak = 0;
    for (int i = 0; i < 4; ++i) peak = std::max(peak, levels[i] * std::min(10000, weights[i] * 2) / 10000);
    return std::max(blend, peak);
}

// A lane plays a step when its level (plus chaos) exceeds this, so density 0 % is silence and
// 100 % every step.
inline int threshold(int density) { return 255 - std::clamp(density, 0, 100) * 255 / 100; }

}  // namespace pt::ui::drum_map
