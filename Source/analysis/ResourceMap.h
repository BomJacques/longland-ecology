#pragma once

#include "core/MathSafety.h"
#include <array>
#include <cstddef>

namespace longland
{
enum class ResourceId : std::size_t
{
    Activity,
    Transient,
    Sustain,
    Low,
    Mid,
    High,
    Silence,
    Sparsity,
    Tail,
    Repetition,
    SpectralDominance,
    DynamicContrast,
    Count
};

struct ResourceMap
{
    static constexpr auto count = static_cast<std::size_t>(ResourceId::Count);
    std::array<double, count> values {};

    double& operator[](const ResourceId id) noexcept { return values[static_cast<std::size_t>(id)]; }
    double operator[](const ResourceId id) const noexcept { return values[static_cast<std::size_t>(id)]; }

    void sanitize() noexcept
    {
        for (auto& value : values)
            value = unit(value);
    }
};

struct AnalysisFrame
{
    std::size_t frameIndex {};
    std::size_t samplePosition {};
    double rmsDb {-120.0};
    double peakDb {-120.0};
    double onsetRateHz {};
    ResourceMap resources;
};
} // namespace longland

