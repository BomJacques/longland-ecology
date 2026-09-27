#include "analysis/ResourceAnalyzer.h"
#include "core/MathSafety.h"
#include <algorithm>
#include <cmath>

namespace longland
{
namespace
{
constexpr double pi = 3.14159265358979323846;

double db(const double linear) noexcept
{
    return linear > 1.0e-12 ? 20.0 * std::log10(linear) : -120.0;
}

double similarity(const std::array<double, 5>& a, const std::array<double, 5>& b) noexcept
{
    double distance = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        distance += std::abs(a[i] - b[i]);
    return unit(1.0 - distance / static_cast<double>(a.size()));
}
} // namespace

void ResourceAnalyzer::prepare(Config config) noexcept
{
    settings = config;
    settings.sampleRate = std::clamp(finiteOr(settings.sampleRate, 48000.0), 8000.0, 384000.0);
    settings.updateRateHz = std::clamp(finiteOr(settings.updateRateHz, 50.0), 10.0, 200.0);
    samplesPerFrame = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(settings.sampleRate / settings.updateRateHz)));
    reset();
}

void ResourceAnalyzer::reset() noexcept
{
    samplesInFrame = totalSamples = 0;
    sumPower = peak = sumTransient = 0.0;
    sumLowPower = sumMidPower = sumHighPower = 0.0;
    activeSamples = fastEnvelope = slowEnvelope = previousSlowEnvelope = 0.0;
    lowStates = {};
    midLowStates = {};
    tailState = previousFrameRms = 0.0;
    previousFingerprintRms = previousFingerprintFlux = 0.0;
    fingerprints = {};
    fingerprintWrite = fingerprintCount = onsetCount = 0;
    samplesSinceOnset = samplesPerFrame;
    latestFrame = {};
}

void ResourceAnalyzer::processBlock(const float* const* channels,
                                    const std::size_t numChannels,
                                    const std::size_t numSamples) noexcept
{
    if (channels == nullptr || numChannels == 0)
        return;

    for (std::size_t sample = 0; sample < numSamples; ++sample)
    {
        double power = 0.0;
        double lowPower = 0.0;
        double midPower = 0.0;
        double highPower = 0.0;
        const double lowA = std::exp(-2.0 * pi * 250.0 / settings.sampleRate);
        const double midA = std::exp(-2.0 * pi * 4000.0 / settings.sampleRate);
        for (std::size_t channel = 0; channel < numChannels; ++channel)
        {
            const double value = std::clamp(finiteOr(channels[channel][sample]), -4.0, 4.0);
            power += value * value;
            const auto stateIndex = std::min<std::size_t>(channel, lowStates.size() - 1);
            auto& lowState = lowStates[stateIndex];
            auto& midLowState = midLowStates[stateIndex];
            lowState = lowA * lowState + (1.0 - lowA) * value;
            midLowState = midA * midLowState + (1.0 - midA) * value;
            const double low = lowState;
            const double mid = midLowState - lowState;
            const double high = value - midLowState;
            lowPower += low * low;
            midPower += mid * mid;
            highPower += high * high;
        }
        power /= static_cast<double>(numChannels); // anti-phase stereo retains energy here
        sumLowPower += lowPower / static_cast<double>(numChannels);
        sumMidPower += midPower / static_cast<double>(numChannels);
        sumHighPower += highPower / static_cast<double>(numChannels);
        accumulateSample(power);
    }
}

void ResourceAnalyzer::accumulateSample(const double power) noexcept
{
    const double magnitude = std::sqrt(std::max(0.0, power));
    const double fastA = std::exp(-1.0 / (settings.sampleRate * 0.005));
    const double slowA = std::exp(-1.0 / (settings.sampleRate * 0.060));
    fastEnvelope = fastA * fastEnvelope + (1.0 - fastA) * magnitude;
    slowEnvelope = slowA * slowEnvelope + (1.0 - slowA) * magnitude;
    const double transient = std::max(0.0, fastEnvelope - slowEnvelope);

    sumPower += power;
    peak = std::max(peak, magnitude);
    sumTransient += transient;
    activeSamples += magnitude > 0.005 ? 1.0 : 0.0;
    ++samplesInFrame;
    ++totalSamples;
    ++samplesSinceOnset;

    if (transient > 0.055 && samplesSinceOnset > static_cast<std::size_t>(settings.sampleRate * 0.035))
    {
        ++onsetCount;
        samplesSinceOnset = 0;
    }

    if (samplesInFrame >= samplesPerFrame)
        finishFrame();
}

void ResourceAnalyzer::finishFrame() noexcept
{
    const double count = static_cast<double>(std::max<std::size_t>(samplesInFrame, 1));
    const double rms = std::sqrt(sumPower / count);
    const double bandTotal = sumLowPower + sumMidPower + sumHighPower + 1.0e-15;
    const double lowShare = sumLowPower / bandTotal;
    const double midShare = sumMidPower / bandTotal;
    const double highShare = sumHighPower / bandTotal;
    const double activity = unit((db(rms) + 72.0) / 60.0);
    const double crestTransient = std::max(0.0, peak - 1.5 * rms);
    const double transient = unit((sumTransient / count) * 20.0 + crestTransient * 1.5) * activity;
    const double density = activeSamples / count;
    const double downward = unit((previousFrameRms - rms) / std::max(previousFrameRms, 1.0e-6));
    const double tailCandidate = activity * (1.0 - transient) * (0.25 + 0.75 * downward);
    const double tailA = smoothingCoefficient(count / settings.sampleRate, tailCandidate > tailState ? 0.10 : 1.2);
    tailState = tailA * tailState + (1.0 - tailA) * tailCandidate;

    const double flux = std::abs(rms - previousFingerprintRms)
                      + std::abs(transient - previousFingerprintFlux);
    const std::array<double, 5> fingerprint {activity, transient, lowShare, midShare, highShare};
    double repetition = 0.0;
    // A single-frame nearest match mistakes unrelated material with a similar
    // level/band balance for repetition. Require a short sequence to recur at
    // one consistent lag, which separates a phrase from coincidental frames.
    constexpr std::size_t sequenceFrames = 4;
    if (fingerprintCount >= 8 + sequenceFrames && flux > 0.003)
    {
        const std::size_t maxLag = std::min<std::size_t>(
            fingerprintCount - sequenceFrames, 48);
        for (std::size_t lag = 8; lag <= maxLag; ++lag)
        {
            const auto index = (fingerprintWrite + fingerprints.size() - lag) % fingerprints.size();
            double sequenceSimilarity = similarity(fingerprint, fingerprints[index]);
            for (std::size_t prior = 1; prior < sequenceFrames; ++prior)
            {
                const auto recentIndex = (fingerprintWrite + fingerprints.size() - prior)
                                       % fingerprints.size();
                const auto pastIndex = (fingerprintWrite + fingerprints.size() - lag - prior)
                                     % fingerprints.size();
                sequenceSimilarity = std::min(sequenceSimilarity,
                    similarity(fingerprints[recentIndex], fingerprints[pastIndex]));
            }
            repetition = std::max(repetition, unit((sequenceSimilarity - 0.68) / 0.32));
        }
        repetition *= unit(flux * 18.0); // stationary pads are sustain, not repetition
    }
    fingerprints[fingerprintWrite] = fingerprint;
    fingerprintWrite = (fingerprintWrite + 1) % fingerprints.size();
    fingerprintCount = std::min(fingerprintCount + 1, fingerprints.size());
    previousFingerprintRms = rms;
    previousFingerprintFlux = transient;

    latestFrame.frameIndex += 1;
    latestFrame.samplePosition = totalSamples;
    latestFrame.rmsDb = db(rms);
    latestFrame.peakDb = db(peak);
    latestFrame.onsetRateHz = onsetCount / std::max(1.0, totalSamples / settings.sampleRate);
    auto& map = latestFrame.resources;
    map[ResourceId::Activity] = activity;
    map[ResourceId::Transient] = transient;
    map[ResourceId::Sustain] = activity * (1.0 - transient);
    map[ResourceId::Low] = lowShare * activity;
    map[ResourceId::Mid] = midShare * activity;
    map[ResourceId::High] = highShare * activity;
    map[ResourceId::Silence] = 1.0 - activity;
    map[ResourceId::Sparsity] = 1.0 - density;
    map[ResourceId::Tail] = tailState;
    map[ResourceId::Repetition] = repetition;
    map[ResourceId::SpectralDominance] = std::max({lowShare, midShare, highShare}) * activity;
    map[ResourceId::DynamicContrast] = unit((peak - rms) * 2.0) * activity;
    map.sanitize();

    previousSlowEnvelope = slowEnvelope;
    previousFrameRms = rms;
    samplesInFrame = 0;
    sumPower = peak = sumTransient = 0.0;
    sumLowPower = sumMidPower = sumHighPower = activeSamples = 0.0;
}
} // namespace longland
