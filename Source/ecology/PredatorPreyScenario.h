#pragma once

#include "ecology/EcologyEngine.h"

namespace longland
{
struct PredatorPreyScenario
{
    static constexpr std::size_t prey = 0;
    static constexpr std::size_t predator = 1;
    static EcologyEngine create();
};
} // namespace longland

