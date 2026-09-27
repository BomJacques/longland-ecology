#include "analysis/ResourceAnalyzer.h"
#include "dsp/DummyGainProcessor.h"
#include "ecology/PredatorBloomScenario.h"
#include "ecology/Phase4EcologyModel.h"
#include "ecology/PredatorPreyModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <string>
#include <vector>

namespace
{
using namespace longland;

struct TestContext
{
    int failures {};

    void expect(const bool condition, const std::string& message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

ResourceMap analyzeMono(const std::vector<float>& audio, const std::size_t blockSize = 257)
{
    ResourceAnalyzer analyzer;
    analyzer.prepare({48000.0, 50.0, 1});
    for (std::size_t offset = 0; offset < audio.size(); offset += blockSize)
    {
        const auto count = std::min(blockSize, audio.size() - offset);
        const float* channel = audio.data() + offset;
        analyzer.processBlock(&channel, 1, count);
    }
    return analyzer.latest().resources;
}

std::vector<float> sine(const double frequency, const double seconds, const double amplitude = 0.5)
{
    const auto samples = static_cast<std::size_t>(48000.0 * seconds);
    std::vector<float> result(samples);
    for (std::size_t i = 0; i < samples; ++i)
        result[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency * i / 48000.0));
    return result;
}

void testResourceAnalysis(TestContext& test)
{
    std::vector<float> silence(4800, 0.0f);
    const auto silenceMap = analyzeMono(silence);
    test.expect(silenceMap[ResourceId::Silence] > 0.98, "silence must create the silence resource");
    test.expect(silenceMap[ResourceId::Activity] < 0.01, "silence must not create activity");

    auto impulses = std::vector<float>(48000, 0.0f);
    for (std::size_t i = 0; i < impulses.size(); i += 960)
        impulses[i] = 1.0f;
    const auto transientMap = analyzeMono(impulses);
    const auto sustainedMap = analyzeMono(sine(440.0, 1.0));
    test.expect(transientMap[ResourceId::Transient] > sustainedMap[ResourceId::Transient] + 0.01,
                "impulses must create more transient food than a sustained tone");
    test.expect(transientMap[ResourceId::Sparsity] > sustainedMap[ResourceId::Sparsity],
                "impulses must be sparser than a sustained tone");

    const auto low = analyzeMono(sine(80.0, 1.0));
    const auto mid = analyzeMono(sine(1000.0, 1.0));
    const auto high = analyzeMono(sine(8000.0, 1.0));
    test.expect(low[ResourceId::Low] > low[ResourceId::Mid] && low[ResourceId::Low] > low[ResourceId::High],
                "80 Hz must dominate the low resource");
    test.expect(mid[ResourceId::Mid] > mid[ResourceId::Low] && mid[ResourceId::Mid] > mid[ResourceId::High],
                "1 kHz must dominate the mid resource");
    test.expect(high[ResourceId::High] > high[ResourceId::Low] && high[ResourceId::High] > high[ResourceId::Mid],
                "8 kHz must dominate the high resource");

    ResourceAnalyzer antiPhase;
    antiPhase.prepare({48000.0, 50.0, 2});
    auto left = sine(1000.0, 0.2);
    auto right = left;
    for (auto& sample : right)
        sample = -sample;
    const float* channels[] {left.data(), right.data()};
    antiPhase.processBlock(channels, 2, left.size());
    test.expect(antiPhase.latest().resources[ResourceId::Activity] > 0.4,
                "anti-phase stereo must retain uncancelled energy");
    test.expect(antiPhase.latest().resources[ResourceId::Mid]
                    > antiPhase.latest().resources[ResourceId::Low]
              && antiPhase.latest().resources[ResourceId::Mid]
                    > antiPhase.latest().resources[ResourceId::High],
                "anti-phase stereo must retain spectral-band identity");

    const auto by64 = analyzeMono(sine(777.0, 1.0), 64);
    const auto by257 = analyzeMono(sine(777.0, 1.0), 257);
    for (std::size_t i = 0; i < ResourceMap::count; ++i)
        test.expect(std::abs(by64.values[i] - by257.values[i]) < 1.0e-10,
                    "analysis must be invariant to host block partitioning");
}

struct Trace
{
    std::vector<double> prey;
    std::vector<double> predator;
    std::vector<double> attack;
    std::vector<double> food;
};

Trace runClosedLoop(const double seconds, const bool attackEnabled = true, const std::size_t hostBlock = 960)
{
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t controlSamples = 960;
    constexpr double dt = controlSamples / sampleRate;
    const auto safeHostBlock = std::max<std::size_t>(1, hostBlock);
    std::vector<float> input(safeHostBlock, 1.0f);
    std::vector<float> preyBus(safeHostBlock);
    DummyGainProcessor preyGain;
    preyGain.prepare(sampleRate, 0.02);
    PredatorPreyModel model;
    model.reset();
    PredatorPreyControls controls;
    controls.attackEnabled = attackEnabled;

    Trace trace;
    const auto ticks = static_cast<std::size_t>(seconds / dt);
    trace.prey.reserve(ticks);
    trace.predator.reserve(ticks);
    trace.attack.reserve(ticks);
    trace.food.reserve(ticks);

    std::size_t remainingInTick = controlSamples;
    std::size_t emittedTicks = 0;
    double energy = 0.0;
    std::size_t energySamples = 0;
    while (emittedTicks < ticks)
    {
        const auto before = model.telemetry();
        preyGain.setPopulationTarget(before.preyPopulation, 1.0);
        const auto segment = std::min(safeHostBlock, remainingInTick);
        preyGain.process(input.data(), preyBus.data(), segment);
        for (std::size_t sampleIndex = 0; sampleIndex < segment; ++sampleIndex)
        {
            const auto sample = preyBus[sampleIndex];
            energy += static_cast<double>(sample) * sample;
        }
        energySamples += segment;
        remainingInTick -= segment;
        if (remainingInTick == 0)
        {
            const double measuredActivity = std::sqrt(energy / static_cast<double>(energySamples));
            model.step(measuredActivity, controls, dt);
            const auto& state = model.telemetry();
            trace.prey.push_back(state.preyPopulation);
            trace.predator.push_back(state.predatorPopulation);
            trace.attack.push_back(state.attackInfluence);
            trace.food.push_back(state.filteredPredatorFood);
            energy = 0.0;
            energySamples = 0;
            remainingInTick = controlSamples;
            ++emittedTicks;
        }
    }
    return trace;
}

std::vector<std::size_t> peaks(const std::vector<double>& values, const std::size_t begin, const double threshold)
{
    std::vector<std::size_t> result;
    for (std::size_t i = std::max<std::size_t>(begin, 1); i + 1 < values.size(); ++i)
        if (values[i] > threshold && values[i] > values[i - 1] && values[i] >= values[i + 1]
            && (result.empty() || i - result.back() > 100))
            result.push_back(i);
    return result;
}

void testPredatorPreyGate(TestContext& test)
{
    const auto trace = runClosedLoop(300.0, true);
    const auto preyPeaks = peaks(trace.prey, 1500, 0.35);
    const auto predatorPeaks = peaks(trace.predator, 1500, 0.20);
    const auto [preyMin, preyMax] = std::minmax_element(trace.prey.begin(), trace.prey.end());
    const auto [predatorMin, predatorMax] = std::minmax_element(trace.predator.begin(), trace.predator.end());

    test.expect(preyPeaks.size() >= 4, "constant input must produce repeated Prey peaks without an LFO");
    test.expect(predatorPeaks.size() >= 4, "constant input must produce repeated Predator peaks without an LFO");
    test.expect(*preyMin < 0.04 && *preyMax > 0.55, "Prey must collapse and recover materially");
    test.expect(*predatorMax > 0.25, "Predator must grow from Prey-created measured activity");

    std::size_t causalPairs = 0;
    for (const auto preyPeak : preyPeaks)
    {
        const auto later = std::find_if(predatorPeaks.begin(), predatorPeaks.end(),
                                        [preyPeak](const auto predatorPeak) {
                                            return predatorPeak > preyPeak && predatorPeak - preyPeak < 1000;
                                        });
        if (later != predatorPeaks.end())
            ++causalPairs;
    }
    test.expect(causalPairs >= 3, "Predator peaks must causally follow Prey peaks");

    const auto noAttack = runClosedLoop(300.0, false);
    const double attackedLateMean = std::accumulate(trace.prey.end() - 2500, trace.prey.end(), 0.0) / 2500.0;
    const double freeLateMean = std::accumulate(noAttack.prey.end() - 2500, noAttack.prey.end(), 0.0) / 2500.0;
    test.expect(freeLateMean > attackedLateMean + 0.15,
                "disabling only the attack edge must materially release Prey");

    const auto repeat = runClosedLoop(300.0, true);
    test.expect(repeat.prey == trace.prey && repeat.predator == trace.predator,
                "same input and state must be exactly deterministic");

    const auto host64 = runClosedLoop(60.0, true, 64);
    const auto host257 = runClosedLoop(60.0, true, 257);
    test.expect(host64.prey == host257.prey && host64.predator == host257.predator,
                "fixed 50 Hz ecology must be invariant to 64- and 257-sample host blocks");

    for (std::size_t i = 0; i < trace.prey.size(); ++i)
    {
        test.expect(std::isfinite(trace.prey[i]) && std::isfinite(trace.predator[i]),
                    "all populations must remain finite");
        test.expect(trace.prey[i] >= 0.0 && trace.prey[i] <= 1.0
                 && trace.predator[i] >= 0.0 && trace.predator[i] <= 1.0,
                    "all populations must remain bounded");
    }

    PredatorPreyModel longRun;
    longRun.reset();
    PredatorPreyControls controls;
    for (std::size_t i = 0; i < 8u * 60u * 60u * 50u; ++i)
    {
        const auto activity = 0.01 + 0.99 * longRun.telemetry().preyPopulation;
        longRun.step(activity, controls, 0.02);
    }
    const auto& final = longRun.telemetry();
    test.expect(final.numericalFaults == 0 && std::isfinite(final.preyPopulation)
             && std::isfinite(final.predatorPopulation),
                "accelerated eight-hour run must finish without numerical recovery");

    std::cout << "Phase 2 metrics: prey_range=[" << *preyMin << ',' << *preyMax
              << "] predator_range=[" << *predatorMin << ',' << *predatorMax
              << "] prey_peaks=" << preyPeaks.size()
              << " predator_peaks=" << predatorPeaks.size()
              << " attack_counterfactual_delta=" << (freeLateMean - attackedLateMean) << '\n';
}

void testPhase3Ecology(TestContext& test)
{
    GlobalControls controls;
    controls.resources = 0.85;
    controls.stability = 0.45;
    controls.populationSpeed = 0.55;

    ResourceMap aggressive;
    aggressive[ResourceId::Activity] = 0.95;
    aggressive[ResourceId::Transient] = 1.0;
    aggressive[ResourceId::High] = 0.82;
    aggressive.sanitize();

    ResourceMap sparse;
    sparse[ResourceId::Silence] = 0.95;
    sparse[ResourceId::Sparsity] = 0.92;
    sparse[ResourceId::Tail] = 0.45;
    sparse.sanitize();

    ResourceMap contested = aggressive;
    contested[ResourceId::Silence] = 0.40;
    contested[ResourceId::Sparsity] = 0.55;
    contested[ResourceId::Tail] = 0.65;
    contested.sanitize();

    auto aggressiveEcology = PredatorBloomScenario::create(true);
    auto contestedAttack = PredatorBloomScenario::create(true);
    auto contestedNoAttack = PredatorBloomScenario::create(false);
    auto sparseEcology = PredatorBloomScenario::create(true);
    for (int tick = 0; tick < 3000; ++tick)
    {
        aggressiveEcology.step(aggressive, controls, 0.02);
        contestedAttack.step(contested, controls, 0.02);
        contestedNoAttack.step(contested, controls, 0.02);
        sparseEcology.step(sparse, controls, 0.02);
    }

    const auto& predatorOnAggressive = aggressiveEcology.species(PredatorBloomScenario::predator);
    const auto& bloomOnAggressive = aggressiveEcology.species(PredatorBloomScenario::bloom);
    const auto& predatorOnSparse = sparseEcology.species(PredatorBloomScenario::predator);
    const auto& bloomOnSparse = sparseEcology.species(PredatorBloomScenario::bloom);
    const auto& bloomWithAttack = contestedAttack.species(PredatorBloomScenario::bloom);
    const auto& bloomWithoutAttack = contestedNoAttack.species(PredatorBloomScenario::bloom);

    test.expect(predatorOnAggressive.population > bloomOnAggressive.population + 0.25,
                "transient/high-energy habitat must favour Predator over Bloom");
    test.expect(bloomOnSparse.population > predatorOnSparse.population + 0.25,
                "silence/sparsity/tails must favour Bloom over Predator");
    test.expect(bloomWithoutAttack.population > bloomWithAttack.population + 0.02,
                "Predator attack edge must measurably suppress Bloom");
    for (const auto value : {predatorOnAggressive.population, bloomOnAggressive.population,
                             predatorOnSparse.population, bloomOnSparse.population})
        test.expect(std::isfinite(value) && value >= 0.0 && value <= 1.0,
                    "Phase 3 ecology populations must remain finite and bounded");
}

void testPhase4Ecology(TestContext& test)
{
    PredatorPreyControls controls;
    controls.resources = 0.92;
    controls.stability = 0.35;
    controls.populationSpeed = 0.55;

    Phase4Resources lush;
    lush.bloomFood = 0.88;
    lush.measuredBloomActivity = 0.78;
    lush.transientEnergy = 0.40;
    lush.tailEnergy = 0.82;
    lush.repetition = 0.55;
    lush.scavengerTail = 0.74;

    auto withResources = Phase4EcologyModel {};
    auto withoutResources = Phase4EcologyModel {};
    auto withoutMeasuredReturn = Phase4EcologyModel {};
    withResources.reset();
    withoutResources.reset();
    withoutMeasuredReturn.reset();
    auto deprived = lush;
    deprived.tailEnergy = 0.0;
    deprived.repetition = 0.0;
    deprived.scavengerTail = 0.0;
    auto noMeasuredReturn = lush;
    noMeasuredReturn.scavengerTail = 0.0;
    for (int tick = 0; tick < 3000; ++tick)
    {
        withResources.step(lush, controls, 0.02);
        withoutResources.step(deprived, controls, 0.02);
        withoutMeasuredReturn.step(noMeasuredReturn, controls, 0.02);
    }
    test.expect(withResources.telemetry().scavenger.population
                    > withoutResources.telemetry().scavenger.population + 0.10,
                "real tail/repetition resources must grow Scavenger");
    test.expect(withResources.telemetry().scavengerBloomSupport > 0.01,
                "successful Scavenger must create measurable Bloom support");
    test.expect(withoutMeasuredReturn.telemetry().scavengerBloomSupport == 0.0,
                "Scavenger population alone must not support Bloom without a measured return tail");

    // Musical-timescale trigger test: dry transients first starve Scavenger;
    // repeated plucks and real tail resources must then recolonise it inside
    // twenty seconds. Nothing in this trajectory is time-scripted by the model.
    Phase4Resources dryTransient;
    dryTransient.bloomFood = 0.10;
    dryTransient.measuredBloomActivity = 0.08;
    dryTransient.transientEnergy = 0.90;
    Phase4Resources repeatedTail;
    repeatedTail.bloomFood = 0.82;
    repeatedTail.measuredBloomActivity = 0.52;
    repeatedTail.tailEnergy = 0.55;
    repeatedTail.repetition = 0.42;
    repeatedTail.scavengerTail = 0.50;
    Phase4EcologyModel musicalScavenger;
    musicalScavenger.reset();
    const double initialScavenger = musicalScavenger.telemetry().scavenger.population;
    for (int tick = 0; tick < 500; ++tick)
        musicalScavenger.step(dryTransient, controls, 0.02);
    const double dryScavenger = musicalScavenger.telemetry().scavenger.population;
    for (int tick = 0; tick < 750; ++tick)
        musicalScavenger.step(repeatedTail, controls, 0.02);
    const double tailScavenger = musicalScavenger.telemetry().scavenger.population;
    test.expect(dryScavenger < initialScavenger - 0.025,
                "ten seconds of dry transient material must visibly starve Scavenger");
    test.expect(tailScavenger > dryScavenger + 0.12,
                "repeated plucks and Bloom tails must visibly regrow Scavenger within fifteen seconds");

    Phase4EcologyModel causalSupport;
    Phase4EcologyModel noSupport;
    causalSupport.reset();
    noSupport.reset();
    auto supportControls = controls;
    supportControls.attackEnabled = false;
    auto supportHabitat = repeatedTail;
    supportHabitat.bloomFood = 0.12;
    supportHabitat.measuredBloomActivity = 0.30;
    supportHabitat.scavengerTail = 0.70;
    for (int tick = 0; tick < 1000; ++tick)
    {
        causalSupport.step(supportHabitat, supportControls, 0.02);
        auto returnRemoved = supportHabitat;
        returnRemoved.scavengerTail = 0.0;
        noSupport.step(returnRemoved, supportControls, 0.02);
    }
    test.expect(causalSupport.telemetry().scavengerBloomSupport > 0.02,
                "measured Scavenger return must create a material support edge");
    test.expect(causalSupport.telemetry().predatorBloom.preyPopulation
                    > noSupport.telemetry().predatorBloom.preyPopulation + 0.005,
                "removing only the measured Scavenger return must reduce Bloom outcome");

    Phase4EcologyModel grazed;
    Phase4EcologyModel ungrazed;
    grazed.reset();
    ungrazed.reset();
    Phase4Resources bright = lush;
    bright.highEnergy = 0.95;
    bright.grazedHighEnergy = 0.30;
    bright.spectralDominance = 0.90;
    Phase4Resources dark = lush;
    dark.highEnergy = 0.0;
    dark.grazedHighEnergy = 0.0;
    dark.spectralDominance = 0.0;
    for (int tick = 0; tick < 6000; ++tick)
    {
        grazed.step(bright, controls, 0.02);
        ungrazed.step(dark, controls, 0.02);
    }
    test.expect(grazed.telemetry().grazer.population
                    > ungrazed.telemetry().grazer.population + 0.10,
                "high-frequency dominance must grow Grazer");
    test.expect(grazed.telemetry().grazerPredatorSuppression > 0.02,
                "Grazer must create measurable Predator-food suppression");

    Phase4EcologyModel longRun;
    longRun.reset();
    for (std::size_t tick = 0; tick < 8u * 60u * 60u * 50u; ++tick)
        longRun.step((tick / 500u) % 2u == 0u ? bright : deprived, controls, 0.02);
    const auto& final = longRun.telemetry();
    for (const auto value : {final.predatorBloom.preyPopulation,
                             final.predatorBloom.predatorPopulation,
                             final.scavenger.population, final.grazer.population})
        test.expect(std::isfinite(value) && value >= 0.0 && value <= 1.0,
                    "all Phase 4 populations must survive an accelerated eight-hour run bounded");
    test.expect(final.numericalFaults == 0,
                "Phase 4 accelerated eight-hour run must need no numerical recovery");
}
} // namespace

int main()
{
    TestContext test;
    testResourceAnalysis(test);
    testPredatorPreyGate(test);
    testPhase3Ecology(test);
    testPhase4Ecology(test);
    if (test.failures == 0)
    {
        std::cout << "PASS: Phase 1 resource analysis and mandatory Phase 2 gain-only ecology gate\n";
        return 0;
    }
    std::cerr << "GAUNTLET FAILED with " << test.failures << " assertion(s)\n";
    return 1;
}
