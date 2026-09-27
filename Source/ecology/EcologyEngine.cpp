#include "ecology/EcologyEngine.h"
#include "core/MathSafety.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace longland
{
std::size_t EcologyEngine::addSpecies(std::string_view name,
                                      const SpeciesTraits& traits,
                                      const double population,
                                      const double health)
{
    if (speciesSize >= maxSpecies)
        throw std::runtime_error("maximum species capacity exceeded");
    const auto index = speciesSize++;
    traitData[index] = traits;
    states[index].name = name;
    states[index].population = unit(population);
    states[index].health = unit(health);
    return index;
}

void EcologyEngine::addRelationship(const std::size_t from,
                                    const std::size_t to,
                                    const RelationshipType type,
                                    const double strength)
{
    if (from >= speciesSize || to >= speciesSize || edgeSize >= maxEdges)
        throw std::runtime_error("invalid relationship or edge capacity exceeded");
    edges[edgeSize++] = {from, to, type, unit(strength), 0.0};
}

void EcologyEngine::clear() noexcept
{
    states = {};
    traitData = {};
    edges = {};
    speciesSize = edgeSize = 0;
}

void EcologyEngine::step(const ResourceMap& resources,
                         const GlobalControls& controls,
                         double deltaSeconds) noexcept
{
    deltaSeconds = std::clamp(finiteOr(deltaSeconds), 0.0, 0.25);
    constexpr double maximumSubstep = 0.01;
    const auto substeps = std::max(1, static_cast<int>(std::ceil(deltaSeconds / maximumSubstep)));
    const double substep = deltaSeconds / static_cast<double>(substeps);
    for (int i = 0; i < substeps; ++i)
        stepSubframe(resources, controls, substep);
}

void EcologyEngine::stepSubframe(const ResourceMap& resourceMap,
                                 const GlobalControls& controls,
                                 const double dt) noexcept
{
    std::array<double, maxSpecies> food {};
    std::array<double, maxSpecies> support {};
    std::array<double, maxSpecies> pressure {};

    for (std::size_t speciesIndex = 0; speciesIndex < speciesSize; ++speciesIndex)
    {
        double weighted = 0.0;
        double weights = 0.0;
        for (std::size_t resource = 0; resource < ResourceMap::count; ++resource)
        {
            const double weight = unit(traitData[speciesIndex].preferences[resource]);
            weighted += weight * unit(resourceMap.values[resource]);
            weights += weight;
        }
        food[speciesIndex] = weights > 0.0 ? weighted / weights : 0.0;
        food[speciesIndex] *= 0.25 + 1.25 * unit(controls.resources);
    }

    for (std::size_t edgeIndex = 0; edgeIndex < edgeSize; ++edgeIndex)
    {
        auto& edge = edges[edgeIndex];
        const double source = states[edge.from].population * states[edge.from].health;
        const double target = states[edge.to].population;
        edge.currentInfluence = unit(edge.strength) * source;
        switch (edge.type)
        {
            case RelationshipType::Feeds:
                food[edge.to] += edge.currentInfluence;
                break;
            case RelationshipType::Supports:
                support[edge.to] += edge.currentInfluence;
                break;
            case RelationshipType::Attacks:
                pressure[edge.to] += edge.currentInfluence * (0.35 + 0.65 * target);
                break;
            case RelationshipType::Competes:
                pressure[edge.to] += edge.currentInfluence * 0.65;
                pressure[edge.from] += target * unit(edge.strength) * 0.65;
                break;
            case RelationshipType::Parasitises:
                food[edge.from] += target * unit(edge.strength);
                pressure[edge.to] += edge.currentInfluence * 0.5;
                break;
        }
    }

    const double stability = unit(controls.stability);
    const double speed = (0.15 + 2.85 * unit(controls.populationSpeed)) * (1.35 - 0.70 * stability);
    std::array<double, maxSpecies> nextPopulation {};
    std::array<double, maxSpecies> nextHealth {};

    for (std::size_t index = 0; index < speciesSize; ++index)
    {
        const auto& traits = traitData[index];
        const auto& state = states[index];
        const double availableFood = unit(food[index] + 0.35 * support[index]);
        const double effectivePressure = unit(pressure[index] * traits.aggression);
        const double healthTarget = unit((availableFood + 0.15 * support[index])
                                       / (0.25 + availableFood + effectivePressure));
        const double healthRate = traits.healthSpeed * (1.25 - 0.8 * stability);
        const double health = unit(state.health + (healthTarget - state.health) * healthRate * dt * speed);

        const double capacity = std::clamp(finiteOr(traits.carryingCapacity, 1.0), 0.05, 1.0);
        const double logisticRoom = std::max(0.0, 1.0 - state.population / capacity);
        const double reproduction = traits.birthRate * traits.hunger * traits.resourceEfficiency
                                  * availableFood * health * state.population * logisticRoom;
        const double recolonisation = traits.recoveryRate * availableFood * (1.0 - state.population);
        const double naturalDeath = traits.deathRate * state.population * (1.0 - 0.7 * health);
        const double attackDeath = effectivePressure * state.population
                                 * (1.4 - 0.8 * unit(traits.resilience));
        const double growth = std::max(0.0, finiteOr(reproduction + recolonisation));
        const double death = std::max(0.0, finiteOr(naturalDeath + attackDeath));
        const double rawDelta = std::clamp((growth - death) * dt * speed, -0.05, 0.05);

        nextPopulation[index] = unit(state.population + rawDelta);
        nextHealth[index] = health;
        states[index].foodLevel = availableFood;
        states[index].growthPerSecond = growth * speed;
        states[index].deathPerSecond = death * speed;
        states[index].pressure = effectivePressure;
    }

    for (std::size_t index = 0; index < speciesSize; ++index)
    {
        states[index].population = nextPopulation[index];
        states[index].health = nextHealth[index];
        states[index].dspIntensity = smoothStep(states[index].population * states[index].health);
        states[index].ageSeconds = std::max(0.0, finiteOr(states[index].ageSeconds + dt));
        sanitizeState(index);
    }
}

void EcologyEngine::sanitizeState(const std::size_t index) noexcept
{
    auto& state = states[index];
    state.population = unit(state.population);
    state.health = unit(state.health);
    state.foodLevel = unit(state.foodLevel);
    state.growthPerSecond = std::clamp(finiteOr(state.growthPerSecond), 0.0, 100.0);
    state.deathPerSecond = std::clamp(finiteOr(state.deathPerSecond), 0.0, 100.0);
    state.pressure = unit(state.pressure);
    state.dspIntensity = unit(state.dspIntensity);
    state.ageSeconds = std::clamp(finiteOr(state.ageSeconds), 0.0, 1.0e12);
}
} // namespace longland

