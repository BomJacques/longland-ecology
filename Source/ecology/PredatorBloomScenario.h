#pragma once

#include "ecology/EcologyEngine.h"

namespace longland
{
struct PredatorBloomScenario
{
    static constexpr std::size_t predator = 0;
    static constexpr std::size_t bloom = 1;
    static EcologyEngine create(bool predatorAttacksBloom = true);
};
} // namespace longland

