#include "ecology/PredatorPreyModel.h"
#include "core/MathSafety.h"
#include <algorithm>
#include <cmath>

namespace longland
{
void PredatorPreyModel::reset(const double preyPopulation, const double predatorPopulation) noexcept
{
    state = {};
    state.preyPopulation = unit(preyPopulation);
    state.predatorPopulation = unit(predatorPopulation);
    state.preyHealth = state.predatorHealth = 0.5;
    state.preyGain = 0.01 + 0.99 * smoothStep(state.preyPopulation);
    state.predatorGain = 0.01 + 0.99 * smoothStep(state.predatorPopulation);
    lastGood = state;
}

double PredatorPreyModel::hill(const double value, const double halfPoint, const double power) noexcept
{
    const double x = std::pow(unit(value), std::clamp(power, 1.0, 6.0));
    const double h = std::pow(std::clamp(halfPoint, 0.01, 0.99), std::clamp(power, 1.0, 6.0));
    return unit(x / (x + h + 1.0e-15));
}

double PredatorPreyModel::integratePopulation(const double population,
                                              const double birthRate,
                                              const double deathRate,
                                              const double dt) noexcept
{
    const double b = std::clamp(finiteOr(birthRate), 0.0, 8.0);
    const double d = std::clamp(finiteOr(deathRate), 0.0, 8.0);
    const double rate = b + d;
    if (rate <= 1.0e-15)
        return unit(population);
    const double equilibrium = b / rate;
    return unit(equilibrium + (unit(population) - equilibrium) * std::exp(-rate * dt));
}

void PredatorPreyModel::step(const double measuredPreyActivity,
                             const PredatorPreyControls& controls,
                             double deltaSeconds) noexcept
{
    deltaSeconds = std::clamp(finiteOr(deltaSeconds), 0.0, 0.25);
    const int substeps = std::max(1, static_cast<int>(std::ceil(deltaSeconds / 0.02)));
    const double dt = deltaSeconds / static_cast<double>(substeps);
    const double stability = unit(controls.stability);
    const double speed = (0.25 + 1.50 * unit(controls.populationSpeed)) * (1.15 - 0.30 * stability);

    for (int tick = 0; tick < substeps; ++tick)
    {
        state.rawPredatorFood = unit(measuredPreyActivity);
        const double foodTau = 1.5 + 2.0 * stability;
        const double foodA = smoothingCoefficient(dt * speed, foodTau);
        state.filteredPredatorFood = foodA * state.filteredPredatorFood
                                   + (1.0 - foodA) * state.rawPredatorFood;

        const double x = state.preyPopulation;
        const double y = state.predatorPopulation;
        const double foodResponse = hill(state.filteredPredatorFood, 0.40, 3.0 - 0.8 * stability);
        state.preyBirthRate = 1.0e-6 + 0.78 * unit(controls.resources) * x;
        state.attackInfluence = controls.attackEnabled
                              ? (2.08 * (1.0 - 0.25 * stability) * y / (0.62 + x))
                              : 0.0;
        state.preyDeathRate = 0.0565 + state.attackInfluence;
        state.predatorBirthRate = 1.0e-6 + 2.75 * foodResponse * y;
        state.predatorDeathRate = 0.0365 + 1.50 * (1.0 - foodResponse);

        state.preyPopulation = integratePopulation(x, state.preyBirthRate, state.preyDeathRate, dt * speed);
        state.predatorPopulation = integratePopulation(y, state.predatorBirthRate, state.predatorDeathRate, dt * speed);

        const double preyOpportunity = state.preyBirthRate;
        const double predatorOpportunity = state.predatorBirthRate;
        const double preyHealthTarget = preyOpportunity / (preyOpportunity + state.preyDeathRate + 1.0e-12);
        const double predatorHealthTarget = predatorOpportunity
                                          / (predatorOpportunity + state.predatorDeathRate + 1.0e-12);
        const double healthA = smoothingCoefficient(dt * speed, 0.8 + 1.2 * stability);
        state.preyHealth = unit(healthA * state.preyHealth + (1.0 - healthA) * preyHealthTarget);
        state.predatorHealth = unit(healthA * state.predatorHealth + (1.0 - healthA) * predatorHealthTarget);
        state.preyGain = 0.01 + 0.99 * smoothStep(state.preyPopulation);
        state.predatorGain = 0.01 + 0.99 * smoothStep(state.predatorPopulation);
        state.timeSeconds += dt;

        const bool valid = std::isfinite(state.preyPopulation)
                        && std::isfinite(state.predatorPopulation)
                        && std::isfinite(state.filteredPredatorFood)
                        && state.preyPopulation >= 0.0 && state.preyPopulation <= 1.0
                        && state.predatorPopulation >= 0.0 && state.predatorPopulation <= 1.0;
        if (!valid)
            recoverFromFault();
        else
            lastGood = state;
    }
}

void PredatorPreyModel::recoverFromFault() noexcept
{
    const auto faults = state.numericalFaults + 1;
    state = lastGood;
    state.numericalFaults = faults;
    if (faults >= 3)
        reset(0.05, 0.01);
}
} // namespace longland
