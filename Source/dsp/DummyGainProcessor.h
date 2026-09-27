#pragma once

#include <cstddef>

namespace longland
{
class DummyGainProcessor
{
public:
    void prepare(double sampleRate, double rampSeconds = 0.02) noexcept;
    void reset() noexcept;
    void setPopulationTarget(double population, double health) noexcept;
    void process(const float* input, float* output, std::size_t samples) noexcept;
    double currentGain() const noexcept { return gain; }

private:
    double gain {};
    double target {};
    double smoothing {0.999};
};
} // namespace longland

