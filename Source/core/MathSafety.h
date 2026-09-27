#pragma once

#include <algorithm>
#include <cmath>

namespace longland
{
inline double finiteOr(const double value, const double fallback = 0.0) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

inline double unit(const double value) noexcept
{
    return std::clamp(finiteOr(value), 0.0, 1.0);
}

inline double smoothStep(const double value) noexcept
{
    const auto x = unit(value);
    return x * x * (3.0 - 2.0 * x);
}

inline double smoothingCoefficient(const double seconds, const double timeConstant) noexcept
{
    if (seconds <= 0.0 || timeConstant <= 0.0)
        return 0.0;
    return std::exp(-std::min(seconds, 1.0) / timeConstant);
}
} // namespace longland

