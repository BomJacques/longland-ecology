#pragma once

#include "ecology/Phase5EcologyModel.h"

namespace longland
{
struct Phase6Resources
{
    Phase5Resources habitat;

    // These are measured features of material reaching the granular branch.
    // "Old" is deliberately explicit: an ordinary fresh transient is not
    // decomposer food merely because it happens to have a long release.
    double measuredRepetition {};
    double stableSustain {};
    double oldBloomTail {};
    double oldScavengerTail {};
    double rapidlyChangingFreshMaterial {};

    // Physical counterfactual measurements around the granular processor.
    double preGranularTransient {};
    double postGranularTransient {};
    double measuredGranularModification {};
    double measuredOldMaterialConsumption {};
};

struct DecomposerTelemetry
{
    double population {};
    double health {0.35};
    double foodLevel {};
    double birthRate {};
    double deathRate {};
    double dspIntensity {};
    double measuredModification {};
    double measuredConsumption {};
    double generatedTransientResource {};
    double habitatPressure {};
};

struct Phase6Telemetry
{
    Phase5Telemetry habitat;
    DecomposerTelemetry decomposer;
    std::size_t numericalFaults {};
};

// Phase 6 is a thin wrapper around the validated Phase 5 ecosystem. It adds a
// Decomposer whose granular DSP is fed by aged, repeating habitat. Only
// physically measured pre/post changes may consume that habitat or create the
// transient resource offered to Predator.
class Phase6EcologyModel
{
public:
    void reset() noexcept;
    void step(const Phase6Resources&, const PredatorPreyControls&, double deltaSeconds) noexcept;
    const Phase6Telemetry& telemetry() const noexcept { return state; }

private:
    static double integrate(double population, double birthRate,
                            double deathRate, double dt) noexcept;

    Phase5EcologyModel phase5;
    Phase6Telemetry state;
};
} // namespace longland
