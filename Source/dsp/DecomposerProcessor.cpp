#include "dsp/DecomposerProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace longland
{
float DecomposerProcessor::SmoothedValue::next() noexcept
{
    current = target + coefficient * (current - target);
    if (!std::isfinite(current))
        current = target;
    return current;
}

float DecomposerProcessor::sanitise(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float DecomposerProcessor::clampUnit(const float value) noexcept
{
    return std::clamp(sanitise(value), 0.0f, 1.0f);
}

float DecomposerProcessor::boundSignal(const float value) noexcept
{
    return std::clamp(sanitise(value), -maximumSignal, maximumSignal);
}

double DecomposerProcessor::wrapPosition(double position, const std::size_t length) noexcept
{
    if (!std::isfinite(position) || length == 0)
        return 0.0;
    const double size = static_cast<double>(length);
    position -= std::floor(position / size) * size;
    return position >= size ? 0.0 : position;
}

float DecomposerProcessor::grainWindow(const std::uint32_t age,
                                       const std::uint32_t length) noexcept
{
    if (length < 2 || age >= length)
        return 0.0f;
    const double phase = static_cast<double>(age) / static_cast<double>(length - 1);
    return static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase));
}

void DecomposerProcessor::prepare(const double requestedSampleRate,
                                  const double maximumBufferMilliseconds)
{
    sampleRateHz = std::clamp(std::isfinite(requestedSampleRate) ? requestedSampleRate : 48000.0,
                              8000.0,
                              384000.0);
    const double bufferMs = std::clamp(
        std::isfinite(maximumBufferMilliseconds) ? maximumBufferMilliseconds : 1600.0,
        500.0,
        5000.0);
    const auto length = std::max<std::size_t>(16,
        static_cast<std::size_t>(std::ceil(sampleRateHz * bufferMs * 0.001)) + 4);
    for (auto& channel : captureBuffer)
        channel.assign(length, 0.0f);

    intensity.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.025)));
    grainSizeMs.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.080)));
    density.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.080)));
    scatter.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.080)));
    prepared = true;
    reset();
}

void DecomposerProcessor::reset() noexcept
{
    for (auto& channel : captureBuffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    grains = {};
    writeIndex = 0;
    recordedSamples = 0;
    samplesUntilNextGrain = 0.0;
    randomState = requestedSeed == 0 ? defaultSeed : requestedSeed;
    intensity.snap();
    grainSizeMs.snap();
    density.snap();
    scatter.snap();
}

void DecomposerProcessor::setIntensity(const float value) noexcept
{
    intensity.target = clampUnit(value);
}

void DecomposerProcessor::setGrainSizeMilliseconds(const float value) noexcept
{
    grainSizeMs.target = std::clamp(sanitise(value, 54.0f), 12.0f, 180.0f);
}

void DecomposerProcessor::setDensity(const float value) noexcept
{
    density.target = clampUnit(value);
}

void DecomposerProcessor::setScatter(const float value) noexcept
{
    scatter.target = clampUnit(value);
}

void DecomposerProcessor::setRandomSeed(const std::uint32_t seed) noexcept
{
    requestedSeed = seed == 0 ? defaultSeed : seed;
    randomState = requestedSeed;
}

std::size_t DecomposerProcessor::activeGrainCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(grains.begin(), grains.end(),
        [](const Grain& grain) { return grain.active; }));
}

std::uint32_t DecomposerProcessor::nextRandom() noexcept
{
    // xorshift32 has fixed, cross-platform integer behaviour and no hidden state.
    std::uint32_t value = randomState == 0 ? defaultSeed : randomState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState = value;
    return value;
}

float DecomposerProcessor::randomUnit() noexcept
{
    return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
}

float DecomposerProcessor::readInterpolated(const std::size_t channel,
                                            const double requestedPosition) const noexcept
{
    const auto& buffer = captureBuffer[channel];
    if (buffer.empty())
        return 0.0f;
    const double position = wrapPosition(requestedPosition, buffer.size());
    const auto first = static_cast<std::size_t>(position);
    const auto second = first + 1 == buffer.size() ? 0 : first + 1;
    const float fraction = static_cast<float>(position - static_cast<double>(first));
    return boundSignal(buffer[first] + fraction * (buffer[second] - buffer[first]));
}

void DecomposerProcessor::launchGrain(const float requestedGrainMs,
                                      const float scatterAmount) noexcept
{
    auto freeGrain = std::find_if(grains.begin(), grains.end(),
                                  [](const Grain& grain) { return !grain.active; });
    if (freeGrain == grains.end() || captureBuffer[0].empty())
        return;

    const float sizeJitter = 0.82f + 0.36f * randomUnit();
    const auto length = static_cast<std::uint32_t>(std::clamp(
        sampleRateHz * 0.001 * static_cast<double>(requestedGrainMs * sizeJitter),
        16.0,
        static_cast<double>(captureBuffer[0].size() / 3)));
    const double pitchSpreadOctaves = 0.22 * static_cast<double>(scatterAmount);
    const double increment = std::exp2((2.0 * static_cast<double>(randomUnit()) - 1.0)
                                       * pitchSpreadOctaves);
    const double minimumLookback = static_cast<double>(length) * std::max(1.0, increment) + 4.0;
    const double maximumExtra = std::min(sampleRateHz * (0.018 + 0.62 * scatterAmount),
        std::max(0.0, static_cast<double>(captureBuffer[0].size()) - minimumLookback - 3.0));
    const double lookback = minimumLookback + maximumExtra * randomUnit();
    if (recordedSamples < static_cast<std::size_t>(std::ceil(lookback)) + 2)
        return;

    freeGrain->readPosition = wrapPosition(static_cast<double>(writeIndex) - lookback,
                                           captureBuffer[0].size());
    freeGrain->increment = increment;
    freeGrain->age = 0;
    freeGrain->length = length;
    freeGrain->pan = (2.0f * randomUnit() - 1.0f) * scatterAmount;
    freeGrain->gain = 0.42f + 0.28f * randomUnit();
    freeGrain->active = true;
}

void DecomposerProcessor::process(const float* const inputLeft,
                                  const float* const inputRight,
                                  float* const outputLeft,
                                  float* const outputRight,
                                  const std::size_t samples) noexcept
{
    if (outputLeft == nullptr || outputRight == nullptr)
        return;
    if (!prepared || inputLeft == nullptr || inputRight == nullptr)
    {
        std::fill_n(outputLeft, samples, 0.0f);
        std::fill_n(outputRight, samples, 0.0f);
        return;
    }

    for (std::size_t sample = 0; sample < samples; ++sample)
    {
        const float dryLeft = boundSignal(inputLeft[sample]);
        const float dryRight = boundSignal(inputRight[sample]);
        captureBuffer[0][writeIndex] = dryLeft;
        captureBuffer[1][writeIndex] = dryRight;
        recordedSamples = std::min(recordedSamples + 1, captureBuffer[0].size());

        const float wetAmount = clampUnit(intensity.next());
        const float sizeMs = std::clamp(grainSizeMs.next(), 12.0f, 180.0f);
        const float densityAmount = clampUnit(density.next());
        const float scatterAmount = clampUnit(scatter.next());

        samplesUntilNextGrain -= 1.0;
        if (samplesUntilNextGrain <= 0.0)
        {
            launchGrain(sizeMs, scatterAmount);
            const double grainsPerSecond = 2.0 + 42.0 * densityAmount;
            const double jitter = 0.72 + 0.56 * static_cast<double>(randomUnit());
            samplesUntilNextGrain = std::max(1.0, sampleRateHz * jitter / grainsPerSecond);
        }

        double wetLeft = 0.0;
        double wetRight = 0.0;
        std::size_t active = 0;
        for (auto& grain : grains)
        {
            if (!grain.active)
                continue;
            const float envelope = grainWindow(grain.age, grain.length);
            const float sourceLeft = readInterpolated(0, grain.readPosition);
            const float sourceRight = readInterpolated(1, grain.readPosition);
            const float cross = 0.5f * (grain.pan + 1.0f);
            const float leftPan = std::sqrt(1.0f - 0.72f * cross);
            const float rightPan = std::sqrt(0.28f + 0.72f * cross);
            wetLeft += static_cast<double>(envelope * grain.gain)
                     * (leftPan * sourceLeft + 0.12f * rightPan * sourceRight);
            wetRight += static_cast<double>(envelope * grain.gain)
                      * (rightPan * sourceRight + 0.12f * leftPan * sourceLeft);
            ++active;

            grain.readPosition = wrapPosition(grain.readPosition + grain.increment,
                                              captureBuffer[0].size());
            if (++grain.age >= grain.length)
                grain.active = false;
        }

        // A fixed, bounded overlap compensation avoids gain jumps as individual
        // grains enter and leave while retaining audible density variation.
        // Do not derive gain from the instantaneous active count: even though a
        // new grain begins at zero, renormalising all older grains at that moment
        // would create a discontinuity. The fixed pool and final signal bound make
        // a constant overlap gain both click-free and safe.
        const float overlapGain = active == 0 ? 0.0f : 0.42f;
        const float dryGain = 1.0f - 0.72f * wetAmount;
        const float wetGain = wetAmount * overlapGain;
        outputLeft[sample] = boundSignal(dryGain * dryLeft
                                         + wetGain * static_cast<float>(wetLeft));
        outputRight[sample] = boundSignal(dryGain * dryRight
                                          + wetGain * static_cast<float>(wetRight));

        if (++writeIndex == captureBuffer[0].size())
            writeIndex = 0;
    }
}
} // namespace longland
