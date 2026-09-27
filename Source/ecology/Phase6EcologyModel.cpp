#include "ecology/Phase6EcologyModel.h"
#include "core/MathSafety.h"

#include <algorithm>
#include <cmath>

namespace longland
{
void Phase6EcologyModel::reset() noexcept
{
    phase5.reset();
    state = {};
    state.habitat = phase5.telemetry();
    state.decomposer.population = 0.012;
    state.decomposer.health = 0.30;
}

double Phase6EcologyModel::integrate(const double population,
                                     const double birthRate,
                                     const double deathRate,
                                     const double dt) noexcept
{
    const double birth = std::clamp(finiteOr(birthRate), 0.0, 8.0);
    const double death = std::clamp(finiteOr(deathRate), 0.0, 8.0);
    const double rate = birth + death;
    if (rate <= 1.0e-15)
        return unit(population);
    const double equilibrium = birth / rate;
    return unit(equilibrium + (unit(population) - equilibrium) * std::exp(-rate * dt));
}

void Phase6EcologyModel::step(const Phase6Resources& input,
                              const PredatorPreyControls& controls,
                              double deltaSeconds) noexcept
{
    deltaSeconds = std::clamp(finiteOr(deltaSeconds), 0.0, 0.25);
    const double stability = unit(controls.stability);
    const double speed = (0.25 + 1.50 * unit(controls.populationSpeed))
                       * (1.15 - 0.30 * stability);

    const double repetition = unit(input.measuredRepetition);
    const double sustain = unit(input.stableSustain);
    const double bloomTail = unit(input.oldBloomTail);
    const double scavengerTail = unit(input.oldScavengerTail);
    const double freshness = unit(input.rapidlyChangingFreshMaterial);
    const double oldTail = unit(0.62 * bloomTail + 0.38 * scavengerTail);
    const double freshRejection = 1.0 - smoothStep(freshness);
    const double rawFood = unit((0.38 * repetition + 0.27 * sustain
                               + 0.35 * oldTail) * freshRejection);

    const double foodA = smoothingCoefficient(deltaSeconds * speed,
                                               1.5 + 1.7 * stability);
    state.decomposer.foodLevel = unit(foodA * state.decomposer.foodLevel
        + (1.0 - foodA) * rawFood);
    state.decomposer.birthRate = 8.0e-4
        + 0.20 * state.decomposer.foodLevel
        + 2.25 * state.decomposer.foodLevel * state.decomposer.population;
    state.decomposer.deathRate = 0.07
        + 0.58 * (1.0 - state.decomposer.foodLevel)
        + 0.35 * freshness;
    state.decomposer.population = integrate(state.decomposer.population,
        state.decomposer.birthRate, state.decomposer.deathRate,
        deltaSeconds * speed);
    const double healthTarget = state.decomposer.birthRate
        / (state.decomposer.birthRate + state.decomposer.deathRate + 1.0e-12);
    const double healthA = smoothingCoefficient(deltaSeconds * speed,
                                                0.9 + 1.6 * stability);
    state.decomposer.health = unit(healthA * state.decomposer.health
        + (1.0 - healthA) * healthTarget);

    // DSP intent may exist before a physical change is observed. In contrast,
    // ecological output and habitat attack below are strictly counterfactual:
    // bypassing the granular processor makes both exactly zero.
    state.decomposer.dspIntensity = unit(2.4
        * std::sqrt(state.decomposer.population)
        * (0.35 + 0.65 * state.decomposer.health)
        * std::sqrt(state.decomposer.foodLevel));
    const double modification = unit(input.measuredGranularModification);
    const double consumption = unit(input.measuredOldMaterialConsumption);
    const double generatedTransient = std::max(0.0,
        unit(input.postGranularTransient) - unit(input.preGranularTransient));
    state.decomposer.measuredModification = modification;
    state.decomposer.measuredConsumption = consumption;
    state.decomposer.generatedTransientResource = unit(
        1.8 * std::min(generatedTransient, modification));
    state.decomposer.habitatPressure = unit(0.70
        * std::sqrt(unit(state.decomposer.population * state.decomposer.health))
        * consumption * modification);

    auto phase5Input = input.habitat;
    const double pressure = state.decomposer.habitatPressure;
    phase5Input.habitat.tailEnergy = unit(
        phase5Input.habitat.tailEnergy * (1.0 - pressure));
    phase5Input.habitat.repetition = unit(
        phase5Input.habitat.repetition * (1.0 - 0.72 * pressure));
    phase5Input.habitat.scavengerTail = unit(
        phase5Input.habitat.scavengerTail * (1.0 - 0.82 * pressure));
    phase5Input.hostTail = unit(phase5Input.hostTail * (1.0 - 0.55 * pressure));
    phase5Input.habitat.transientEnergy = unit(
        phase5Input.habitat.transientEnergy
        + state.decomposer.generatedTransientResource);
    phase5.step(phase5Input, controls, deltaSeconds);
    state.habitat = phase5.telemetry();

    const bool valid = std::isfinite(state.decomposer.population)
                    && std::isfinite(state.decomposer.health)
                    && std::isfinite(state.decomposer.dspIntensity)
                    && std::isfinite(state.decomposer.generatedTransientResource)
                    && state.decomposer.population >= 0.0
                    && state.decomposer.population <= 1.0;
    if (!valid)
    {
        const auto faults = state.numericalFaults + 1;
        reset();
        state.numericalFaults = faults;
    }
    else
    {
        state.numericalFaults = std::max(state.numericalFaults,
                                         state.habitat.numericalFaults);
    }
}
} // namespace longland
