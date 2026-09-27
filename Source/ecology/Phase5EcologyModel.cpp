#include "ecology/Phase5EcologyModel.h"
#include "core/MathSafety.h"

#include <algorithm>
#include <cmath>

namespace longland
{
void Phase5EcologyModel::reset() noexcept
{
    phase4.reset();
    state = {};
    state.habitat = phase4.telemetry();
    state.parasite.population = 0.008;
    state.parasite.health = 0.30;
}

double Phase5EcologyModel::integrate(const double population,
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

void Phase5EcologyModel::step(const Phase5Resources& input,
                              const PredatorPreyControls& controls,
                              double deltaSeconds) noexcept
{
    deltaSeconds = std::clamp(finiteOr(deltaSeconds), 0.0, 0.25);
    const double stability = unit(controls.stability);
    const double speed = (0.25 + 1.50 * unit(controls.populationSpeed))
                       * (1.15 - 0.30 * stability);

    const auto& previousHabitat = phase4.telemetry();
    const auto& bloom = previousHabitat.predatorBloom;
    const double hostPopulation = unit(bloom.preyPopulation);
    const double hostHealth = unit(bloom.preyHealth);
    const double hostEcology = smoothStep(unit(hostPopulation * hostHealth));
    const double hostAudio = unit(1.5 * unit(input.measuredHostActivity));
    const double hostViability = std::sqrt(unit(hostEcology * hostAudio));

    // Pressure is lagged by one ecological frame. Parasite cannot weaken a
    // host in the same frame that it grows from that host.
    const double measuredModification = unit(input.measuredModification);
    const double hostPressure = unit(0.45 * hostViability * std::sqrt(unit(
        state.parasite.population * state.parasite.health)
        * measuredModification));

    auto phase4Controls = controls;
    phase4Controls.resources = unit(controls.resources) * (1.0 - hostPressure);
    phase4.step(input.habitat, phase4Controls, deltaSeconds);

    const double measuredHost = unit(input.measuredHostActivity);
    const double stableSustain = unit(input.stableSustain);
    const double rawFood = unit(hostViability * unit(
        0.55 * stableSustain + 0.25 * unit(input.hostTail)
        + 0.20 * measuredHost));
    const double foodA = smoothingCoefficient(deltaSeconds * speed,
                                               1.6 + 1.8 * stability);
    state.parasite.foodLevel = unit(foodA * state.parasite.foodLevel
        + (1.0 - foodA) * rawFood);
    // A small food-dependent settlement term lets dormant spores recolonise a
    // viable Bloom host. Reproduction then accelerates with the attached
    // population; neither term can operate when the measured host is absent.
    state.parasite.birthRate = 0.25 * state.parasite.foodLevel
        + 2.60 * state.parasite.foodLevel * state.parasite.population;
    state.parasite.deathRate = 0.04
        + 0.35 * (1.0 - state.parasite.foodLevel)
        + 0.18 * (1.0 - hostViability);
    state.parasite.population = integrate(state.parasite.population,
        state.parasite.birthRate, state.parasite.deathRate,
        deltaSeconds * speed);
    const double healthTarget = state.parasite.birthRate
        / (state.parasite.birthRate + state.parasite.deathRate + 1.0e-12);
    const double healthA = smoothingCoefficient(deltaSeconds * speed,
                                                0.9 + 1.6 * stability);
    state.parasite.health = unit(healthA * state.parasite.health
        + (1.0 - healthA) * healthTarget);
    state.parasite.hostPopulation = hostPopulation;
    state.parasite.hostViability = hostViability;
    state.parasite.measuredModification = measuredModification;
    state.parasite.hostPressure = hostPressure;
    state.parasite.dspIntensity = unit(hostViability
        * std::sqrt(state.parasite.population)
        * (0.40 + 0.60 * state.parasite.health));
    state.habitat = phase4.telemetry();

    const bool valid = std::isfinite(state.parasite.population)
                    && std::isfinite(state.parasite.health)
                    && std::isfinite(state.parasite.dspIntensity)
                    && state.parasite.population >= 0.0
                    && state.parasite.population <= 1.0;
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
