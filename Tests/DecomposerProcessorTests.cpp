#include "dsp/DecomposerProcessor.h"

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

void fillSource(std::vector<float>& left, std::vector<float>& right)
{
    constexpr double sampleRate = 48000.0;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const double time = static_cast<double>(i) / sampleRate;
        const double pulse = (i % 7200) < 1700 ? 1.0 : 0.18;
        left[i] = static_cast<float>(pulse * (0.24 * std::sin(2.0 * std::numbers::pi * 233.0 * time)
                                            + 0.11 * std::sin(2.0 * std::numbers::pi * 887.0 * time)));
        right[i] = static_cast<float>(pulse * (0.22 * std::sin(2.0 * std::numbers::pi * 311.0 * time)
                                             + 0.09 * std::cos(2.0 * std::numbers::pi * 631.0 * time)));
    }
}

void configure(longland::DecomposerProcessor& processor)
{
    processor.setRandomSeed(0x12345678u);
    processor.setIntensity(1.0f);
    processor.setGrainSizeMilliseconds(62.0f);
    processor.setDensity(0.72f);
    processor.setScatter(0.86f);
    processor.prepare(48000.0);
}

void testExactBypassAndNoAllocation(TestContext& test)
{
    constexpr std::size_t length = 96000;
    std::vector<float> left(length), right(length);
    fillSource(left, right);
    const auto originalLeft = left;
    const auto originalRight = right;

    longland::DecomposerProcessor processor;
    processor.setIntensity(0.0f);
    processor.prepare(48000.0);
    const auto before = allocationCount.load(std::memory_order_relaxed);
    processor.process(left.data(), right.data(), left.data(), right.data(), length);
    const auto after = allocationCount.load(std::memory_order_relaxed);

    float error = 0.0f;
    for (std::size_t i = 0; i < length; ++i)
        error = std::max({error, std::abs(left[i] - originalLeft[i]),
                          std::abs(right[i] - originalRight[i])});
    test.expect(error == 0.0f, "zero Decomposer intensity must be an exact stereo bypass");
    test.expect(after == before, "process must not allocate after prepare");
    test.expect(processor.activeGrainCount() <= longland::DecomposerProcessor::maximumGrains,
                "grain population must remain statically bounded");
}

void testAudibleDeterministicDecomposition(TestContext& test)
{
    constexpr std::size_t length = 240000;
    std::vector<float> inputLeft(length), inputRight(length);
    std::vector<float> firstLeft(length), firstRight(length), secondLeft(length), secondRight(length);
    fillSource(inputLeft, inputRight);

    longland::DecomposerProcessor first;
    longland::DecomposerProcessor second;
    configure(first);
    configure(second);
    first.process(inputLeft.data(), inputRight.data(), firstLeft.data(), firstRight.data(), length);
    second.process(inputLeft.data(), inputRight.data(), secondLeft.data(), secondRight.data(), length);

    double differenceEnergy = 0.0;
    double outputEnergy = 0.0;
    double stereoDifference = 0.0;
    float maximumRepeatError = 0.0f;
    float maximumStep = 0.0f;
    for (std::size_t i = 1; i < length; ++i)
    {
        const double dl = firstLeft[i] - inputLeft[i];
        const double dr = firstRight[i] - inputRight[i];
        differenceEnergy += dl * dl + dr * dr;
        outputEnergy += firstLeft[i] * firstLeft[i] + firstRight[i] * firstRight[i];
        const double stereo = firstLeft[i] - firstRight[i];
        stereoDifference += stereo * stereo;
        maximumRepeatError = std::max({maximumRepeatError,
            std::abs(firstLeft[i] - secondLeft[i]), std::abs(firstRight[i] - secondRight[i])});
        maximumStep = std::max({maximumStep, std::abs(firstLeft[i] - firstLeft[i - 1]),
                                std::abs(firstRight[i] - firstRight[i - 1])});
    }
    test.expect(differenceEnergy > outputEnergy * 0.18,
                "full Decomposer intensity must create a non-vacuous granular transformation");
    test.expect(stereoDifference > outputEnergy * 0.08,
                "scattered grains must preserve or create meaningful stereo structure");
    test.expect(maximumRepeatError == 0.0f,
                "a fixed seed and identical controls must render sample-identically");
    test.expect(maximumStep < 0.32f,
                "Hann grain envelopes and smoothed controls must prevent discontinuous clicks");
}

void testSmoothingResetAndSafety(TestContext& test)
{
    constexpr std::size_t length = 48000 * 5;
    std::vector<float> left(length, 1.0e30f), right(length, -1.0e30f);
    left[17] = std::numeric_limits<float>::infinity();
    right[31] = std::numeric_limits<float>::quiet_NaN();

    longland::DecomposerProcessor processor;
    processor.setIntensity(0.0f);
    processor.prepare(std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity());
    processor.setGrainSizeMilliseconds(std::numeric_limits<float>::infinity());
    processor.setDensity(std::numeric_limits<float>::quiet_NaN());
    processor.setScatter(std::numeric_limits<float>::infinity());
    processor.setIntensity(1.0f);
    processor.process(left.data(), right.data(), left.data(), right.data(), 1);
    test.expect(processor.currentIntensity() > 0.0f && processor.currentIntensity() < 0.01f,
                "intensity must ramp rather than jump after automation");
    processor.process(left.data() + 1, right.data() + 1,
                      left.data() + 1, right.data() + 1, length - 1);
    test.expect(std::all_of(left.begin(), left.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 12.0f;
                }), "hostile left input and controls must stay finite and bounded");
    test.expect(std::all_of(right.begin(), right.end(), [](const float sample) {
                    return std::isfinite(sample) && std::abs(sample) <= 12.0f;
                }), "hostile right input and controls must stay finite and bounded");

    processor.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    processor.process(left.data(), right.data(), left.data(), right.data(), length);
    test.expect(std::all_of(left.begin(), left.end(), [](const float sample) { return sample == 0.0f; })
             && std::all_of(right.begin(), right.end(), [](const float sample) { return sample == 0.0f; }),
                "reset must clear capture memory and every active grain");
    test.expect(processor.activeGrainCount() <= longland::DecomposerProcessor::maximumGrains,
                "even hostile operation must not exceed the fixed grain pool");
}
} // namespace

int main()
{
    TestContext test;
    testExactBypassAndNoAllocation(test);
    testAudibleDeterministicDecomposition(test);
    testSmoothingResetAndSafety(test);
    if (test.failures == 0)
    {
        std::cout << "PASS: Decomposer bypass, granular audibility, deterministic stereo, smoothing, reset, allocation and safety\n";
        return 0;
    }
    return 1;
}
