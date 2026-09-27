#pragma once

#include <cstddef>

namespace longland
{
struct PredatorPreyControls
{
    double resources {0.92};
    double stability {0.25};
    double populationSpeed {0.5};
    bool attackEnabled {true};
};

struct PredatorPreyTelemetry
{
    double timeSeconds {};
    double preyPopulation {0.15};
    double predatorPopulation {0.03};
    double preyHealth {0.5};
    double predatorHealth {0.5};
    double rawPredatorFood {};
    double filteredPredatorFood {};
    double preyBirthRate {};
    double preyDeathRate {};
    double predatorBirthRate {};
    double predatorDeathRate {};
    double attackInfluence {};
    double preyGain {0.01};
    double predatorGain {0.01};
    std::size_t numericalFaults {};
};

class PredatorPreyModel
{
public:
    void reset(double preyPopulation = 0.15, double predatorPopulation = 0.03) noexcept;
    void step(double measuredPreyActivity, const PredatorPreyControls&, double deltaSeconds) noexcept;
    const PredatorPreyTelemetry& telemetry() const noexcept { return state; }

private:
    static double integratePopulation(double population, double birthRate, double deathRate, double dt) noexcept;
    static double hill(double value, double halfPoint, double power) noexcept;
    void recoverFromFault() noexcept;

    PredatorPreyTelemetry state;
    PredatorPreyTelemetry lastGood;
};
} // namespace longland

