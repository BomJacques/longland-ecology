#include "ecology/PredatorBloomScenario.h"

namespace longland
{
EcologyEngine PredatorBloomScenario::create(const bool predatorAttacksBloom)
{
    EcologyEngine engine;

    SpeciesTraits predatorTraits;
    predatorTraits.preferences[static_cast<std::size_t>(ResourceId::Transient)] = 1.0;
    predatorTraits.preferences[static_cast<std::size_t>(ResourceId::High)] = 0.55;
    predatorTraits.preferences[static_cast<std::size_t>(ResourceId::Activity)] = 0.25;
    predatorTraits.birthRate = 1.55;
    predatorTraits.deathRate = 0.28;
    predatorTraits.recoveryRate = 0.002;
    predatorTraits.hunger = 1.1;
    predatorTraits.resourceEfficiency = 1.05;
    predatorTraits.resilience = 0.62;
    predatorTraits.healthSpeed = 2.0;

    SpeciesTraits bloomTraits;
    bloomTraits.preferences[static_cast<std::size_t>(ResourceId::Silence)] = 1.0;
    bloomTraits.preferences[static_cast<std::size_t>(ResourceId::Sparsity)] = 0.75;
    bloomTraits.preferences[static_cast<std::size_t>(ResourceId::Tail)] = 0.65;
    bloomTraits.preferences[static_cast<std::size_t>(ResourceId::Sustain)] = 0.10;
    bloomTraits.birthRate = 1.05;
    bloomTraits.deathRate = 0.16;
    bloomTraits.recoveryRate = 0.010;
    bloomTraits.hunger = 0.92;
    bloomTraits.resourceEfficiency = 1.0;
    bloomTraits.resilience = 0.32;
    bloomTraits.healthSpeed = 1.45;

    engine.addSpecies("Predator", predatorTraits, 0.08, 0.45);
    engine.addSpecies("Bloom", bloomTraits, 0.15, 0.55);
    if (predatorAttacksBloom)
        engine.addRelationship(predator, bloom, RelationshipType::Attacks, 0.92);
    return engine;
}
} // namespace longland
