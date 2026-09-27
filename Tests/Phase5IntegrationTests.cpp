#include "analysis/ResourceAnalyzer.h"
#include "core/MathSafety.h"
#include "dsp/BloomProcessor.h"
#include "dsp/ParasiteProcessor.h"
#include "ecology/Phase5EcologyModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr std::size_t frameSamples = 960;
constexpr std::size_t totalFrames = 90u * 50u;

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

double sourceSample(const double time, const bool right) noexcept
{
    const double local = std::fmod(time, 1.0);
    if (time < 10.0)
    {
        const double age = std::fmod(time, 0.25);
        return 0.55 * std::exp(-38.0 * age)
             * std::sin(2.0 * std::numbers::pi * (right ? 1800.0 : 1500.0) * time);
    }
    if (time < 40.0 || time >= 60.0)
    {
        // Repeating half-second tonal phrases separated by genuine space.
        if (local > 0.58)
            return 0.0;
        const double edge = std::min(1.0, local * 35.0)
                          * std::min(1.0, (0.58 - local) * 24.0);
        const double detune = right ? 1.006 : 1.0;
        return edge * (0.25 * std::sin(2.0 * std::numbers::pi * 220.0 * detune * time)
                     + 0.17 * std::sin(2.0 * std::numbers::pi * 330.0 * detune * time)
                     + 0.10 * std::sin(2.0 * std::numbers::pi * 440.0 * detune * time));
    }
    // Dense material contracts Bloom; the host-audio edge is also explicitly
    // removed below to model a collapsed/unavailable host while source continues.
    return 0.20 * std::sin(2.0 * std::numbers::pi * 170.0 * time)
         + 0.16 * std::sin(2.0 * std::numbers::pi * 1230.0 * time)
         + 0.11 * std::sin(2.0 * std::numbers::pi * 6100.0 * time);
}

double hostIntensity(const longland::Phase5Telemetry& state) noexcept
{
    const auto& bloom = state.habitat.predatorBloom;
    return std::sqrt(longland::smoothStep(
        bloom.preyPopulation * bloom.preyHealth));
}
}

int main()
{
    TestContext test;
    longland::ResourceAnalyzer sourceAnalyzer;
    longland::ResourceAnalyzer preHostAnalyzer;
    longland::ResourceAnalyzer postHostAnalyzer;
    sourceAnalyzer.prepare({sampleRate, 50.0, 2});
    preHostAnalyzer.prepare({sampleRate, 50.0, 2});
    postHostAnalyzer.prepare({sampleRate, 50.0, 2});

    longland::BloomProcessor bloom;
    longland::ParasiteProcessor parasite;
    bloom.prepare(sampleRate);
    parasite.prepare(sampleRate);

    longland::Phase5EcologyModel active;
    longland::Phase5EcologyModel bypassed;
    longland::Phase5EcologyModel hostless;
    active.reset();
    bypassed.reset();
    hostless.reset();
    longland::PredatorPreyControls controls;
    controls.resources = 0.98;
    controls.stability = 0.20;
    controls.populationSpeed = 0.75;
    controls.attackEnabled = false;

    std::array<std::vector<float>, 2> source {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> bloomRaw {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> postHost {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};

    double modificationEnergy = 0.0;
    double preHostEnergyTotal = 0.0;
    double maximumOutput = 0.0;
    double maximumParasite = 0.0;
    double maximumPressure = 0.0;
    double maximumHostViability = 0.0;
    double maximumFood = 0.0;
    double maximumDspIntensity = 0.0;
    double maximumBloomCounterfactual = 0.0;
    double parasiteAt40 = 0.0;
    double parasiteAt60 = 0.0;
    double parasiteAt90 = 0.0;
    std::size_t nonFinite = 0;

    for (std::size_t frame = 0; frame < totalFrames; ++frame)
    {
        const double frameTime = static_cast<double>(frame) / 50.0;
        for (std::size_t sample = 0; sample < frameSamples; ++sample)
        {
            const double time = frameTime + static_cast<double>(sample) / sampleRate;
            source[0][sample] = static_cast<float>(sourceSample(time, false));
            source[1][sample] = static_cast<float>(sourceSample(time, true));
        }

        const auto before = active.telemetry();
        const double bloomAmount = hostIntensity(before);
        bloom.setIntensity(static_cast<float>(bloomAmount));
        bloom.setDecay(static_cast<float>(0.30
            + 0.68 * before.habitat.predatorBloom.preyPopulation));
        bloom.process(source[0].data(), source[1].data(),
                      bloomRaw[0].data(), bloomRaw[1].data(), frameSamples);

        parasite.setIntensity(static_cast<float>(before.parasite.dspIntensity));
        parasite.setDetuneCents(static_cast<float>(8.0
            + 18.0 * before.parasite.population));
        parasite.setSmear(static_cast<float>(0.22
            + 0.68 * before.parasite.health));
        parasite.setFeedback(static_cast<float>(0.04
            + 0.20 * before.parasite.population));
        parasite.process(bloomRaw[0].data(), bloomRaw[1].data(),
                         postHost[0].data(), postHost[1].data(), frameSamples);

        double sourceEnergy = 0.0;
        double preEnergy = 0.0;
        double postEnergy = 0.0;
        double cross = 0.0;
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            for (std::size_t sample = 0; sample < frameSamples; ++sample)
            {
                const double dry = source[channel][sample];
                const double pre = bloomAmount * bloomRaw[channel][sample];
                const double post = bloomAmount * postHost[channel][sample];
                sourceEnergy += dry * dry;
                preEnergy += pre * pre;
                postEnergy += post * post;
                cross += pre * post;
                maximumOutput = std::max(maximumOutput, std::abs(post));
                if (!std::isfinite(post))
                    ++nonFinite;
            }
        }
        const double count = 2.0 * static_cast<double>(frameSamples);
        const double sourceRms = std::sqrt(sourceEnergy / count);
        const double preRms = std::sqrt(preEnergy / count);
        const double gainMatch = std::sqrt(preEnergy / std::max(1.0e-12, postEnergy));
        const double differenceEnergy = std::max(0.0,
            preEnergy + gainMatch * gainMatch * postEnergy - 2.0 * gainMatch * cross);
        const double differenceRms = std::sqrt(differenceEnergy / count);
        modificationEnergy += differenceEnergy;
        preHostEnergyTotal += preEnergy;

        const std::array<const float*, 2> sourcePointers {
            source[0].data(), source[1].data()};
        const std::array<const float*, 2> prePointers {
            bloomRaw[0].data(), bloomRaw[1].data()};
        const std::array<const float*, 2> postPointers {
            postHost[0].data(), postHost[1].data()};
        sourceAnalyzer.processBlock(sourcePointers.data(), 2, frameSamples);
        preHostAnalyzer.processBlock(prePointers.data(), 2, frameSamples);
        postHostAnalyzer.processBlock(postPointers.data(), 2, frameSamples);
        const auto& sourceMap = sourceAnalyzer.latest().resources;
        const auto& preMap = preHostAnalyzer.latest().resources;
        const auto& postMap = postHostAnalyzer.latest().resources;

        longland::Phase5Resources resources;
        resources.habitat.bloomFood = longland::unit(
            0.55 * sourceMap[longland::ResourceId::Silence]
          + 0.45 * sourceMap[longland::ResourceId::Sparsity]);
        resources.habitat.measuredBloomActivity = longland::unit(
            1.2 * std::sqrt(postEnergy / count)
            / std::max(0.02, sourceRms));
        resources.habitat.transientEnergy = longland::unit(
            4.0 * sourceMap[longland::ResourceId::Transient]);
        resources.habitat.tailEnergy = longland::unit(
            bloomAmount * postMap[longland::ResourceId::Tail]);
        resources.habitat.repetition = longland::unit(
            bloomAmount * postMap[longland::ResourceId::Repetition]);
        resources.measuredHostActivity = longland::unit(preRms / std::max(0.02, sourceRms));
        resources.stableSustain = longland::unit(bloomAmount
            * preMap[longland::ResourceId::Sustain]
            * (1.0 - 0.5 * preMap[longland::ResourceId::DynamicContrast]));
        resources.hostTail = longland::unit(bloomAmount
            * preMap[longland::ResourceId::Tail]);
        resources.measuredModification = longland::unit(
            1.5 * differenceRms / std::max(0.02, preRms));

        // Explicit host-collapse window while the dry source remains active.
        if (frameTime >= 40.0 && frameTime < 60.0)
        {
            resources.measuredHostActivity = 0.0;
            resources.stableSustain = 0.0;
            resources.hostTail = 0.0;
            resources.measuredModification = 0.0;
        }
        active.step(resources, controls, 0.02);
        auto noModification = resources;
        noModification.measuredModification = 0.0;
        bypassed.step(noModification, controls, 0.02);
        auto noHost = resources;
        noHost.measuredHostActivity = 0.0;
        noHost.stableSustain = 0.0;
        noHost.hostTail = 0.0;
        noHost.measuredModification = 0.0;
        hostless.step(noHost, controls, 0.02);

        const auto& after = active.telemetry();
        maximumParasite = std::max(maximumParasite, after.parasite.population);
        maximumPressure = std::max(maximumPressure, after.parasite.hostPressure);
        maximumHostViability = std::max(maximumHostViability, after.parasite.hostViability);
        maximumFood = std::max(maximumFood, after.parasite.foodLevel);
        maximumDspIntensity = std::max(maximumDspIntensity, after.parasite.dspIntensity);
        maximumBloomCounterfactual = std::max(maximumBloomCounterfactual,
            std::abs(after.habitat.predatorBloom.preyPopulation
                   - bypassed.telemetry().habitat.predatorBloom.preyPopulation));
        if (frame + 1 == 40u * 50u)
            parasiteAt40 = after.parasite.population;
        if (frame + 1 == 60u * 50u)
            parasiteAt60 = after.parasite.population;
        if (frame + 1 == 90u * 50u)
            parasiteAt90 = after.parasite.population;
    }

    const double modificationRatio = std::sqrt(modificationEnergy
        / std::max(1.0e-12, preHostEnergyTotal));
    test.expect(maximumParasite > 0.12,
                "real Bloom-host audio must grow Parasite materially");
    test.expect(hostless.telemetry().parasite.population < 0.01,
                "same habitat without measured host audio must not sustain Parasite");
    test.expect(modificationRatio > 0.02,
                "hosted Parasite must make an audible gain-matched modification");
    test.expect(maximumPressure > 0.005,
                "physical Parasite modification must create host pressure");
    test.expect(maximumBloomCounterfactual > 0.01,
                "removing physical modification must change Bloom trajectory");
    test.expect(parasiteAt60 < 0.30 * parasiteAt40,
                "Parasite must collapse when measured Bloom host disappears");
    test.expect(parasiteAt90 > parasiteAt60 + 0.08,
                "Parasite must recover when measured Bloom host returns");
    test.expect(maximumOutput < 1.2 && nonFinite == 0,
                "Phase 5 integrated audio must remain finite with headroom");

    if (test.failures != 0)
    {
        std::cerr << "metrics: parasite_max=" << maximumParasite
                  << " hostless_final=" << hostless.telemetry().parasite.population
                  << " modification_ratio=" << modificationRatio
                  << " pressure_max=" << maximumPressure
                  << " host_max=" << maximumHostViability
                  << " food_max=" << maximumFood
                  << " dsp_max=" << maximumDspIntensity
                  << " bloom_counterfactual=" << maximumBloomCounterfactual
                  << " p40=" << parasiteAt40 << " p60=" << parasiteAt60
                  << " p90=" << parasiteAt90 << " peak=" << maximumOutput
                  << " nonfinite=" << nonFinite << '\n';
        return 1;
    }
    std::cout << "PASS: Phase 5 real-audio host dependence"
              << " parasite_max=" << maximumParasite
              << " modification_ratio=" << modificationRatio
              << " pressure_max=" << maximumPressure
              << " bloom_counterfactual=" << maximumBloomCounterfactual
              << " p40=" << parasiteAt40 << " p60=" << parasiteAt60
              << " p90=" << parasiteAt90 << '\n';
    return 0;
}
