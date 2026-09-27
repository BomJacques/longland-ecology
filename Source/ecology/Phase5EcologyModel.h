#pragma once

#include "ecology/Phase4EcologyModel.h"

namespace longland
{
struct Phase5Resources
{
    Phase4Resources habitat;
    double measuredHostActivity {};
    double stableSustain {};
    double hostTail {};
    double measuredModification {};
};

struct ParasiteTelemetry
{
    double population {};
    double health {0.35};
    double foodLevel {};
    double hostPopulation {};
    double hostViability {};
    double measuredModification {};
    double birthRate {};
    double deathRate {};
    double dspIntensity {};
    double hostPressure {};
};

struct Phase5Telemetry
{
    Phase4Telemetry habitat;
    ParasiteTelemetry parasite;
    std::size_t numericalFaults {};
};

// Phase 5 wraps the already-gated four-species model. Parasite is fixed to the
// Bloom host for this phase and cannot produce DSP intensity without that host.
class Phase5EcologyModel
{
public:
    void reset() noexcept;
    void step(const Phase5Resources&, const PredatorPreyControls&, double deltaSeconds) noexcept;
    const Phase5Telemetry& telemetry() const noexcept { return state; }

private:
    static double integrate(double population, double birthRate,
                            double deathRate, double dt) noexcept;

    Phase4EcologyModel phase4;
    Phase5Telemetry state;
};
} // namespace longland
