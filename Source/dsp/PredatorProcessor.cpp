#include "dsp/PredatorProcessor.h"
#include "core/MathSafety.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace longland
{
namespace
{
double dbToGain(const double decibels) noexcept
{
    return std::pow(10.0, decibels / 20.0);
}
}

void PredatorProcessor::prepare(const double newSampleRate, const double smoothingSeconds) noexcept
{
    sampleRateHz = std::clamp(finiteOr(newSampleRate, 48000.0), 8000.0, 384000.0);
    const auto seconds = std::clamp(finiteOr(smoothingSeconds, 0.02), 0.001, 1.0);
    smoothing = std::exp(-1.0 / (sampleRateHz * seconds));
    dcPole = std::exp(-2.0 * std::numbers::pi * 8.0 / sampleRateHz);
    buildOversamplingFilter();
    prepared = true;
    reset();
}

void PredatorProcessor::reset() noexcept
{
    upHistory.fill(0.0);
    downHistory.fill(0.0);
    dryDelay.fill(0.0);
    upWrite = downWrite = dryWrite = 0;
    previousWetInput = previousWetOutput = 0.0;
    drive = targetDrive;
    intensity = targetIntensity;
    asymmetry = targetAsymmetry;
    outputGain = targetOutputGain;
    compensation = targetCompensation;
}

void PredatorProcessor::setDriveDb(const double driveDb) noexcept
{
    targetDrive = dbToGain(std::clamp(finiteOr(driveDb), 0.0, 30.0));
    updateCompensationTarget();
}

void PredatorProcessor::setIntensity(const double newIntensity) noexcept
{
    targetIntensity = unit(newIntensity);
    updateCompensationTarget();
}

void PredatorProcessor::setAsymmetry(const double newAsymmetry) noexcept
{
    targetAsymmetry = unit(newAsymmetry);
}

void PredatorProcessor::setOutputGainDb(const double outputGainDb) noexcept
{
    targetOutputGain = dbToGain(std::clamp(finiteOr(outputGainDb), -24.0, 6.0));
}

void PredatorProcessor::updateCompensationTarget() noexcept
{
    // The saturator approaches unit peak as drive rises. This empirical taper
    // prevents a population increase from masquerading primarily as loudness.
    targetCompensation = std::pow(targetDrive, -0.38 * targetIntensity);
}

void PredatorProcessor::buildOversamplingFilter() noexcept
{
    // Blackman-windowed sinc. At the 2x rate, 0.225 cycles/sample corresponds
    // to 90% of the original Nyquist frequency and leaves a practical stop band.
    constexpr double cutoff = 0.225;
    constexpr double centre = static_cast<double>(firLength - 1) * 0.5;
    double sum = 0.0;
    for (std::size_t i = 0; i < firLength; ++i)
    {
        const auto offset = static_cast<double>(i) - centre;
        const auto sinc = std::abs(offset) < 1.0e-12
                        ? 2.0 * cutoff
                        : std::sin(2.0 * std::numbers::pi * cutoff * offset)
                            / (std::numbers::pi * offset);
        const auto phase = 2.0 * std::numbers::pi * static_cast<double>(i)
                         / static_cast<double>(firLength - 1);
        const auto window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
        fir[i] = sinc * window;
        sum += fir[i];
    }
    if (std::abs(sum) > 1.0e-12)
        for (auto& coefficient : fir)
            coefficient /= sum;
}

std::array<double, 2> PredatorProcessor::upsample(const double input) noexcept
{
    upHistory[upWrite] = input;
    upWrite = (upWrite + 1) % firLength;

    std::array<double, 2> result {};
    for (std::size_t phase = 0; phase < 2; ++phase)
    {
        for (std::size_t coefficient = phase, age = 0;
             coefficient < firLength;
             coefficient += 2, ++age)
        {
            const auto historyIndex = (upWrite + firLength - 1 - age) % firLength;
            result[phase] += fir[coefficient] * upHistory[historyIndex];
        }
        result[phase] *= 2.0;
    }
    return result;
}

void PredatorProcessor::pushDownsample(const double input) noexcept
{
    downHistory[downWrite] = input;
    downWrite = (downWrite + 1) % firLength;
}

double PredatorProcessor::downsampleOutput() const noexcept
{
    double result = 0.0;
    for (std::size_t age = 0; age < firLength; ++age)
    {
        const auto historyIndex = (downWrite + firLength - 1 - age) % firLength;
        result += fir[age] * downHistory[historyIndex];
    }
    return result;
}

double PredatorProcessor::shape(const double input) const noexcept
{
    const auto driven = std::clamp(input * drive, -60.0, 60.0);
    const auto symmetricNormaliser = std::max(std::tanh(drive), 1.0e-9);
    const auto symmetric = std::tanh(driven) / symmetricNormaliser;

    const auto bias = 0.12 + 0.38 * asymmetry;
    const auto biasTanh = std::tanh(bias);
    const auto asymmetricNormaliser = 1.0 + biasTanh;
    const auto asymmetric = (std::tanh(driven + bias) - biasTanh) / asymmetricNormaliser;
    return std::clamp(symmetric + 0.32 * asymmetry * (asymmetric - symmetric), -1.0, 1.0);
}

double PredatorProcessor::delayDry(const double input) noexcept
{
    const auto delayed = dryDelay[dryWrite];
    dryDelay[dryWrite] = input;
    dryWrite = (dryWrite + 1) % latencySamples;
    return delayed;
}

void PredatorProcessor::process(const float* input, float* output, const std::size_t samples) noexcept
{
    if (input == nullptr || output == nullptr)
        return;

    if (!prepared)
    {
        for (std::size_t i = 0; i < samples; ++i)
            output[i] = static_cast<float>(std::clamp(finiteOr(input[i]), -2.0, 2.0));
        return;
    }

    const auto oneMinusSmoothing = 1.0 - smoothing;
    for (std::size_t i = 0; i < samples; ++i)
    {
        drive = smoothing * drive + oneMinusSmoothing * targetDrive;
        intensity = smoothing * intensity + oneMinusSmoothing * targetIntensity;
        asymmetry = smoothing * asymmetry + oneMinusSmoothing * targetAsymmetry;
        outputGain = smoothing * outputGain + oneMinusSmoothing * targetOutputGain;
        compensation = smoothing * compensation + oneMinusSmoothing * targetCompensation;

        const auto clean = std::clamp(finiteOr(input[i]), -8.0, 8.0);
        const auto delayedClean = delayDry(clean);
        const auto oversampled = upsample(clean);

        // Filtering before and after the nonlinearity suppresses both interpolation
        // images and nonlinear products above the original Nyquist frequency.
        pushDownsample(shape(oversampled[0]));
        auto wet = downsampleOutput(); // even 2x phase gives exactly 15 base samples latency
        pushDownsample(shape(oversampled[1]));

        const auto dcBlocked = wet - previousWetInput + dcPole * previousWetOutput;
        previousWetInput = wet;
        previousWetOutput = std::abs(dcBlocked) < 1.0e-24 ? 0.0 : dcBlocked;

        const auto processed = previousWetOutput * compensation * outputGain;
        const auto mixed = delayedClean + intensity * (processed - delayedClean);
        output[i] = static_cast<float>(std::clamp(finiteOr(mixed), -2.0, 2.0));
    }
}
} // namespace longland
