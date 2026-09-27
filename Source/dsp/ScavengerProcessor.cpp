#include "dsp/ScavengerProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace longland
{
namespace
{
constexpr double maximumSignal = 8.0;
constexpr double maximumFeedback = 0.94;
}

double ScavengerProcessor::SmoothedValue::next() noexcept
{
    current = target + coefficient * (current - target);
    if (!std::isfinite(current))
        current = target;
    return current;
}

void ScavengerProcessor::FeedbackFilter::clear() noexcept
{
    lowPass = 0.0;
    previousInput = 0.0;
    highPass = 0.0;
}

double ScavengerProcessor::FeedbackFilter::process(const double input,
                                                    const double lowPassCoefficient,
                                                    const double highPassPole) noexcept
{
    lowPass += lowPassCoefficient * (input - lowPass);
    const auto filtered = lowPass - previousInput + highPassPole * highPass;
    previousInput = lowPass;
    highPass = std::abs(filtered) < 1.0e-24 ? 0.0 : filtered;
    return ScavengerProcessor::boundSignal(highPass);
}

double ScavengerProcessor::sanitise(const double value, const double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

double ScavengerProcessor::clampUnit(const double value) noexcept
{
    return std::clamp(sanitise(value), 0.0, 1.0);
}

double ScavengerProcessor::boundSignal(const double value) noexcept
{
    return std::clamp(sanitise(value), -maximumSignal, maximumSignal);
}

void ScavengerProcessor::prepare(const double requestedSampleRate,
                                 const double requestedMaximumDelaySeconds)
{
    sampleRateHz = std::clamp(sanitise(requestedSampleRate, 48000.0), 8000.0, 384000.0);
    const auto maximumSeconds = std::clamp(sanitise(requestedMaximumDelaySeconds, 2.0), 0.1, 10.0);
    const auto bufferLength = std::max<std::size_t>(4,
        static_cast<std::size_t>(std::ceil(sampleRateHz * maximumSeconds)) + 2);
    for (auto& buffer : delayBuffers)
        buffer.assign(bufferLength, 0.0f);

    maximumDelayMs = 1000.0 * (static_cast<double>(bufferLength) - 2.0) / sampleRateHz;
    delayTimeMs.target = std::clamp(delayTimeMs.target > 0.0 ? delayTimeMs.target : 160.0,
                                    1.0,
                                    maximumDelayMs);
    feedback.target = std::clamp(feedback.target > 0.0 ? feedback.target : 0.35,
                                 0.0,
                                 maximumFeedback);
    diffusion.target = clampUnit(diffusion.target > 0.0 ? diffusion.target : 0.25);

    // Population/feedback changes settle quickly while delay-time changes are
    // deliberately slower, avoiding discontinuous changes in the read head.
    intensity.coefficient = std::exp(-1.0 / (sampleRateHz * 0.020));
    feedback.coefficient = std::exp(-1.0 / (sampleRateHz * 0.030));
    diffusion.coefficient = std::exp(-1.0 / (sampleRateHz * 0.030));
    delayTimeMs.coefficient = std::exp(-1.0 / (sampleRateHz * 0.080));

    const auto lowPassHz = 5200.0;
    lowPassCoefficient = 1.0 - std::exp(-2.0 * std::numbers::pi * lowPassHz / sampleRateHz);
    highPassPole = std::exp(-2.0 * std::numbers::pi * 28.0 / sampleRateHz);
    prepared = true;
    reset();
}

void ScavengerProcessor::reset() noexcept
{
    for (auto& buffer : delayBuffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    for (auto& filter : feedbackFilters)
        filter.clear();
    writeIndex = 0;
    intensity.snap();
    feedback.snap();
    delayTimeMs.snap();
    diffusion.snap();
}

void ScavengerProcessor::setIntensity(const double populationIntensity) noexcept
{
    intensity.target = clampUnit(populationIntensity);
}

void ScavengerProcessor::setFeedback(const double feedbackAmount) noexcept
{
    feedback.target = std::clamp(sanitise(feedbackAmount), 0.0, maximumFeedback);
}

void ScavengerProcessor::setDelayTimeMs(const double milliseconds) noexcept
{
    const auto upper = prepared ? maximumDelayMs : 10000.0;
    delayTimeMs.target = std::clamp(sanitise(milliseconds, 160.0), 1.0, upper);
}

void ScavengerProcessor::setDiffusion(const double normalisedDiffusion) noexcept
{
    diffusion.target = clampUnit(normalisedDiffusion);
}

double ScavengerProcessor::readInterpolated(const std::size_t channel,
                                            const double delaySamples) const noexcept
{
    const auto& buffer = delayBuffers[channel];
    if (buffer.empty())
        return 0.0;

    const auto length = static_cast<double>(buffer.size());
    auto readPosition = static_cast<double>(writeIndex) - delaySamples;
    while (readPosition < 0.0)
        readPosition += length;
    while (readPosition >= length)
        readPosition -= length;

    const auto first = static_cast<std::size_t>(readPosition);
    const auto second = first + 1 == buffer.size() ? 0 : first + 1;
    const auto fraction = readPosition - static_cast<double>(first);
    return static_cast<double>(buffer[first])
         + fraction * (static_cast<double>(buffer[second]) - static_cast<double>(buffer[first]));
}

void ScavengerProcessor::write(const std::size_t channel, const double value) noexcept
{
    delayBuffers[channel][writeIndex] = static_cast<float>(boundSignal(value));
}

void ScavengerProcessor::process(const float* const inputLeft,
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
        // Read both inputs before writing either output, retaining safe in-place
        // operation for normal stereo host buffers.
        const auto cleanLeft = boundSignal(inputLeft[sample]);
        const auto cleanRight = boundSignal(inputRight[sample]);
        const auto population = intensity.next();
        const auto feedbackAmount = feedback.next();
        const auto diffusionAmount = diffusion.next();
        const auto delayMs = std::clamp(delayTimeMs.next(), 1.0, maximumDelayMs);
        const auto baseDelaySamples = delayMs * 0.001 * sampleRateHz;
        const auto leftDelayed = readInterpolated(0, baseDelaySamples);
        // A small population-controlled stereo offset breaks up coincident echoes.
        const auto rightDelay = std::min(baseDelaySamples * (1.0 + 0.021 * diffusionAmount),
                                         static_cast<double>(delayBuffers[1].size() - 2));
        const auto rightDelayed = readInterpolated(1, rightDelay);

        const auto filteredLeft = feedbackFilters[0].process(leftDelayed,
                                                              lowPassCoefficient,
                                                              highPassPole);
        const auto filteredRight = feedbackFilters[1].process(rightDelayed,
                                                               lowPassCoefficient,
                                                               highPassPole);
        // Convex cross-feedback is energy-bounded; the hard feedback ceiling
        // guarantees a lossy loop even at maximum diffusion.
        const auto cross = 0.48 * diffusionAmount;
        const auto feedbackLeft = (1.0 - cross) * filteredLeft + cross * filteredRight;
        const auto feedbackRight = (1.0 - cross) * filteredRight + cross * filteredLeft;
        // Population controls one gain stage only. Applying it at both send and
        // return made moderate populations effectively inaudible, particularly
        // after the ecology mixer applied its own species weight. Keeping the
        // return open also lets already-created material decay naturally when a
        // population falls, while zero population injects no new audio.
        const auto send = 0.90 * population;
        write(0, send * cleanLeft + feedbackAmount * feedbackLeft);
        write(1, send * cleanRight + feedbackAmount * feedbackRight);

        outputLeft[sample] = static_cast<float>(boundSignal(leftDelayed));
        outputRight[sample] = static_cast<float>(boundSignal(rightDelayed));
        if (++writeIndex == delayBuffers[0].size())
            writeIndex = 0;
    }
}
} // namespace longland
