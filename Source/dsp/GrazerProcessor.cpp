#include "dsp/GrazerProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace longland
{
float GrazerProcessor::sanitise(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float GrazerProcessor::clampUnit(const float value) noexcept
{
    return std::clamp(sanitise(value), 0.0f, 1.0f);
}

float GrazerProcessor::bounded(const float value) noexcept
{
    return std::clamp(sanitise(value), -maximumSignal, maximumSignal);
}

std::size_t GrazerProcessor::bandIndex(const Band band) noexcept
{
    switch (band)
    {
        case Band::low:  return 0;
        case Band::mid:  return 1;
        case Band::high: return 2;
        case Band::none: break;
    }
    return 3;
}

void GrazerProcessor::prepare(const double requestedSampleRate,
                              const double requestedSmoothingSeconds) noexcept
{
    const double sampleRate = std::clamp(std::isfinite(requestedSampleRate)
                                             ? requestedSampleRate
                                             : 48000.0,
                                         8000.0,
                                         384000.0);
    const double smoothingSeconds = std::clamp(std::isfinite(requestedSmoothingSeconds)
                                                    ? requestedSmoothingSeconds
                                                    : 0.03,
                                                0.001,
                                                1.0);
    const double lowCutoff = 250.0;
    const double highCutoff = std::min(2500.0, sampleRate * 0.20);

    lowCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * lowCutoff / sampleRate));
    highCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * highCutoff / sampleRate));
    detectorAttack = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.015)));
    detectorRelease = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.180)));
    smoothingCoefficient = static_cast<float>(std::exp(-1.0 / (sampleRate * smoothingSeconds)));
    prepared = true;
    reset();
}

void GrazerProcessor::reset() noexcept
{
    channels = {};
    energy.fill(0.0f);
    bandGain.fill(1.0f);
    intensity = targetIntensity;
    selectedBand = Band::none;
}

void GrazerProcessor::setIntensity(const float normalisedIntensity) noexcept
{
    targetIntensity = clampUnit(normalisedIntensity);
}

std::array<float, 3> GrazerProcessor::split(ChannelState& state, const float input) const noexcept
{
    state.lowpassLow += lowCoefficient * (input - state.lowpassLow);
    state.lowpassHigh += highCoefficient * (input - state.lowpassHigh);
    state.lowpassLow = bounded(state.lowpassLow);
    state.lowpassHigh = bounded(state.lowpassHigh);

    // These complementary differences sum exactly to the input. Consequently
    // zero Grazer intensity is a true, latency-free bypass.
    return {state.lowpassLow,
            state.lowpassHigh - state.lowpassLow,
            input - state.lowpassHigh};
}

void GrazerProcessor::updateDetector(const std::array<float, 3>& left,
                                     const std::array<float, 3>& right) noexcept
{
    for (std::size_t band = 0; band < energy.size(); ++band)
    {
        const float instantaneous = 0.5f * (left[band] * left[band] + right[band] * right[band]);
        const float coefficient = instantaneous > energy[band] ? detectorAttack : detectorRelease;
        energy[band] = instantaneous + coefficient * (energy[band] - instantaneous);
        energy[band] = std::clamp(sanitise(energy[band]), 0.0f, maximumSignal * maximumSignal);
    }
    updateTargetBand();
}

void GrazerProcessor::updateTargetBand() noexcept
{
    const float total = energy[0] + energy[1] + energy[2];
    if (!(total > 1.0e-10f))
    {
        selectedBand = Band::none;
        return;
    }

    std::size_t candidate = 0;
    if (energy[1] > energy[candidate])
        candidate = 1;
    if (energy[2] > energy[candidate])
        candidate = 2;

    const std::size_t current = bandIndex(selectedBand);
    if (current >= energy.size())
    {
        float second = 0.0f;
        for (std::size_t band = 0; band < energy.size(); ++band)
            if (band != candidate)
                second = std::max(second, energy[band]);
        if (energy[candidate] > second * 1.08f)
            selectedBand = static_cast<Band>(candidate + 1);
        return;
    }

    // Hysteresis prevents fast low/mid/high chatter around a crossover.
    if (candidate != current && energy[candidate] > energy[current] * 1.25f)
        selectedBand = static_cast<Band>(candidate + 1);
}

void GrazerProcessor::process(const float* const inputLeft,
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
        const float dryLeft = bounded(inputLeft[sample]);
        const float dryRight = bounded(inputRight[sample]);
        const auto left = split(channels[0], dryLeft);
        const auto right = split(channels[1], dryRight);
        updateDetector(left, right);

        intensity = targetIntensity + smoothingCoefficient * (intensity - targetIntensity);
        intensity = clampUnit(intensity);
        const auto selected = bandIndex(selectedBand);
        const float reducedGain = std::pow(10.0f, -maximumAttenuationDb * intensity / 20.0f);
        for (std::size_t band = 0; band < bandGain.size(); ++band)
        {
            const float target = band == selected ? reducedGain : 1.0f;
            bandGain[band] = target + smoothingCoefficient * (bandGain[band] - target);
            bandGain[band] = std::clamp(sanitise(bandGain[band], 1.0f),
                                        std::pow(10.0f, -maximumAttenuationDb / 20.0f),
                                        1.0f);
        }

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        for (std::size_t band = 0; band < bandGain.size(); ++band)
        {
            wetLeft += left[band] * bandGain[band];
            wetRight += right[band] * bandGain[band];
        }
        outputLeft[sample] = bounded(wetLeft);
        outputRight[sample] = bounded(wetRight);
    }
}

GrazerProcessor::Telemetry GrazerProcessor::telemetry() const noexcept
{
    Telemetry result;
    for (std::size_t band = 0; band < energy.size(); ++band)
        result.bandLevels[band] = std::sqrt(std::max(0.0f, energy[band]));
    result.targetBand = selectedBand;
    result.currentIntensity = intensity;
    result.targetAttenuationDb = selectedBand == Band::none ? 0.0f : maximumAttenuationDb * intensity;
    return result;
}

float GrazerProcessor::currentBandGain(const Band band) const noexcept
{
    const auto index = bandIndex(band);
    return index < bandGain.size() ? bandGain[index] : 1.0f;
}
} // namespace longland

