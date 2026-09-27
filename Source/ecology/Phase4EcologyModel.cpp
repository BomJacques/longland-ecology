#include "ecology/Phase4EcologyModel.h"
#include "core/MathSafety.h"
#include <algorithm>
#include <cmath>

namespace longland
{
void Phase4EcologyModel::reset() noexcept
{
    core.reset(0.45, 0.05);
    state = {};
    state.predatorBloom = core.telemetry();
    state.scavenger.population = 0.08;
    state.scavenger.health = 0.45;
    state.grazer.population = 0.04;
    state.grazer.health = 0.40;
}

double Phase4EcologyModel::integrate(const double population,
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

void Phase4EcologyModel::updateAuxiliary(AuxiliarySpeciesTelemetry& species,
                                         const double food,
                                         const double recoveryScale,
                                         const double birthScale,
                                         const double naturalDeath,
                                         const double starvation,
                                         const double speed,
                                         const double stability,
                                         const double dt) noexcept
{
    const double foodA = smoothingCoefficient(dt * speed, 1.4 + 1.6 * stability);
    species.foodLevel = unit(foodA * species.foodLevel + (1.0 - foodA) * unit(food));
    // A small viable seed makes ecological extinction reversible on musical
    // timescales without keeping the DSP audibly active while starved.
    species.birthRate = 2.0e-3 + recoveryScale * species.foodLevel
                      + birthScale * species.foodLevel * species.population;
    species.deathRate = naturalDeath + starvation * (1.0 - species.foodLevel);
    species.population = integrate(species.population, species.birthRate, species.deathRate, dt * speed);
    const double healthTarget = species.birthRate
                              / (species.birthRate + species.deathRate + 1.0e-12);
    const double healthA = smoothingCoefficient(dt * speed, 0.75 + 1.5 * stability);
    species.health = unit(healthA * species.health + (1.0 - healthA) * healthTarget);
    species.dspIntensity = smoothStep(species.population * species.health);
}

void Phase4EcologyModel::step(const Phase4Resources& input,
                              const PredatorPreyControls& controls,
                              double deltaSeconds) noexcept
{
    deltaSeconds = std::clamp(finiteOr(deltaSeconds), 0.0, 0.25);
    const double stability = unit(controls.stability);
    const double speed = (0.25 + 1.50 * unit(controls.populationSpeed)) * (1.15 - 0.30 * stability);

    const auto& previousCore = core.telemetry();
    // Modifiers are calculated from the previous organism state and this
    // frame's measured audio. That explicit one-frame causality prevents a
    // population update from feeding itself instantaneously.
    state.scavengerBloomSupport = unit(0.65 * std::sqrt(unit(
        state.scavenger.population * state.scavenger.health))
        * unit(input.scavengerTail));
    const double preGrazerHigh = unit(input.highEnergy);
    const double postGrazerHigh = unit(input.grazedHighEnergy);
    const double physicallyConsumedHigh = std::max(0.0, preGrazerHigh - postGrazerHigh);
    state.grazerPredatorSuppression = unit(physicallyConsumedHigh
                                         / std::max(0.02, preGrazerHigh));

    // The lightweight analyzer deliberately reports conservative tail values.
    // Calibrate that physical feature here, at the ecological preference, so
    // production and offline tests share one mapping and no DSP/test-only boost
    // is needed. Bloom activity is only a weak fallback; dry activity alone
    // cannot sustain Scavenger.
    const double scavengerFood = unit(2.40 * unit(input.tailEnergy)
                                    + 0.55 * unit(input.repetition)
                                    + 0.10 * unit(input.measuredBloomActivity));
    const double grazerFood = unit(2.40 * preGrazerHigh
                                 + 0.25 * unit(input.spectralDominance)
                                 + 0.20 * previousCore.predatorPopulation * preGrazerHigh);

    // Scavenger is intentionally opportunistic: a measured Bloom tail should
    // establish it within a musical phrase, while a dry habitat still removes
    // it decisively. These values put the crossover near food=0.3 rather than
    // requiring a near-full-scale analyzer resource.
    updateAuxiliary(state.scavenger, scavengerFood, 0.05, 1.45, 0.06, 0.34,
                    speed, stability, deltaSeconds);
    updateAuxiliary(state.grazer, grazerFood, 0.01, 1.10, 0.13, 0.78,
                    speed, stability, deltaSeconds);

    auto coreControls = controls;
    coreControls.resources = unit(controls.resources)
                           * unit(input.bloomFood + state.scavengerBloomSupport);
    // Predator retains the validated Bloom-as-prey input while also consuming
    // real source transients and the physically post-Grazer high-band result.
    // Removing the Grazer DSP therefore changes Predator food numerically.
    const double aggressiveSourceFood = unit(0.80 * unit(input.transientEnergy)
                                           + 0.20 * postGrazerHigh);
    const double predatorFoodBeforeGrazer = std::max(unit(input.measuredBloomActivity),
                                                     aggressiveSourceFood);
    const double predatorFood = predatorFoodBeforeGrazer
                              * (1.0 - 0.45 * state.grazerPredatorSuppression);
    core.step(predatorFood, coreControls, deltaSeconds);
    state.predatorBloom = core.telemetry();

    const bool valid = std::isfinite(state.scavenger.population)
                    && std::isfinite(state.grazer.population)
                    && std::isfinite(state.predatorBloom.preyPopulation)
                    && std::isfinite(state.predatorBloom.predatorPopulation);
    if (!valid)
    {
        const auto faults = state.numericalFaults + 1;
        reset();
        state.numericalFaults = faults;
    }
    else
    {
        state.numericalFaults = std::max(state.numericalFaults,
                                         state.predatorBloom.numericalFaults);
    }
}
} // namespace longland
