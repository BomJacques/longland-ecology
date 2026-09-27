#include "ecology/Phase6EcologyModel.h"

#include <cmath>
#include <iostream>

namespace
{
struct TestContext
{
    int failures {};
    void expect(const bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};
}

int main()
{
    TestContext test;
    longland::PredatorPreyControls controls;
    controls.resources = 0.96;
    controls.stability = 0.30;
    controls.populationSpeed = 0.68;
    controls.attackEnabled = false;

    longland::Phase6Resources aged;
    aged.habitat.habitat.bloomFood = 0.82;
    // Keep baseline Predator food below the transient created by the granular
    // counterfactual, so this test observes that resource path directly.
    aged.habitat.habitat.measuredBloomActivity = 0.22;
    aged.habitat.habitat.tailEnergy = 0.72;
    aged.habitat.habitat.repetition = 0.84;
    aged.habitat.habitat.scavengerTail = 0.66;
    aged.habitat.habitat.transientEnergy = 0.04;
    aged.habitat.measuredHostActivity = 0.65;
    aged.habitat.stableSustain = 0.74;
    aged.habitat.hostTail = 0.70;
    aged.measuredRepetition = 0.88;
    aged.stableSustain = 0.80;
    aged.oldBloomTail = 0.78;
    aged.oldScavengerTail = 0.74;
    aged.rapidlyChangingFreshMaterial = 0.04;
    aged.preGranularTransient = 0.05;
    aged.postGranularTransient = 0.28;
    aged.measuredGranularModification = 0.34;
    aged.measuredOldMaterialConsumption = 0.30;

    auto fresh = aged;
    fresh.habitat.habitat.tailEnergy = 0.03;
    fresh.habitat.habitat.repetition = 0.02;
    fresh.habitat.habitat.scavengerTail = 0.01;
    fresh.habitat.stableSustain = 0.04;
    fresh.habitat.hostTail = 0.02;
    fresh.measuredRepetition = 0.02;
    fresh.stableSustain = 0.04;
    fresh.oldBloomTail = 0.01;
    fresh.oldScavengerTail = 0.01;
    fresh.rapidlyChangingFreshMaterial = 0.96;
    fresh.preGranularTransient = 0.70;
    fresh.postGranularTransient = 0.70;
    fresh.measuredGranularModification = 0.0;
    fresh.measuredOldMaterialConsumption = 0.0;

    longland::Phase6EcologyModel decomposing;
    longland::Phase6EcologyModel freshOnly;
    decomposing.reset();
    freshOnly.reset();
    for (int tick = 0; tick < 3000; ++tick)
    {
        decomposing.step(aged, controls, 0.02);
        freshOnly.step(fresh, controls, 0.02);
    }
    test.expect(decomposing.telemetry().decomposer.population
                    > freshOnly.telemetry().decomposer.population + 0.12,
                "old repeating tails must grow Decomposer while fresh material does not");
    test.expect(freshOnly.telemetry().decomposer.population < 0.005,
                "rapidly changing fresh material must leave Decomposer dormant");
    test.expect(decomposing.telemetry().decomposer.dspIntensity > 0.04,
                "viable Decomposer must request granular DSP");
    test.expect(decomposing.telemetry().decomposer.habitatPressure > 0.005,
                "physical granular modification must attack repeated tail habitat");
    test.expect(decomposing.telemetry().decomposer.generatedTransientResource > 0.10,
                "physical granular output must create a new transient resource");

    // The generated resource must enter the unchanged nested model, not merely
    // illuminate a meter. Compare against a bypassed granular counterfactual.
    auto bypassedInput = aged;
    bypassedInput.postGranularTransient = bypassedInput.preGranularTransient;
    bypassedInput.measuredGranularModification = 0.0;
    bypassedInput.measuredOldMaterialConsumption = 0.0;
    longland::Phase6EcologyModel modified;
    longland::Phase6EcologyModel bypassed;
    modified.reset();
    bypassed.reset();
    for (int tick = 0; tick < 4500; ++tick)
    {
        modified.step(aged, controls, 0.02);
        bypassed.step(bypassedInput, controls, 0.02);
    }
    test.expect(modified.telemetry().habitat.habitat.predatorBloom.predatorPopulation
                    > bypassed.telemetry().habitat.habitat.predatorBloom.predatorPopulation + 0.01,
                "granular-created transients must materially feed Predator");
    test.expect(bypassed.telemetry().decomposer.habitatPressure == 0.0,
                "bypassed granular DSP must exert no habitat pressure");
    test.expect(bypassed.telemetry().decomposer.generatedTransientResource == 0.0,
                "bypassed granular DSP must create no transient resource");

    const double beforeStarvation = decomposing.telemetry().decomposer.population;
    for (int tick = 0; tick < 1600; ++tick)
        decomposing.step(fresh, controls, 0.02);
    test.expect(decomposing.telemetry().decomposer.population
                    < 0.20 * beforeStarvation,
                "Decomposer must approach extinction when aged habitat disappears");
    test.expect(decomposing.telemetry().decomposer.dspIntensity < 0.002,
                "starved Decomposer must silence its granular DSP");
    for (int tick = 0; tick < 2200; ++tick)
        decomposing.step(aged, controls, 0.02);
    test.expect(decomposing.telemetry().decomposer.population > 0.12,
                "dormant Decomposer must recover when aged habitat returns");

    auto longRun = longland::Phase6EcologyModel {};
    longRun.reset();
    for (std::size_t tick = 0; tick < 8u * 60u * 60u * 50u; ++tick)
        longRun.step((tick / 650u) % 2u == 0u ? aged : fresh,
                     controls, 0.02);
    const auto& final = longRun.telemetry();
    for (const double value : {final.decomposer.population,
                               final.decomposer.health,
                               final.decomposer.dspIntensity,
                               final.decomposer.generatedTransientResource,
                               final.habitat.parasite.population,
                               final.habitat.habitat.predatorBloom.preyPopulation,
                               final.habitat.habitat.predatorBloom.predatorPopulation,
                               final.habitat.habitat.scavenger.population,
                               final.habitat.habitat.grazer.population})
        test.expect(std::isfinite(value) && value >= 0.0 && value <= 1.0,
                    "Phase 6 accelerated run must remain finite and bounded");
    test.expect(final.numericalFaults == 0,
                "Phase 6 accelerated run must need no numerical recovery");

    if (test.failures != 0)
        return 1;
    std::cout << "Phase 6 metrics: aged_population="
              << modified.telemetry().decomposer.population
              << ", fresh_population=" << freshOnly.telemetry().decomposer.population
              << ", generated_transient="
              << modified.telemetry().decomposer.generatedTransientResource
              << ", habitat_pressure="
              << modified.telemetry().decomposer.habitatPressure
              << ", predator_modified="
              << modified.telemetry().habitat.habitat.predatorBloom.predatorPopulation
              << ", predator_bypassed="
              << bypassed.telemetry().habitat.habitat.predatorBloom.predatorPopulation
              << '\n';
    std::cout << "PASS: Phase 6 aged-habitat dependence, granular counterfactual, "
                 "extinction, recovery, and bounds\n";
    return 0;
}
