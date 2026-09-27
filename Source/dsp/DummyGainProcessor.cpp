#include "dsp/DummyGainProcessor.h"
#include "core/MathSafety.h"
#include <algorithm>
#include <cmath>

namespace longland
{
void DummyGainProcessor::prepare(const double sampleRate, const double rampSeconds) noexcept
{
    const double rate = std::clamp(finiteOr(sampleRate, 48000.0), 8000.0, 384000.0);
    smoothing = std::exp(-1.0 / (rate * std::clamp(rampSeconds, 0.001, 1.0)));
    reset();
}

void DummyGainProcessor::reset() noexcept
{
    gain = target = 0.0;
}

void DummyGainProcessor::setPopulationTarget(const double population, const double health) noexcept
{
    target = 0.01 + 0.99 * smoothStep(unit(population) * unit(health));
}

void DummyGainProcessor::process(const float* input, float* output, const std::size_t samples) noexcept
{
    if (input == nullptr || output == nullptr)
        return;
    for (std::size_t i = 0; i < samples; ++i)
    {
        gain = smoothing * gain + (1.0 - smoothing) * target;
        output[i] = static_cast<float>(std::clamp(finiteOr(input[i]) * gain, -4.0, 4.0));
    }
}
} // namespace longland

