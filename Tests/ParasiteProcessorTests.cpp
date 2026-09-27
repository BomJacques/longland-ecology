#include "dsp/ParasiteProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <string>
#include <vector>

namespace
{
std::atomic<std::size_t> allocationCount {};
}

void* operator new(const std::size_t size)
{
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc {};
}

void* operator new[](const std::size_t size)
{
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc {};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

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

double component(const std::vector<float>& signal,
                 const double frequency,
                 const double sampleRate,
                 const std::size_t start)
{
    double real = 0.0;
    double imaginary = 0.0;
    const auto count = signal.size() - start;
    for (std::size_t i = start; i < signal.size(); ++i)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / sampleRate;
        real += signal[i] * std::cos(phase);
        imaginary -= signal[i] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(count);
}

void testHostBypassAndNoAllocation(TestContext& test)
{
    constexpr std::size_t length = 32768;
    std::vector<float> left(length);
    std::vector<float> right(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        left[i] = static_cast<float>(0.31 * std::sin(0.017 * static_cast<double>(i)));
        right[i] = static_cast<float>(0.27 * std::cos(0.023 * static_cast<double>(i)));
    }
    const auto originalLeft = left;
    const auto originalRight = right;

    longland::ParasiteProcessor parasite;
    parasite.setIntensity(0.0f);
    parasite.prepare(48000.0);
    const auto before = allocationCount.load(std::memory_order_relaxed);
    parasite.process(left.data(), right.data(), left.data(), right.data(), length);
    const auto after = allocationCount.load(std::memory_order_relaxed);

    float maximumError = 0.0f;
    for (std::size_t i = 0; i < length; ++i)
        maximumError = std::max({maximumError,
                                 std::abs(left[i] - originalLeft[i]),
                                 std::abs(right[i] - originalRight[i])});
    test.expect(maximumError == 0.0f,
                "zero Parasite intensity must exactly bypass its host branch");
    test.expect(after == before,
                "process must perform no heap allocation after prepare");
}

void testAudibleMicroshift(TestContext& test)
{
    constexpr double sampleRate = 48000.0;
    constexpr double sourceFrequency = 1000.0;
    constexpr float cents = 18.0f;
    constexpr std::size_t length = 192000;
    std::vector<float> input(length);
    std::vector<float> left(length);
    std::vector<float> right(length);
    for (std::size_t i = 0; i < length; ++i)
        input[i] = static_cast<float>(0.35 * std::sin(2.0 * std::numbers::pi
                                      * sourceFrequency * static_cast<double>(i) / sampleRate));

    longland::ParasiteProcessor parasite;
    parasite.setIntensity(1.0f);
    parasite.setDetuneCents(cents);
    parasite.setSmear(0.0f);
    parasite.setFeedback(0.0f);
    parasite.prepare(sampleRate);
    parasite.process(input.data(), input.data(), left.data(), right.data(), length);

    const double ratio = std::exp2(static_cast<double>(cents) / 1200.0);
    const double leftShifted = component(left, sourceFrequency * ratio, sampleRate, length / 2);
    const double leftOriginal = component(left, sourceFrequency, sampleRate, length / 2);
    const double rightShifted = component(right, sourceFrequency / ratio, sampleRate, length / 2);
    const double rightOriginal = component(right, sourceFrequency, sampleRate, length / 2);
    test.expect(leftShifted > leftOriginal * 2.0 && leftShifted > 0.08,
                "left host branch must contain a non-vacuous upward microshift");
    test.expect(rightShifted > rightOriginal * 2.0 && rightShifted > 0.08,
                "right host branch must contain a non-vacuous downward microshift");

    double stereoDifference = 0.0;
    double signalEnergy = 0.0;
    for (std::size_t i = length / 2; i < length; ++i)
    {
        const double difference = left[i] - right[i];
        stereoDifference += difference * difference;
        signalEnergy += 0.5 * (left[i] * left[i] + right[i] * right[i]);
    }
    test.expect(stereoDifference > signalEnergy * 0.25,
                "opposing microshifts must create audible stereo phase differentiation");
}

void testSmoothingAndReset(TestContext& test)
{
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 997.0;
    std::vector<float> host(48001);
    for (std::size_t i = 0; i < host.size(); ++i)
        host[i] = static_cast<float>(0.35 * std::sin(2.0 * std::numbers::pi
                                    * frequency * static_cast<double>(i) / sampleRate));
    std::vector<float> output(host.size());
    longland::ParasiteProcessor parasite;
    parasite.setIntensity(0.0f);
    parasite.prepare(sampleRate);
    parasite.process(host.data(), host.data(), output.data(), output.data(), host.size() - 1);
    parasite.setIntensity(1.0f);
    parasite.process(host.data() + host.size() - 1, host.data() + host.size() - 1,
                     output.data() + output.size() - 1, output.data() + output.size() - 1, 1);
    test.expect(std::abs(output.back() - host.back()) < 0.001f,
                "a maximum intensity jump must remain near the continuous host on its first sample");
    test.expect(parasite.currentIntensity() > 0.0f && parasite.currentIntensity() < 0.01f,
                "intensity must ramp instead of jumping to its target");

    std::vector<float> impulse(8192, 0.0f);
    impulse[0] = 1.0f;
    parasite.setIntensity(1.0f);
    parasite.setFeedback(0.32f);
    parasite.reset();
    parasite.process(impulse.data(), impulse.data(), impulse.data(), impulse.data(), impulse.size());
    parasite.reset();
    std::fill(impulse.begin(), impulse.end(), 0.0f);
    parasite.process(impulse.data(), impulse.data(), impulse.data(), impulse.data(), impulse.size());
    test.expect(std::all_of(impulse.begin(), impulse.end(), [](const float sample) { return sample == 0.0f; }),
                "reset must clear all delay and feedback memory");
}

void testHostileBoundsAndFeedbackDecay(TestContext& test)
{
    constexpr std::size_t length = 48000 * 8;
    std::vector<float> left(length, 1.0e30f);
    std::vector<float> right(length, -1.0e30f);
    left[13] = std::numeric_limits<float>::infinity();
    right[29] = std::numeric_limits<float>::quiet_NaN();
    longland::ParasiteProcessor parasite;
    parasite.setIntensity(1.0f);
    parasite.setFeedback(std::numeric_limits<float>::infinity());
    parasite.setSmear(std::numeric_limits<float>::quiet_NaN());
    parasite.setDetuneCents(std::numeric_limits<float>::infinity());
    parasite.prepare(std::numeric_limits<double>::quiet_NaN());
    parasite.process(left.data(), right.data(), left.data(), right.data(), length);
    test.expect(std::all_of(left.begin(), left.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 12.0f;
                }), "hostile left input and controls must remain finite and bounded");
    test.expect(std::all_of(right.begin(), right.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 12.0f;
                }), "hostile right input and controls must remain finite and bounded");

    parasite.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    left[0] = right[0] = 1.0f;
    parasite.setFeedback(0.32f);
    parasite.process(left.data(), right.data(), left.data(), right.data(), length);
    double earlyEnergy = 0.0;
    double lateEnergy = 0.0;
    for (std::size_t i = 0; i < 48000; ++i)
        earlyEnergy += left[i] * left[i] + right[i] * right[i];
    for (std::size_t i = length - 48000; i < length; ++i)
        lateEnergy += left[i] * left[i] + right[i] * right[i];
    test.expect(earlyEnergy > 1.0e-6,
                "full-intensity Parasite must produce an audible delayed impulse response");
    test.expect(lateEnergy < earlyEnergy * 1.0e-4,
                "maximum feedback must decay rather than run away");
}
} // namespace

int main()
{
    TestContext test;
    testHostBypassAndNoAllocation(test);
    testAudibleMicroshift(test);
    testSmoothingAndReset(test);
    testHostileBoundsAndFeedbackDecay(test);
    if (test.failures == 0)
    {
        std::cout << "PASS: Parasite host bypass, microshift, stereo smear, smoothing, reset, allocation and safety\n";
        return 0;
    }
    return 1;
}
