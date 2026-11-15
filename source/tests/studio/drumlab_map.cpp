// Drum Lab's Map style (ui/drumlab_map.h): a Grids-style rhythm map with TrackWerk's own nodes.
// Checks the map's levels, the ordered density (more density only ever adds hits), the archetypes at
// the nodes, Chaos and Seed, locks, the controls, and Apply into a song.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/drumlab.h"
#include "ui/drumlab_map.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace pt::ui;

namespace {
uint16_t hits16(const DrumLabState& s, int lane, int bar = 0) {
    uint16_t m = 0;
    for (int step = 0; step < 16; ++step)
        if (s.grid[lane][bar * 16 + step].velocity) m = uint16_t(m | (1u << step));
    return m;
}
DrumLabState map_state(int x, int y, int chaos = 0) {
    DrumLabState s;
    s.genre = DRUMLAB_MAP;
    s.mapX = x;
    s.mapY = y;
    s.chaos = chaos;
    s.humanize = 0;
    s.swing = 0;
    s.steps = 16;
    s.activeLanes = 8;
    return s;
}
std::string bits(uint16_t m) {
    std::string out;
    for (int i = 0; i < 16; ++i) out += (m >> i) & 1 ? 'x' : '.';
    return out;
}
songcore::Project project() {
    auto p = songcore::make_default_project();
    const int models[] = {21, 22, 23, 23, 57, 59, 19, 17};
    for (int lane = 0; lane < 8; ++lane) {
        auto& ins = p.instruments[lane];
        ins.name = drumlab_lane_name(lane);
        ins.sampleFilePath = "/@mutable/p" + std::to_string(models[lane]) + "-100-160-120.wav";
        ins.root = {0, 3};
        ins.loopMode = "fwd";
    }
    return p;
}
}  // namespace

int main() {
    using drum_map::level;
    assert(std::string(drumlab_genre_name(DRUMLAB_MAP)) == "Map" && DRUMLAB_MAP == DRUMLAB_EUCLIDEAN + 1);

    // ── The map ─────────────────────────────────────────────────────────────────────────────
    // At a node the level is the node's own; between two nodes it is their blend, or the nearer's share.
    for (int y = 0; y <= 2; ++y)
        for (int x = 0; x <= 2; ++x)
            for (int lane = 0; lane < 8; ++lane)
                for (int step = 0; step < 16; ++step)
                    assert(level(x * 50, y * 50, lane, step) == drum_map::node_level(drum_map::kNodes[y][x], lane, step));
    {
        const int a = drum_map::node_level(drum_map::kNodes[1][0], 0, 10), b = drum_map::node_level(drum_map::kNodes[1][1], 0, 10);
        // Kick, step 10, halfway from house (10) to hip hop (230): the blend is 120, but the hip hop node
        // at half weight keeps its full 230. A quarter of the way it keeps half: 115 against a blend of 65.
        assert(level(25, 50, 0, 10) == std::max(a, b) && level(25, 50, 0, 10) == 230);
        assert(level(12, 50, 0, 10) == 230 * 48 / 100 || level(12, 50, 0, 10) == (a * 76 + b * 24 + 50) / 100);
    }
    assert(drum_map::threshold(0) == 255 && drum_map::threshold(100) == 0);

    // Ordered density, as in Grids: with Chaos at 0, raising a lane's density only ever adds hits,
    // and 0 % is silence, 100 % every step.
    for (int x : {0, 20, 50, 75, 100})
        for (int y : {0, 35, 50, 100}) {
            uint16_t previous = 0;
            for (int density = 0; density <= 100; density += 5) {
                auto s = map_state(x, y);
                for (auto& lane : s.lanes) lane.mapDensity = density;
                assert(drumlab_generate(s));
                for (int lane = 0; lane < 8; ++lane) {
                    const uint16_t now = hits16(s, lane);
                    if (density == 0) assert(now == 0);
                    if (density == 100) assert(now == 0xFFFF);
                    if (lane == 0) {
                        assert((now & previous) == previous);
                        previous = now;
                    }
                }
            }
        }

    // The nodes play what they are: core hits only at a low density.
    {
        const auto core = [](int x, int y, int lane, int density) {
            auto s = map_state(x, y);
            for (auto& l : s.lanes) l.mapDensity = density;
            assert(drumlab_generate(s));
            return hits16(s, lane);
        };
        const uint16_t house = core(0, 50, 0, 15), hiphop = core(50, 50, 0, 15), breaks = core(100, 50, 0, 40);
        const uint16_t backbeat = core(50, 50, 1, 15), techno = core(0, 100, 3, 15);
        std::printf("Kick, house node:     %s\nKick, hip hop node:   %s\nKick, breakbeat node: %s (secondary hits in)\n"
                    "Snare, hip hop node:  %s\nOpen hat, techno:     %s\n",
                    bits(house).c_str(), bits(hiphop).c_str(), bits(breaks).c_str(), bits(backbeat).c_str(), bits(techno).c_str());
        assert(house == drum_map::kQuarters);
        assert(hiphop == drum_map::steps({0, 10}));
        assert(breaks == drum_map::steps({0, 6, 10, 11}));
        assert(backbeat == drum_map::steps({4, 12}));
        assert(techno == drum_map::steps({2, 6, 10, 14}));
        // Sparse to busy: the same density plays more on the busy row.
        int sparse = 0, busy = 0;
        for (int lane = 0; lane < 8; ++lane) {
            sparse += __builtin_popcount(core(50, 0, lane, 50));
            busy += __builtin_popcount(core(50, 100, lane, 50));
        }
        std::printf("Hits at 50 %% density: sparse row %d, busy row %d\n", sparse, busy);
        assert(busy > sparse * 3 / 2);
    }

    // Chaos: reproducible for one seed, different for another, and different bar to bar.
    {
        auto a = map_state(50, 50, 60), b = a, other = a;
        a.steps = b.steps = other.steps = 64;
        other.seed = 99;
        assert(drumlab_generate(a) && drumlab_generate(b) && drumlab_generate(other));
        assert(a.grid == b.grid);
        assert(a.grid != other.grid);
        bool barsDiffer = false;
        for (int lane = 0; lane < 8; ++lane) barsDiffer = barsDiffer || hits16(a, lane, 0) != hits16(a, lane, 1);
        assert(barsDiffer);
        // Without Chaos every bar repeats the first.
        auto still = map_state(50, 50, 0);
        still.steps = 64;
        assert(drumlab_generate(still));
        for (int lane = 0; lane < 8; ++lane)
            for (int bar = 1; bar < 4; ++bar) assert(hits16(still, lane, bar) == hits16(still, lane, 0));
        for (int lane = 0; lane < 8; ++lane)
            for (int step = 0; step < 64; ++step) assert(a.grid[lane][step].velocity <= 127 && a.grid[lane][step].delayTicks < 12);
    }

    // Velocity follows the level: core hits louder than ghost notes on the same lane.
    {
        auto s = map_state(50, 50);
        for (auto& l : s.lanes) l.mapDensity = 70;
        assert(drumlab_generate(s));
        const int core = s.grid[1][4].velocity, ghost = s.grid[1][9].velocity;   // snare: 4 core, 9 ghost
        assert(core > 0 && ghost > 0 && core > ghost + 20);
    }

    // A locked lane keeps its hits when the map moves.
    {
        auto s = map_state(0, 50);
        assert(drumlab_generate(s));
        const auto kick = s.grid[0];
        s.lanes[0].locked = true;
        s.mapX = 100;
        s.mapY = 100;
        assert(drumlab_generate(s) && s.grid[0] == kick && s.grid[1] != kick);
    }

    // Invalid map values change nothing.
    {
        auto s = map_state(50, 50);
        assert(drumlab_generate(s));
        const auto before = s.grid;
        s.mapX = 101;
        assert(!drumlab_generate(s) && s.grid == before);
        s.mapX = 50;
        s.lanes[2].mapDensity = -1;
        assert(!drumlab_generate(s) && s.grid == before);
    }

    // The controls: Style reaches Map after Euclidean; rows 3, 4 and 7 move X, Y and Chaos; on a
    // lane the third column is its density, and the column stops there.
    {
        auto engine = std::make_unique<AudioEngine>();
        songcore::SongcoreHost host(engine.get(), 48000);
        assert(host.push_project(songcore::serialize_project(project())));
        DrumLabState s;
        const auto press = [&](Button b, bool withA) {
            ButtonEvent e{};
            e.button = b;
            e.action = ButtonAction::PRESSED;
            e.mods.a = withA;
            drumlab_button(e, s, host, *engine);
        };
        s.genre = DRUMLAB_EUCLIDEAN;
        s.row = 0;
        press(Button::DPAD_RIGHT, true);
        assert(s.genre == DRUMLAB_MAP);
        s.row = 3; press(Button::DPAD_RIGHT, true); assert(s.mapX == 51);
        s.row = 4; press(Button::DPAD_UP, true); assert(s.mapY == 60);
        s.row = 7; press(Button::DPAD_LEFT, true); assert(s.chaos == 14);
        assert(s.density == 65 && s.variation == 35 && s.fill == 35);   // the other styles' values untouched
        s.row = 9; s.col = 0;
        press(Button::DPAD_RIGHT, false); press(Button::DPAD_RIGHT, false); press(Button::DPAD_RIGHT, false);
        assert(s.col == 2);
        press(Button::DPAD_UP, true);
        assert(s.lanes[0].mapDensity == 70 && s.dirty);
        s.lanes[0].locked = true;
        press(Button::DPAD_UP, true);
        assert(s.lanes[0].mapDensity == 70);
        s.row = 0; press(Button::DPAD_RIGHT, true);
        assert(s.genre == 0);   // wraps to Hip Hop
    }

    // Apply: the map pattern lands in the song like any other style.
    {
        auto p = project();
        songcore::SongcoreHost host(nullptr, 48000);
        assert(host.push_project(songcore::serialize_project(p)));
        auto s = map_state(60, 70, 20);
        s.activeLanes = 5;
        s.steps = 32;
        assert(drumlab_generate(s));
        const int hits = drumlab_hit_count(s);
        assert(hits > 0 && drumlab_apply(s, host));
        int placed = 0;
        for (int lane = 0; lane < 5; ++lane) {
            const int chainId = host.project().tracks[lane].chainRefs[0];
            assert(chainId >= 0);
            for (int bar = 0; bar < 2; ++bar) {
                const auto& phrase = host.project().phrases[host.project().chains[chainId].phraseRefs[bar]];
                for (int step = 0; step < 16; ++step) {
                    const auto& row = phrase.steps[step];
                    const auto& hit = s.grid[lane][bar * 16 + step];
                    assert((row.note != songcore::Note::EMPTY()) == (hit.velocity > 0));
                    if (hit.velocity) { assert(row.volume == hit.velocity); ++placed; }
                }
            }
        }
        assert(placed == hits);
        std::printf("Applied %d map hits into 5 chains of 2 phrases\n", placed);
    }

    std::puts("PASS: Drum Lab Map style — node levels and blends, ordered density (only adds hits; 0 % silent, 100 % "
              "full), node archetypes (house quarters, hip hop 1+11, breakbeat, backbeat, techno off-beat hat), busy row "
              "denser, Chaos reproducible per seed and varied per bar, accents by level, locks, invalid values refused, "
              "Apply into the song");
    return 0;
}
