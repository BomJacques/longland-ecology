#include "ecology/PredatorPreyScenario.h"

namespace longland
{
EcologyEngine PredatorPreyScenario::create()
{
    EcologyEngine engine;

    SpeciesTraits preyTraits;
    preyTraits.preferences[static_cast<std::size_t>(ResourceId::Sustain)] = 1.0;
    preyTraits.preferences[static_cast<std::size_t>(ResourceId::Tail)] = 0.45;
    preyTraits.birthRate = 1.65;
    preyTraits.deathRate = 0.11;
    preyTraits.recoveryRate = 0.012;
    preyTraits.resilience = 0.28;
    preyTraits.healthSpeed = 2.2;

    SpeciesTraits predatorTraits;
    predatorTraits.birthRate = 1.35;
    predatorTraits.deathRate = 0.38;
    predatorTraits.recoveryRate = 0.001;
    predatorTraits.hunger = 1.15;
    predatorTraits.resilience = 0.58;
    predatorTraits.healthSpeed = 1.25;

    engine.addSpecies("Prey", preyTraits, 0.12, 0.7);
    engine.addSpecies("Predator", predatorTraits, 0.025, 0.35);
    engine.addRelationship(prey, predator, RelationshipType::Feeds, 1.0);
    engine.addRelationship(predator, prey, RelationshipType::Attacks, 1.0);
    return engine;
}
} // namespace longland

