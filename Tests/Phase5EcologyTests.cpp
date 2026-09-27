#include "ecology/Phase5EcologyModel.h"

#include <cmath>
#include <iostream>
#include <limits>

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
    controls.resources = 0.98;
    controls.stability = 0.28;
    controls.populationSpeed = 0.65;
    controls.attackEnabled = false;

    longland::Phase5Resources hosted;
    hosted.habitat.bloomFood = 0.92;
    hosted.habitat.measuredBloomActivity = 0.78;
    hosted.habitat.tailEnergy = 0.45;
    hosted.habitat.repetition = 0.25;
    hosted.habitat.scavengerTail = 0.20;
    hosted.measuredHostActivity = 0.82;
    hosted.stableSustain = 0.88;
    hosted.hostTail = 0.62;
    hosted.measuredModification = 0.35;

    auto attached = longland::Phase5EcologyModel {};
    auto hostless = longland::Phase5EcologyModel {};
    attached.reset();
    hostless.reset();
    auto absent = hosted;
    absent.habitat.bloomFood = 0.0;
    absent.habitat.measuredBloomActivity = 0.0;
    absent.measuredHostActivity = 0.0;
    absent.stableSustain = 0.0;
    absent.hostTail = 0.0;
    absent.measuredModification = 0.0;
    for (int tick = 0; tick < 3000; ++tick)
    {
        attached.step(hosted, controls, 0.02);
        hostless.step(absent, controls, 0.02);
    }
    test.expect(attached.telemetry().parasite.population
                    > hostless.telemetry().parasite.population + 0.10,
                "Bloom host plus stable sustain must grow Parasite");
    test.expect(hostless.telemetry().parasite.population < 0.005,
                "Parasite must remain nonviable without its Bloom host");
    test.expect(attached.telemetry().parasite.dspIntensity > 0.03,
                "successful hosted Parasite must create DSP intensity");
    test.expect(attached.telemetry().parasite.hostPressure > 0.01,
                "successful Parasite must exert measurable host pressure");

    auto collapsed = absent;
    const double beforeCollapse = attached.telemetry().parasite.population;
    for (int tick = 0; tick < 1000; ++tick)
        attached.step(collapsed, controls, 0.02);
    test.expect(attached.telemetry().parasite.population < beforeCollapse - 0.08,
                "Parasite must decline after Bloom host collapse");
    test.expect(attached.telemetry().parasite.dspIntensity < 0.005,
                "host collapse must silence Parasite DSP");

    for (int tick = 0; tick < 1800; ++tick)
        attached.step(hosted, controls, 0.02);
    test.expect(attached.telemetry().parasite.population > 0.08,
                "dormant Parasite must recover when its host returns");

    auto longRun = longland::Phase5EcologyModel {};
    longRun.reset();
    for (std::size_t tick = 0; tick < 8u * 60u * 60u * 50u; ++tick)
        longRun.step((tick / 600u) % 2u == 0u ? hosted : absent,
                     controls, 0.02);
    const auto& final = longRun.telemetry();
    for (const double value : {final.parasite.population,
                               final.parasite.health,
                               final.parasite.dspIntensity,
                               final.habitat.predatorBloom.preyPopulation,
                               final.habitat.predatorBloom.predatorPopulation,
                               final.habitat.scavenger.population,
                               final.habitat.grazer.population})
        test.expect(std::isfinite(value) && value >= 0.0 && value <= 1.0,
                    "Phase 5 accelerated run must remain finite and bounded");
    test.expect(final.numericalFaults == 0,
                "Phase 5 accelerated run must need no numerical recovery");

    if (test.failures != 0)
        return 1;
    std::cout << "PASS: Phase 5 host dependence, collapse, recovery, and bounds\n";
    return 0;
}
