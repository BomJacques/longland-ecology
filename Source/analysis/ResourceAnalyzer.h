#pragma once

#include "analysis/ResourceMap.h"
#include <array>
#include <cstddef>

namespace longland
{
class ResourceAnalyzer
{
public:
    struct Config
    {
        double sampleRate {48000.0};
        double updateRateHz {50.0};
        std::size_t channels {2};
    };

    void prepare(Config config) noexcept;
    void reset() noexcept;
    void processBlock(const float* const* channels, std::size_t numChannels, std::size_t numSamples) noexcept;

    const AnalysisFrame& latest() const noexcept { return latestFrame; }
    std::size_t completedFrames() const noexcept { return latestFrame.frameIndex; }

private:
    void accumulateSample(double power) noexcept;
    void finishFrame() noexcept;

    Config settings;
    std::size_t samplesPerFrame {960};
    std::size_t samplesInFrame {};
    std::size_t totalSamples {};
    double sumPower {};
    double peak {};
    double sumTransient {};
    double sumLowPower {};
    double sumMidPower {};
    double sumHighPower {};
    double activeSamples {};
    double fastEnvelope {};
    double slowEnvelope {};
    double previousSlowEnvelope {};
    std::array<double, 2> lowStates {};
    std::array<double, 2> midLowStates {};
    double tailState {};
    double previousFrameRms {};
    double previousFingerprintRms {};
    double previousFingerprintFlux {};
    std::array<std::array<double, 5>, 64> fingerprints {};
    std::size_t fingerprintWrite {};
    std::size_t fingerprintCount {};
    std::size_t onsetCount {};
    std::size_t samplesSinceOnset {};
    AnalysisFrame latestFrame;
};
} // namespace longland
