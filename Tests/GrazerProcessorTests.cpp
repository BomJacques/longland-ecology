#include "dsp/GrazerProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

using Band = longland::GrazerProcessor::Band;

double component(const std::vector<float>& signal, const double frequency, const double sampleRate)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / sampleRate;
        real += signal[i] * std::cos(phase);
        imaginary -= signal[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(signal.size());
}

void testFixture(TestContext& test, const double frequency, const Band expected, const std::string& name)
{
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t length = 96000;
    std::vector<float> input(length);
    std::vector<float> bypass(length);
    std::vector<float> grazed(length);
    for (std::size_t sample = 0; sample < input.size(); ++sample)
        input[sample] = static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * frequency * sample / sampleRate));

    longland::GrazerProcessor processor;
    processor.prepare(sampleRate, 0.01);
    processor.process(input.data(), input.data(), bypass.data(), bypass.data(), input.size());
    test.expect(processor.targetBand() == expected, name + " fixture must select its expected band");
    const double bypassLevel = component(std::vector<float>(bypass.end() - 48000, bypass.end()), frequency, sampleRate);

    processor.reset();
    processor.setIntensity(1.0f);
    processor.process(input.data(), input.data(), grazed.data(), grazed.data(), input.size());
    test.expect(processor.targetBand() == expected, name + " fixture target must remain stable under grazing");
    const double grazedLevel = component(std::vector<float>(grazed.end() - 48000, grazed.end()), frequency, sampleRate);
    test.expect(grazedLevel < bypassLevel * 0.65, name + " dominance must be attenuated materially");
    test.expect(grazedLevel > bypassLevel * 0.25, name + " attenuation must remain musically bounded");
}

void testTrueBypassAndStereoCoherence(TestContext& test)
{
    constexpr std::size_t length = 4096;
    std::vector<float> left(length);
    std::vector<float> right(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        left[i] = static_cast<float>(0.3 * std::sin(0.037 * i));
        right[i] = static_cast<float>(0.2 * std::cos(0.071 * i));
    }
    const auto originalLeft = left;
    const auto originalRight = right;
    longland::GrazerProcessor processor;
    processor.prepare(48000.0);
    processor.process(left.data(), right.data(), left.data(), right.data(), length);
    float error = 0.0f;
    for (std::size_t i = 0; i < length; ++i)
        error = std::max({error, std::abs(left[i] - originalLeft[i]), std::abs(right[i] - originalRight[i])});
    test.expect(error < 2.0e-6f, "zero intensity must be an in-place, latency-free stereo bypass");
}

void testSmoothingAndReset(TestContext& test)
{
    constexpr double sampleRate = 48000.0;
    std::vector<float> signal(48000);
    for (std::size_t i = 0; i < signal.size(); ++i)
        signal[i] = static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * 7000.0 * i / sampleRate));

    longland::GrazerProcessor processor;
    processor.prepare(sampleRate, 0.05);
    processor.process(signal.data(), signal.data(), signal.data(), signal.data(), signal.size());
    test.expect(processor.targetBand() == Band::high, "bright signal must establish a high-band target");
    processor.setIntensity(1.0f);
    const float before = processor.currentBandGain(Band::high);
    std::array<float, 1> one {0.1f};
    processor.process(one.data(), one.data(), one.data(), one.data(), one.size());
    const float first = processor.currentBandGain(Band::high);
    test.expect(first < before && first > 0.99f, "intensity jump must begin with a smooth gain step");

    processor.process(signal.data(), signal.data(), signal.data(), signal.data(), signal.size());
    test.expect(processor.currentBandGain(Band::high) < 0.40f, "smoothed attenuation must eventually reach its target");
    processor.reset();
    test.expect(processor.targetBand() == Band::none, "reset must clear the detected target");
    test.expect(processor.currentBandGain(Band::high) == 1.0f, "reset must clear band attenuation");

    std::array<float, 32> silence {};
    processor.process(silence.data(), silence.data(), silence.data(), silence.data(), silence.size());
    test.expect(std::all_of(silence.begin(), silence.end(), [](const float sample) { return sample == 0.0f; }),
                "reset processor must return exact silence for silence");
}

void testHostileInput(TestContext& test)
{
    longland::GrazerProcessor processor;
    processor.prepare(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity());
    processor.setIntensity(std::numeric_limits<float>::quiet_NaN());
    std::vector<float> left(8192, 1.0e30f);
    std::vector<float> right(8192, -1.0e30f);
    left[10] = std::numeric_limits<float>::infinity();
    right[20] = std::numeric_limits<float>::quiet_NaN();
    processor.process(left.data(), right.data(), left.data(), right.data(), left.size());
    test.expect(std::all_of(left.begin(), left.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 16.0f;
                }), "left output must remain finite and bounded for hostile input");
    test.expect(std::all_of(right.begin(), right.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 16.0f;
                }), "right output must remain finite and bounded for hostile input");

    const auto telemetry = processor.telemetry();
    test.expect(std::all_of(telemetry.bandLevels.begin(), telemetry.bandLevels.end(), [](const float level) {
                    return std::isfinite(level) && level >= 0.0f;
                }), "telemetry must remain finite after hostile input");
}
} // namespace

int main()
{
    TestContext test;
    testFixture(test, 80.0, Band::low, "low");
    testFixture(test, 800.0, Band::mid, "mid");
    testFixture(test, 8000.0, Band::high, "bright");
    testTrueBypassAndStereoCoherence(test);
    testSmoothingAndReset(test);
    testHostileInput(test);
    if (test.failures == 0)
    {
        std::cout << "PASS: Grazer adaptive low/mid/high attenuation, telemetry, smoothing, reset and safety\n";
        return 0;
    }
    return 1;
}

