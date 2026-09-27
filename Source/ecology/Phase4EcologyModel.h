#pragma once

#include "ecology/PredatorPreyModel.h"

namespace longland
{
struct Phase4Resources
{
    double bloomFood {};
    double measuredBloomActivity {};
    double transientEnergy {};
    double tailEnergy {};
    double repetition {};
    double scavengerTail {};
    double highEnergy {};
    double grazedHighEnergy {};
    double spectralDominance {};
};

struct AuxiliarySpeciesTelemetry
{
    double population {};
    double health {0.5};
    double foodLevel {};
    double birthRate {};
    double deathRate {};
    double dspIntensity {};
};

struct Phase4Telemetry
{
    PredatorPreyTelemetry predatorBloom;
    AuxiliarySpeciesTelemetry scavenger;
    AuxiliarySpeciesTelemetry grazer;
    double scavengerBloomSupport {};
    double grazerPredatorSuppression {};
    std::size_t numericalFaults {};
};

// Extends, rather than replaces, the validated Phase 2 Predator/Prey model.
// Bloom occupies the Prey role. Scavenger and Grazer alter only measured inputs
// to that unchanged core and receive food from real habitat resources.
class Phase4EcologyModel
{
public:
    void reset() noexcept;
    void step(const Phase4Resources&, const PredatorPreyControls&, double deltaSeconds) noexcept;
    const Phase4Telemetry& telemetry() const noexcept { return state; }

private:
    static double integrate(double population, double birthRate, double deathRate, double dt) noexcept;
    static void updateAuxiliary(AuxiliarySpeciesTelemetry&, double food, double recoveryScale,
                                double birthScale, double naturalDeath, double starvation,
                                double speed, double stability, double dt) noexcept;

    PredatorPreyModel core;
    Phase4Telemetry state;
};
} // namespace longland
