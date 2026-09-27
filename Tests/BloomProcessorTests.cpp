#include "dsp/BloomProcessor.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

int main()
{
    constexpr std::size_t blockSize = 127;
    constexpr std::size_t totalSamples = 48000 * 12;
    longland::BloomProcessor bloom;
    bloom.setIntensity(1.0f);
    bloom.setDecay(1.0f);
    bloom.prepare(48000.0);

    std::array<float, blockSize> left {};
    std::array<float, blockSize> right {};
    double tailEnergy = 0.0;
    float peak = 0.0f;
    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = std::min(blockSize, totalSamples - offset);
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        if (offset == 0)
            left[0] = right[0] = 1.0f;

        bloom.process(left.data(), right.data(), left.data(), right.data(), count);
        for (std::size_t i = 0; i < count; ++i)
        {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            peak = std::max({peak, std::abs(left[i]), std::abs(right[i])});
            if (offset + i > 2000)
                tailEnergy += left[i] * left[i] + right[i] * right[i];
        }
    }
    assert(peak <= 16.0f);
    assert(tailEnergy > 1.0e-8);

    bloom.reset();
    left.fill(0.0f);
    right.fill(0.0f);
    bloom.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    for (std::size_t i = 0; i < blockSize; ++i)
        assert(left[i] == 0.0f && right[i] == 0.0f);

    // A full-scale control jump must ramp rather than create a discontinuity.
    bloom.setIntensity(0.0f);
    bloom.reset();
    left.fill(0.25f);
    right.fill(0.25f);
    bloom.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    const float beforeJump = left[blockSize - 1];
    bloom.setIntensity(1.0f);
    left.fill(0.25f);
    right.fill(0.25f);
    bloom.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    assert(std::abs(left[0] - beforeJump) < 0.01f);

    bloom.setIntensity(std::numeric_limits<float>::quiet_NaN());
    bloom.setDecay(std::numeric_limits<float>::infinity());
    bloom.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    for (std::size_t i = 0; i < blockSize; ++i)
        assert(std::isfinite(left[i]) && std::isfinite(right[i]));
}
