#pragma once
// Reads a voice rendered in fixed native blocks (Plaits/Rings 48 kHz, Braids 96 kHz, Elements
// 32 kHz) at the device's output rate.
//
// Interpolation runs between the previous and the current native sample, so it spans block
// boundaries: the tail of the last block is kept. (Interpolating towards out[i+1] held the last
// sample of every block instead — a step every 16 native samples on Elements at 48 kHz, every
// block on all models at 44.1 kHz.) That costs one native sample of latency.
// Decimation (advance > 1, e.g. Braids 96 kHz → 48 kHz) is a box filter exactly one output sample
// wide: every native position it passes is weighted by the distance it covers.
#include <algorithm>

namespace studio {

struct NativeClock {
    double cursor = 0;   // position in the current block, in native samples
    float  tail   = 0;   // last sample of the previous block

    // Starts the next read on a fresh block whose predecessor was silence.
    void restart(int size) { cursor = size; tail = 0; }

    // `out[0..size)` is the current block; `refill()` overwrites it and may change `size`.
    template <class Refill>
    float next(const float* out, int& size, double advance, Refill&& refill) {
        auto read = [&] {
            if (cursor >= size) { tail = out[size - 1]; cursor -= size; refill(); }
            const int i = std::min(size - 1, static_cast<int>(cursor));
            const float f = static_cast<float>(cursor - i);
            const float a = i > 0 ? out[i - 1] : tail;
            return a + f * (out[i] - a);
        };
        if (advance <= 1.0) {
            const float value = read();
            cursor += advance;
            return value;
        }
        float sum = 0;
        double weight = 0, remaining = advance;
        while (remaining > 1e-9) {
            const double step = std::min(1.0, remaining);
            sum += read() * static_cast<float>(step);
            weight += step;
            cursor += step;
            remaining -= step;
        }
        return sum / static_cast<float>(weight);
    }
};

}  // namespace studio
