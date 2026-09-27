#pragma once

#include "analysis/ResourceMap.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace longland
{
constexpr std::size_t maxSpecies = 8;
constexpr std::size_t maxEdges = 64;

enum class RelationshipType { Feeds, Supports, Attacks, Competes, Parasitises };

struct SpeciesTraits
{
    std::array<double, ResourceMap::count> preferences {};
    double hunger {1.0};
    double birthRate {0.8};
    double deathRate {0.2};
    double resourceEfficiency {1.0};
    double aggression {1.0};
    double resilience {0.5};
    double recoveryRate {0.002};
    double carryingCapacity {1.0};
    double healthSpeed {1.5};
};

struct SpeciesState
{
    std::string_view name;
    double population {0.05};
    double health {0.5};
    double foodLevel {};
    double growthPerSecond {};
    double deathPerSecond {};
    double pressure {};
    double dspIntensity {};
    double ageSeconds {};
};

struct RelationshipEdge
{
    std::size_t from {};
    std::size_t to {};
    RelationshipType type {RelationshipType::Feeds};
    double strength {1.0};
    double currentInfluence {};
};

struct GlobalControls
{
    double resources {0.75};
    double stability {0.5};
    double populationSpeed {0.5};
};
} // namespace longland

