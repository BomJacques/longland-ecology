#include "dsp/ScavengerProcessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
std::atomic<bool> countAllocations {false};
std::atomic<std::size_t> allocationCount {0};

void countAllocation() noexcept
{
    if (countAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
}

struct TestContext
{
    int failures {};

    void expect(const bool condition, const char* const message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

constexpr std::size_t blockSize = 127;
using Block = std::array<float, blockSize>;

struct ImpulseMetrics
{
    double totalEnergy {};
    double firstWindowEnergy {};
    double middleWindowEnergy {};
    double lateWindowEnergy {};
    float peak {};
};

ImpulseMetrics renderImpulse(longland::ScavengerProcessor& processor,
                             const std::size_t totalSamples,
                             const bool stereoImpulse = false)
{
    Block left {};
    Block right {};
    ImpulseMetrics metrics;
    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = std::min(blockSize, totalSamples - offset);
        left.fill(0.0f);
        right.fill(0.0f);
        if (offset == 0)
        {
            left[0] = 1.0f;
            right[0] = stereoImpulse ? 1.0f : 0.0f;
        }
        processor.process(left.data(), right.data(), left.data(), right.data(), count);
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const double energy = static_cast<double>(left[sample]) * left[sample]
                                + static_cast<double>(right[sample]) * right[sample];
            const auto position = offset + sample;
            metrics.totalEnergy += energy;
            metrics.peak = std::max({metrics.peak, std::abs(left[sample]), std::abs(right[sample])});
            if (position < 4u * 48000u)
                metrics.firstWindowEnergy += energy;
            else if (position >= 8u * 48000u && position < 12u * 48000u)
                metrics.middleWindowEnergy += energy;
            else if (position >= 20u * 48000u)
                metrics.lateWindowEnergy += energy;
        }
    }
    return metrics;
}
} // namespace

void* operator new(const std::size_t size)
{
    countAllocation();
    if (auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc {};
}

void* operator new[](const std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    countAllocation();
#if defined(_MSC_VER)
    if (auto* memory = _aligned_malloc(size, static_cast<std::size_t>(alignment)))
        return memory;
#else
    const auto align = static_cast<std::size_t>(alignment);
    const auto roundedSize = (size + align - 1u) / align * align;
    if (auto* memory = std::aligned_alloc(align, roundedSize))
        return memory;
#endif
    throw std::bad_alloc {};
}

void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void operator delete(void* memory, std::size_t, const std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete[](void* memory, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete[](void* memory, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

int main()
{
    TestContext test;
    Block left {};
    Block right {};

    longland::ScavengerProcessor scavenger;
    left.fill(1.0f);
    right.fill(-1.0f);
    scavenger.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    for (std::size_t sample = 0; sample < blockSize; ++sample)
        test.expect(left[sample] == 0.0f && right[sample] == 0.0f,
                    "unprepared wet-only branch must be silent");

    scavenger.prepare(48000.0);

    // A half-sample delay must split the first echo evenly between adjacent
    // samples. This proves both causality and interpolation.
    constexpr double fractionalDelaySamples = 3864.5;
    scavenger.setIntensity(1.0);
    scavenger.setFeedback(0.0);
    scavenger.setDelayTimeMs(1000.0 * fractionalDelaySamples / 48000.0);
    scavenger.setDiffusion(0.0);
    scavenger.reset();
    float atFirst = 0.0f;
    float atSecond = 0.0f;
    float preDelayPeak = 0.0f;
    for (std::size_t offset = 0; offset < 4200; offset += blockSize)
    {
        const auto count = std::min<std::size_t>(blockSize, 4200 - offset);
        left.fill(0.0f);
        right.fill(0.0f);
        if (offset == 0)
            left[0] = 1.0f;
        scavenger.process(left.data(), right.data(), left.data(), right.data(), count);
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            const auto position = offset + sample;
            if (position < 3864)
                preDelayPeak = std::max(preDelayPeak, std::abs(left[sample]));
            if (position == 3864)
                atFirst = left[sample];
            if (position == 3865)
                atSecond = left[sample];
        }
    }
    test.expect(preDelayPeak < 1.0e-7f, "delay return must be causal and silent before its read time");
    test.expect(std::abs(atFirst - 0.45f) < 2.0e-5f
                    && std::abs(atSecond - 0.45f) < 2.0e-5f,
                "half-sample interpolation must split the 0.90 send over adjacent samples");

    // Intensity is intentionally one send scaling, rather than being squared
    // inside this processor.
    scavenger.setIntensity(0.5);
    scavenger.setFeedback(0.0);
    scavenger.setDelayTimeMs(40.0);
    scavenger.setDiffusion(0.0);
    scavenger.reset();
    const auto moderateImpulse = renderImpulse(scavenger, 4800);
    scavenger.setIntensity(1.0);
    scavenger.reset();
    const auto fullImpulse = renderImpulse(scavenger, 4800);
    const double intensityRatio = moderateImpulse.peak / std::max(1.0e-12f, fullImpulse.peak);
    test.expect(std::abs(fullImpulse.peak - 0.90f) < 2.0e-5f,
                "full-intensity first echo must use the documented 0.90 send gain");
    test.expect(std::abs(intensityRatio - 0.5) < 2.0e-5,
                "Scavenger impulse gain must scale linearly once with intensity");

    // Worst production feedback must remain below the signal bound and decay.
    scavenger.setIntensity(1.0);
    scavenger.setFeedback(0.82);
    scavenger.setDelayTimeMs(420.0);
    scavenger.setDiffusion(0.70);
    scavenger.reset();
    const auto tail = renderImpulse(scavenger, 24u * 48000u, true);
    test.expect(std::isfinite(tail.totalEnergy) && tail.totalEnergy > 0.1,
                "maximum production feedback must create a finite audible tail");
    test.expect(tail.peak < 1.25f && tail.totalEnergy < 30.0,
                "maximum production feedback must retain gain margin and bounded energy");
    test.expect(tail.middleWindowEnergy < 0.06 * tail.firstWindowEnergy,
                "feedback energy must decay materially by the middle window");
    test.expect(tail.lateWindowEnergy < 1.0e-4 * tail.firstWindowEnergy,
                "feedback tail must approach silence, not a clipped limit cycle");

    // Measure sustained audibility at a normal population intensity.
    scavenger.setIntensity(0.5);
    scavenger.setFeedback(0.58);
    scavenger.setDelayTimeMs(160.0);
    scavenger.setDiffusion(0.45);
    scavenger.reset();
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    double phase = 0.0;
    for (std::size_t offset = 0; offset < 4u * 48000u; offset += blockSize)
    {
        const auto count = std::min<std::size_t>(blockSize, 4u * 48000u - offset);
        for (std::size_t sample = 0; sample < count; ++sample)
        {
            left[sample] = right[sample] = static_cast<float>(0.2 * std::sin(phase));
            phase += 2.0 * 3.14159265358979323846 * 233.0 / 48000.0;
            if (offset + sample >= 3u * 48000u)
                inputEnergy += static_cast<double>(left[sample]) * left[sample]
                             + static_cast<double>(right[sample]) * right[sample];
        }
        scavenger.process(left.data(), right.data(), left.data(), right.data(), count);
        if (offset + count > 3u * 48000u)
        {
            const auto begin = offset < 3u * 48000u ? 3u * 48000u - offset : 0u;
            for (std::size_t sample = begin; sample < count; ++sample)
                outputEnergy += static_cast<double>(left[sample]) * left[sample]
                              + static_cast<double>(right[sample]) * right[sample];
        }
    }
    const double steadyRmsRatio = std::sqrt(outputEnergy / std::max(inputEnergy, 1.0e-20));
    test.expect(steadyRmsRatio > 0.25 && steadyRmsRatio < 2.0,
                "normal-population return must be audible without excessive gain");

    scavenger.reset();
    left.fill(0.0f);
    right.fill(0.0f);
    scavenger.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    for (std::size_t sample = 0; sample < blockSize; ++sample)
        test.expect(left[sample] == 0.0f && right[sample] == 0.0f,
                    "reset must clear every sample of the Scavenger tail");

    scavenger.setIntensity(std::numeric_limits<double>::quiet_NaN());
    scavenger.setFeedback(std::numeric_limits<double>::infinity());
    scavenger.setDelayTimeMs(-std::numeric_limits<double>::infinity());
    scavenger.setDiffusion(std::numeric_limits<double>::quiet_NaN());
    left.fill(std::numeric_limits<float>::quiet_NaN());
    right.fill(std::numeric_limits<float>::infinity());
    scavenger.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    for (std::size_t sample = 0; sample < blockSize; ++sample)
        test.expect(std::isfinite(left[sample]) && std::isfinite(right[sample]),
                    "invalid values must never poison Scavenger output");

    // A large delay-time jump is smoothed: no single-sample discontinuity.
    scavenger.setIntensity(1.0);
    scavenger.setFeedback(0.35);
    scavenger.setDiffusion(0.3);
    scavenger.setDelayTimeMs(35.0);
    scavenger.reset();
    phase = 0.0;
    float previous = 0.0f;
    float largestStep = 0.0f;
    for (std::size_t block = 0; block < 500; ++block)
    {
        if (block == 250)
            scavenger.setDelayTimeMs(900.0);
        for (std::size_t sample = 0; sample < blockSize; ++sample)
        {
            left[sample] = right[sample] = static_cast<float>(0.2 * std::sin(phase));
            phase += 2.0 * 3.14159265358979323846 * 233.0 / 48000.0;
        }
        scavenger.process(left.data(), right.data(), left.data(), right.data(), blockSize);
        for (std::size_t sample = 0; sample < blockSize; ++sample)
        {
            largestStep = std::max(largestStep, std::abs(left[sample] - previous));
            previous = left[sample];
        }
    }
    test.expect(largestStep < 0.15f, "a full delay-time jump must remain click-free");

    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t block = 0; block < 100; ++block)
        scavenger.process(left.data(), right.data(), left.data(), right.data(), blockSize);
    countAllocations.store(false, std::memory_order_relaxed);
    test.expect(allocationCount.load(std::memory_order_relaxed) == 0,
                "process must not allocate after prepare");

    constexpr double recommendedPluginMixWeight = 0.42;
    const double mixedModerateFirstEcho = recommendedPluginMixWeight * 0.5 * moderateImpulse.peak;
    std::cout << "Scavenger metrics: full_impulse_peak=" << fullImpulse.peak
              << " moderate_impulse_peak=" << moderateImpulse.peak
              << " intensity_ratio=" << intensityRatio
              << " steady_wet_dry_rms_ratio=" << steadyRmsRatio
              << " max_feedback_peak=" << tail.peak
              << " max_feedback_total_energy=" << tail.totalEnergy
              << " middle_to_early_energy="
              << tail.middleWindowEnergy / std::max(tail.firstWindowEnergy, 1.0e-20)
              << " late_to_early_energy="
              << tail.lateWindowEnergy / std::max(tail.firstWindowEnergy, 1.0e-20)
              << " recommended_plugin_mix_weight=" << recommendedPluginMixWeight
              << " moderate_first_echo_after_species_weight=" << mixedModerateFirstEcho
              << '\n';

    if (test.failures != 0)
    {
        std::cerr << "Scavenger tests failed with " << test.failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
