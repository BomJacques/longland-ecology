#include "dsp/ParasiteProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace longland
{
float ParasiteProcessor::SmoothedValue::next() noexcept
{
    current = target + coefficient * (current - target);
    if (!std::isfinite(current))
        current = target;
    return current;
}

float ParasiteProcessor::sanitise(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float ParasiteProcessor::clampUnit(const float value) noexcept
{
    return std::clamp(sanitise(value), 0.0f, 1.0f);
}

float ParasiteProcessor::boundSignal(const float value) noexcept
{
    return std::clamp(sanitise(value), -maximumSignal, maximumSignal);
}

double ParasiteProcessor::wrapUnit(double phase) noexcept
{
    if (!std::isfinite(phase))
        return 0.0;
    phase -= std::floor(phase);
    return phase;
}

double ParasiteProcessor::window(const double phase) noexcept
{
    return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * wrapUnit(phase));
}

void ParasiteProcessor::prepare(const double requestedSampleRate,
                                const double requestedMaximumDelayMilliseconds)
{
    sampleRateHz = std::clamp(std::isfinite(requestedSampleRate) ? requestedSampleRate : 48000.0,
                              8000.0,
                              384000.0);
    const double maximumDelayMs = std::clamp(
        std::isfinite(requestedMaximumDelayMilliseconds) ? requestedMaximumDelayMilliseconds : 60.0,
        35.0,
        150.0);
    const auto bufferLength = std::max<std::size_t>(8,
        static_cast<std::size_t>(std::ceil(sampleRateHz * maximumDelayMs * 0.001)) + 4);
    for (auto& buffer : delayBuffers)
        buffer.assign(bufferLength, 0.0f);

    minimumDelaySamples = std::max(2.0, sampleRateHz * 0.008);
    const double desiredMaximum = sampleRateHz * 0.044;
    const double safeMaximum = static_cast<double>(bufferLength) - 3.0;
    delayRangeSamples = std::max(4.0, std::min(desiredMaximum, safeMaximum) - minimumDelaySamples);

    intensity.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.020)));
    pitchRatio.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.050)));
    smear.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.050)));
    feedback.coefficient = static_cast<float>(std::exp(-1.0 / (sampleRateHz * 0.035)));
    smearPhaseIncrement = 0.17 / sampleRateHz;
    prepared = true;
    reset();
}

void ParasiteProcessor::reset() noexcept
{
    for (auto& buffer : delayBuffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    previousWet = {};
    writeIndex = 0;
    upwardPhase = 0.25;
    downwardPhase = 0.75;
    smearPhase = 0.0;
    intensity.snap();
    pitchRatio.snap();
    smear.snap();
    feedback.snap();
}

void ParasiteProcessor::setIntensity(const float normalisedIntensity) noexcept
{
    intensity.target = clampUnit(normalisedIntensity);
}

void ParasiteProcessor::setDetuneCents(const float requestedCents) noexcept
{
    const float cents = std::clamp(sanitise(requestedCents, 12.0f), 0.0f, 30.0f);
    pitchRatio.target = std::exp2(cents / 1200.0f);
}

void ParasiteProcessor::setSmear(const float normalisedSmear) noexcept
{
    smear.target = clampUnit(normalisedSmear);
}

void ParasiteProcessor::setFeedback(const float feedbackAmount) noexcept
{
    feedback.target = std::clamp(sanitise(feedbackAmount, 0.12f), 0.0f, maximumFeedback);
}

float ParasiteProcessor::readInterpolated(const std::size_t channel,
                                          const double requestedDelaySamples) const noexcept
{
    const auto& buffer = delayBuffers[channel];
    if (buffer.empty())
        return 0.0f;

    const double delay = std::clamp(std::isfinite(requestedDelaySamples) ? requestedDelaySamples : minimumDelaySamples,
                                    1.0,
                                    static_cast<double>(buffer.size()) - 2.0);
    const double length = static_cast<double>(buffer.size());
    double readPosition = static_cast<double>(writeIndex) - delay;
    while (readPosition < 0.0)
        readPosition += length;
    while (readPosition >= length)
        readPosition -= length;

    const auto first = static_cast<std::size_t>(readPosition);
    const auto second = first + 1 == buffer.size() ? 0 : first + 1;
    const float fraction = static_cast<float>(readPosition - static_cast<double>(first));
    return boundSignal(buffer[first] + fraction * (buffer[second] - buffer[first]));
}

float ParasiteProcessor::pitchRead(const std::size_t channel,
                                   const double phase,
                                   const double smearOffset) const noexcept
{
    const double firstPhase = wrapUnit(phase);
    const double secondPhase = wrapUnit(firstPhase + 0.5);
    const double firstDelay = minimumDelaySamples + firstPhase * delayRangeSamples + smearOffset;
    const double secondDelay = minimumDelaySamples + secondPhase * delayRangeSamples + smearOffset;
    const double firstWeight = window(firstPhase);
    const double secondWeight = window(secondPhase);
    return boundSignal(static_cast<float>(firstWeight * readInterpolated(channel, firstDelay)
                                        + secondWeight * readInterpolated(channel, secondDelay)));
}

void ParasiteProcessor::process(const float* const inputLeft,
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
        // Read both inputs before writing either output, preserving in-place use.
        const float dryLeft = boundSignal(inputLeft[sample]);
        const float dryRight = boundSignal(inputRight[sample]);
        const float wetAmount = clampUnit(intensity.next());
        const float ratio = std::clamp(pitchRatio.next(), 1.0f, std::exp2(30.0f / 1200.0f));
        const float smearAmount = clampUnit(smear.next());
        const float feedbackAmount = std::clamp(feedback.next(), 0.0f, maximumFeedback) * wetAmount;

        const float feedbackLeft = 0.78f * previousWet[0] + 0.22f * previousWet[1];
        const float feedbackRight = 0.78f * previousWet[1] + 0.22f * previousWet[0];
        delayBuffers[0][writeIndex] = boundSignal(dryLeft + feedbackAmount * feedbackLeft);
        delayBuffers[1][writeIndex] = boundSignal(dryRight + feedbackAmount * feedbackRight);

        const double smearWave = std::sin(2.0 * std::numbers::pi * smearPhase);
        const double smearDepthSamples = sampleRateHz * 0.0025 * smearAmount;
        const float wetLeft = pitchRead(0, upwardPhase, smearDepthSamples * smearWave);
        const float wetRight = pitchRead(1, downwardPhase, -smearDepthSamples * smearWave);
        previousWet = {wetLeft, wetRight};

        // Equal-power-like compensation avoids a large centre bump while the
        // two delayed heads fade in. Zero intensity remains an exact bypass.
        const float dryGain = 1.0f - wetAmount;
        const float wetGain = 0.92f * wetAmount;
        outputLeft[sample] = boundSignal(dryGain * dryLeft + wetGain * wetLeft);
        outputRight[sample] = boundSignal(dryGain * dryRight + wetGain * wetRight);

        const double upwardStep = (static_cast<double>(ratio) - 1.0) / delayRangeSamples;
        const double downwardRatio = 1.0 / static_cast<double>(ratio);
        const double downwardStep = (1.0 - downwardRatio) / delayRangeSamples;
        upwardPhase = wrapUnit(upwardPhase - upwardStep);
        downwardPhase = wrapUnit(downwardPhase + downwardStep);
        smearPhase = wrapUnit(smearPhase + smearPhaseIncrement);
        if (++writeIndex == delayBuffers[0].size())
            writeIndex = 0;
    }
}
} // namespace longland

