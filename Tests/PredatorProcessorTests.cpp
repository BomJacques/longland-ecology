#include "dsp/PredatorProcessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace
{
struct TestContext
{
    int failures {};
    void expect(const bool condition, const std::string& message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void testAlignedBypass(TestContext& test)
{
    longland::PredatorProcessor processor;
    processor.prepare(48000.0);
    std::vector<float> input(64, 0.0f);
    std::vector<float> output(64, 0.0f);
    input[0] = 1.0f;
    processor.process(input.data(), output.data(), output.size());
    test.expect(std::abs(output[longland::PredatorProcessor::latencySamples] - 1.0f) < 1.0e-6f,
                "zero intensity must preserve the signal at the documented latency");
    for (std::size_t i = 0; i < output.size(); ++i)
        if (i != longland::PredatorProcessor::latencySamples)
            test.expect(std::abs(output[i]) < 1.0e-6f, "bypass impulse must not smear");
}

void testBoundedAndFinite(TestContext& test)
{
    longland::PredatorProcessor processor;
    processor.prepare(192000.0, 0.001);
    processor.setDriveDb(30.0);
    processor.setIntensity(1.0);
    processor.setAsymmetry(1.0);
    processor.setOutputGainDb(6.0);
    std::vector<float> input(32768, 1000.0f);
    std::vector<float> output(input.size());
    input[100] = std::numeric_limits<float>::infinity();
    input[200] = std::numeric_limits<float>::quiet_NaN();
    processor.process(input.data(), output.data(), output.size());
    test.expect(std::all_of(output.begin(), output.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 2.0f;
                }), "hostile input and maximum controls must remain finite and bounded");
}

double component(const std::vector<float>& signal, const double frequency, const double sampleRate)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / sampleRate;
        real += signal[i] * std::cos(phase);
        imaginary -= signal[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(signal.size());
}

void testCreatesHarmonics(TestContext& test)
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 1000.0;
    longland::PredatorProcessor processor;
    processor.prepare(sampleRate, 0.001);
    processor.setDriveDb(24.0);
    processor.setIntensity(1.0);
    processor.setAsymmetry(0.0);
    std::vector<float> input(96000);
    std::vector<float> output(input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(0.35 * std::sin(2.0 * std::numbers::pi * frequency * i / sampleRate));
    processor.process(input.data(), output.data(), output.size());
    std::vector<float> tail(output.end() - 48000, output.end());
    const auto fundamental = component(tail, frequency, sampleRate);
    const auto third = component(tail, 3.0 * frequency, sampleRate);
    test.expect(fundamental > 0.1, "processed signal must retain its fundamental");
    test.expect(third > fundamental * 0.08, "Predator must create material odd harmonics");
}

void testSmoothAutomationAndDcBlocking(TestContext& test)
{
    constexpr double sampleRate = 48000.0;
    longland::PredatorProcessor processor;
    processor.prepare(sampleRate, 0.02);
    std::vector<float> constant(12000, 0.25f);
    std::vector<float> before(constant.size());
    processor.process(constant.data(), before.data(), before.size());
    const auto preceding = before.back();

    processor.setDriveDb(30.0);
    processor.setIntensity(1.0);
    processor.setAsymmetry(1.0);
    std::vector<float> after(48000, 0.25f);
    std::vector<float> output(after.size());
    processor.process(after.data(), output.data(), output.size());
    test.expect(std::abs(output.front() - preceding) < 0.01f,
                "a maximum automation jump must be smoothed at the audio output");

    std::vector<float> silence(48000, 0.0f);
    processor.process(silence.data(), silence.data(), silence.size());
    double lateMean = 0.0;
    for (std::size_t i = 24000; i < silence.size(); ++i)
        lateMean += silence[i];
    lateMean /= 24000.0;
    test.expect(std::abs(lateMean) < 1.0e-5, "asymmetric saturation DC must decay to silence");
}
} // namespace

int main()
{
    TestContext test;
    testAlignedBypass(test);
    testBoundedAndFinite(test);
    testCreatesHarmonics(test);
    testSmoothAutomationAndDcBlocking(test);
    if (test.failures == 0)
    {
        std::cout << "PASS: Predator oversampled saturation, safety, smoothing, harmonics and DC rejection\n";
        return 0;
    }
    return 1;
}
