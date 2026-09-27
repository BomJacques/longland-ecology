#pragma once

#include <array>
#include <cstddef>

namespace longland
{
// One channel of Predator saturation. Create one instance per audio channel.
// The fixed 15-sample latency aligns the dry path with the 2x FIR oversampler.
class PredatorProcessor
{
public:
    static constexpr std::size_t latencySamples = 15;

    void prepare(double sampleRate, double smoothingSeconds = 0.02) noexcept;
    void reset() noexcept;

    // These controls describe DSP behaviour only. Population/health mapping belongs
    // to the ecology/routing layer.
    void setDriveDb(double driveDb) noexcept;          // 0 .. 30 dB
    void setIntensity(double intensity) noexcept;      // 0 .. 1 (aligned dry/wet)
    void setAsymmetry(double asymmetry) noexcept;      // 0 .. 1
    void setOutputGainDb(double outputGainDb) noexcept; // -24 .. +6 dB

    void process(const float* input, float* output, std::size_t samples) noexcept;

    double currentIntensity() const noexcept { return intensity; }
    double currentDrive() const noexcept { return drive; }

private:
    static constexpr std::size_t firLength = 31;

    void buildOversamplingFilter() noexcept;
    void updateCompensationTarget() noexcept;
    std::array<double, 2> upsample(double input) noexcept;
    void pushDownsample(double input) noexcept;
    double downsampleOutput() const noexcept;
    double shape(double input) const noexcept;
    double delayDry(double input) noexcept;

    std::array<double, firLength> fir {};
    std::array<double, firLength> upHistory {};
    std::array<double, firLength> downHistory {};
    std::array<double, latencySamples> dryDelay {};
    std::size_t upWrite {};
    std::size_t downWrite {};
    std::size_t dryWrite {};

    double sampleRateHz {48000.0};
    double smoothing {0.999};
    double dcPole {0.999};
    double previousWetInput {};
    double previousWetOutput {};

    double drive {1.0};
    double targetDrive {1.0};
    double intensity {};
    double targetIntensity {};
    double asymmetry {0.18};
    double targetAsymmetry {0.18};
    double outputGain {1.0};
    double targetOutputGain {1.0};
    double compensation {1.0};
    double targetCompensation {1.0};
    bool prepared {};
};
} // namespace longland
