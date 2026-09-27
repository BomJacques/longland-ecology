#include "dsp/BloomProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace longland
{
namespace
{
constexpr float maxSignal = 16.0f;

std::size_t delaySamples(const double sampleRate, const double milliseconds)
{
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(sampleRate * milliseconds * 0.001)));
}
} // namespace

float BloomProcessor::SmoothedValue::next() noexcept
{
    current = target + coefficient * (current - target);
    if (!std::isfinite(current))
        current = target;
    return current;
}

void BloomProcessor::DelayLine::resize(const std::size_t length)
{
    samples.assign(std::max<std::size_t>(1, length), 0.0f);
    clear();
}

void BloomProcessor::DelayLine::clear() noexcept
{
    std::fill(samples.begin(), samples.end(), 0.0f);
    writeIndex = 0;
    damped = 0.0f;
}

float BloomProcessor::DelayLine::read() const noexcept
{
    return samples.empty() ? 0.0f : samples[writeIndex];
}

void BloomProcessor::DelayLine::write(const float value) noexcept
{
    if (samples.empty())
        return;

    samples[writeIndex] = value;
    if (++writeIndex == samples.size())
        writeIndex = 0;
}

float BloomProcessor::Allpass::process(const float input) noexcept
{
    const float delayed = delay.read();
    const float output = delayed - coefficient * input;
    delay.write(BloomProcessor::bounded(input + coefficient * output));
    return BloomProcessor::bounded(output);
}

float BloomProcessor::sanitise(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float BloomProcessor::clampUnit(const float value) noexcept
{
    return std::clamp(sanitise(value), 0.0f, 1.0f);
}

float BloomProcessor::bounded(const float value) noexcept
{
    return std::clamp(sanitise(value), -maxSignal, maxSignal);
}

void BloomProcessor::prepare(const double requestedSampleRate)
{
    const double sampleRate = std::clamp(std::isfinite(requestedSampleRate) ? requestedSampleRate : 48000.0,
                                         8000.0,
                                         384000.0);

    // Mutually prime-ish delay times prevent the four-line FDN from collapsing
    // into an obvious periodic echo. These are the only heap operations.
    constexpr std::array<double, 4> tankTimesMs {29.7, 37.1, 43.7, 53.3};
    constexpr std::array<double, 2> leftDiffuserTimesMs {4.7, 7.9};
    constexpr std::array<double, 2> rightDiffuserTimesMs {5.3, 9.1};

    for (std::size_t i = 0; i < tank.size(); ++i)
        tank[i].resize(delaySamples(sampleRate, tankTimesMs[i]));
    for (std::size_t i = 0; i < leftDiffusers.size(); ++i)
    {
        leftDiffusers[i].delay.resize(delaySamples(sampleRate, leftDiffuserTimesMs[i]));
        rightDiffusers[i].delay.resize(delaySamples(sampleRate, rightDiffuserTimesMs[i]));
    }

    leftDiffusers[0].coefficient = 0.52f;
    leftDiffusers[1].coefficient = 0.46f;
    rightDiffusers[0].coefficient = 0.50f;
    rightDiffusers[1].coefficient = 0.48f;

    // A 20 ms one-pole ramp removes gain/feedback discontinuities.
    const auto smoothing = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.020)));
    intensity.coefficient = smoothing;
    decay.coefficient = smoothing;
    prepared = true;
    reset();
}

void BloomProcessor::reset() noexcept
{
    for (auto& line : tank)
        line.clear();
    for (auto& diffuser : leftDiffusers)
        diffuser.delay.clear();
    for (auto& diffuser : rightDiffusers)
        diffuser.delay.clear();

    intensity.snapToTarget();
    decay.snapToTarget();
}

void BloomProcessor::setIntensity(const float normalisedIntensity) noexcept
{
    intensity.target = clampUnit(normalisedIntensity);
}

void BloomProcessor::setDecay(const float normalisedDecay) noexcept
{
    decay.target = clampUnit(normalisedDecay);
}

void BloomProcessor::process(const float* const inputLeft,
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
        // Read both inputs before either output is written, preserving in-place
        // stereo operation even when all four pointers alias host buffers.
        const float dryLeft = bounded(inputLeft[sample]);
        const float dryRight = bounded(inputRight[sample]);
        const float wetAmount = intensity.next();
        const float decayAmount = decay.next();

        float diffusedLeft = dryLeft;
        float diffusedRight = dryRight;
        for (auto& diffuser : leftDiffusers)
            diffusedLeft = diffuser.process(diffusedLeft);
        for (auto& diffuser : rightDiffusers)
            diffusedRight = diffuser.process(diffusedRight);

        std::array<float, 4> delayed {};
        for (std::size_t i = 0; i < tank.size(); ++i)
        {
            const float raw = tank[i].read();
            // More damping at short decays, slightly brighter at long decays.
            const float damping = 0.14f + 0.18f * decayAmount;
            tank[i].damped += damping * (raw - tank[i].damped);
            tank[i].damped = bounded(tank[i].damped);
            delayed[i] = tank[i].damped;
        }

        // A normalised Hadamard matrix is unitary. Combined with feedback below
        // 0.925, it guarantees a strictly lossy feedback loop.
        const std::array<float, 4> mixed {
            0.5f * (delayed[0] + delayed[1] + delayed[2] + delayed[3]),
            0.5f * (delayed[0] - delayed[1] + delayed[2] - delayed[3]),
            0.5f * (delayed[0] + delayed[1] - delayed[2] - delayed[3]),
            0.5f * (delayed[0] - delayed[1] - delayed[2] + delayed[3])};

        const float feedback = 0.30f + 0.625f * decayAmount; // hard maximum 0.925
        const float send = 0.34f * wetAmount;
        const float mid = 0.5f * (diffusedLeft + diffusedRight);
        const float side = 0.5f * (diffusedLeft - diffusedRight);
        const std::array<float, 4> injection {mid, side, -side, -mid};
        for (std::size_t i = 0; i < tank.size(); ++i)
            tank[i].write(bounded(send * injection[i] + feedback * mixed[i]));

        const float wetLeft = 0.5f * (delayed[0] + delayed[1] - delayed[2] - delayed[3]);
        const float wetRight = 0.5f * (delayed[0] - delayed[1] + delayed[2] - delayed[3]);
        const float dryGain = 1.0f - 0.45f * wetAmount;
        const float wetGain = 0.72f * wetAmount;
        outputLeft[sample] = bounded(dryGain * dryLeft + wetGain * wetLeft);
        outputRight[sample] = bounded(dryGain * dryRight + wetGain * wetRight);
    }
}
} // namespace longland

