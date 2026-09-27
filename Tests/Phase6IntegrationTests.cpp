#include "analysis/ResourceAnalyzer.h"
#include "core/MathSafety.h"
#include "dsp/BloomProcessor.h"
#include "dsp/DecomposerProcessor.h"
#include "dsp/ParasiteProcessor.h"
#include "dsp/ScavengerProcessor.h"
#include "ecology/Phase6EcologyModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr std::size_t frameSamples = 960;
constexpr std::size_t framesPerSecond = 50;
constexpr std::size_t totalFrames = 110u * framesPerSecond;

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

std::uint32_t hashFrame(std::uint32_t value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

double sourceSample(const std::size_t frame,
                    const std::size_t sample,
                    const bool right) noexcept
{
    const double time = (static_cast<double>(frame * frameSamples + sample)) / sampleRate;
    const bool repeated = (frame >= 20u * framesPerSecond && frame < 65u * framesPerSecond)
                       || frame >= 85u * framesPerSecond;
    if (repeated)
    {
        const std::size_t sectionStart = frame < 65u * framesPerSecond
            ? 20u * framesPerSecond : 85u * framesPerSecond;
        constexpr std::size_t phraseSamples = 24000;
        const std::size_t phraseSample = ((frame - sectionStart) * frameSamples + sample)
                                       % phraseSamples;
        const double local = static_cast<double>(phraseSample) / sampleRate;
        if (local >= 0.31)
            return 0.0;
        const double edge = std::min(1.0, local * 45.0)
                          * std::min(1.0, (0.31 - local) * 28.0);
        const double detune = right ? 1.007 : 1.0;
        return edge * (0.27 * std::sin(2.0 * std::numbers::pi * 196.0 * detune * local)
                     + 0.16 * std::sin(2.0 * std::numbers::pi * 294.0 * detune * local)
                     + 0.09 * std::sin(2.0 * std::numbers::pi * 588.0 * detune * local));
    }

    // Every fresh frame changes level and spectral region deterministically.
    // This prevents the repetition detector from seeing a hidden fixed loop.
    const auto code = hashFrame(static_cast<std::uint32_t>(frame)
                              + (right ? 0x9e3779b9u : 0u));
    const std::array<double, 8> frequencies {83.0, 147.0, 263.0, 521.0,
                                             1031.0, 2089.0, 4217.0, 7129.0};
    const double frequency = frequencies[code % frequencies.size()];
    const double amplitude = 0.10 + 0.30 * static_cast<double>((code >> 8) & 255u) / 255.0;
    const double local = static_cast<double>(sample) / sampleRate;
    const double envelope = std::exp(-(9.0 + static_cast<double>((code >> 18) & 31u)) * local);
    return amplitude * envelope
         * (0.78 * std::sin(2.0 * std::numbers::pi * frequency * time)
          + 0.22 * std::sin(2.0 * std::numbers::pi * frequency * 2.73 * time));
}

double bloomIntensity(const longland::Phase6Telemetry& state) noexcept
{
    const auto& bloom = state.habitat.habitat.predatorBloom;
    return std::sqrt(longland::smoothStep(bloom.preyPopulation * bloom.preyHealth));
}

double scavengerIntensity(const longland::Phase6Telemetry& state) noexcept
{
    const auto& scavenger = state.habitat.habitat.scavenger;
    return std::pow(longland::unit(scavenger.population * scavenger.health), 0.35);
}
}

int main()
{
    TestContext test;
    longland::ResourceAnalyzer sourceAnalyzer;
    longland::ResourceAnalyzer bloomAnalyzer;
    longland::ResourceAnalyzer hostAnalyzer;
    longland::ResourceAnalyzer scavengerAnalyzer;
    longland::ResourceAnalyzer tailPreAnalyzer;
    longland::ResourceAnalyzer tailPostAnalyzer;
    for (auto* analyzer : {&sourceAnalyzer, &bloomAnalyzer, &hostAnalyzer,
                           &scavengerAnalyzer, &tailPreAnalyzer, &tailPostAnalyzer})
        analyzer->prepare({sampleRate, 50.0, 2});

    longland::BloomProcessor bloom;
    longland::ParasiteProcessor parasite;
    longland::ScavengerProcessor scavenger;
    longland::DecomposerProcessor decomposer;
    bloom.prepare(sampleRate);
    parasite.prepare(sampleRate);
    scavenger.prepare(sampleRate);
    decomposer.setRandomSeed(0x50483644u);
    decomposer.prepare(sampleRate);

    longland::Phase6EcologyModel active;
    longland::Phase6EcologyModel bypassed;
    active.reset();
    bypassed.reset();
    longland::PredatorPreyControls controls;
    controls.resources = 0.98;
    controls.stability = 0.18;
    controls.populationSpeed = 0.72;
    controls.attackEnabled = true;

    std::array<std::vector<float>, 2> source {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> bloomRaw {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> hosted {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> scavenged {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> tailPre {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> tailPost {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};

    double freshRepetitionSum = 0.0;
    double repeatedRepetitionSum = 0.0;
    std::size_t freshRepetitionFrames = 0;
    std::size_t repeatedRepetitionFrames = 0;
    double modificationEnergy = 0.0;
    double preEnergyTotal = 0.0;
    double maximumPopulation = 0.0;
    double maximumConsumption = 0.0;
    double maximumFragments = 0.0;
    double maximumFood = 0.0;
    double maximumDspIntensity = 0.0;
    double maximumSourceRepetition = 0.0;
    double maximumHostRepetition = 0.0;
    double maximumTailRepetition = 0.0;
    double maximumTailSustain = 0.0;
    double maximumOldBloomTail = 0.0;
    double maximumOldScavengerTail = 0.0;
    double maximumPredatorDivergence = 0.0;
    double maximumScavengerDivergence = 0.0;
    double maximumBloomDivergence = 0.0;
    double maximumOutput = 0.0;
    double maximumBypassPressure = 0.0;
    double maximumBypassFragments = 0.0;
    double populationAt20 = 0.0;
    double populationAt65 = 0.0;
    double populationAt85 = 0.0;
    double populationAt110 = 0.0;
    std::size_t firstGrowthFrame = totalFrames;
    std::size_t firstPredatorDivergenceFrame = totalFrames;
    std::size_t nonFinite = 0;

    for (std::size_t frame = 0; frame < totalFrames; ++frame)
    {
        for (std::size_t sample = 0; sample < frameSamples; ++sample)
        {
            source[0][sample] = static_cast<float>(sourceSample(frame, sample, false));
            source[1][sample] = static_cast<float>(sourceSample(frame, sample, true));
        }

        const auto before = active.telemetry();
        const double bloomAmount = bloomIntensity(before);
        const double scavengerAmount = scavengerIntensity(before);
        bloom.setIntensity(static_cast<float>(bloomAmount));
        bloom.setDecay(static_cast<float>(0.28
            + 0.69 * before.habitat.habitat.predatorBloom.preyPopulation));
        bloom.process(source[0].data(), source[1].data(),
                      bloomRaw[0].data(), bloomRaw[1].data(), frameSamples);

        parasite.setIntensity(static_cast<float>(before.habitat.parasite.dspIntensity));
        parasite.setDetuneCents(static_cast<float>(7.0
            + 19.0 * before.habitat.parasite.population));
        parasite.setSmear(static_cast<float>(0.20
            + 0.68 * before.habitat.parasite.health));
        parasite.setFeedback(static_cast<float>(0.04
            + 0.20 * before.habitat.parasite.population));
        parasite.process(bloomRaw[0].data(), bloomRaw[1].data(),
                         hosted[0].data(), hosted[1].data(), frameSamples);

        scavenger.setIntensity(scavengerAmount);
        scavenger.setFeedback(0.24
            + 0.58 * before.habitat.habitat.scavenger.population);
        scavenger.setDelayTimeMs(125.0
            + 430.0 * (1.0 - before.habitat.habitat.scavenger.health));
        scavenger.setDiffusion(0.20
            + 0.70 * before.habitat.habitat.scavenger.health);
        for (std::size_t sample = 0; sample < frameSamples; ++sample)
        {
            scavenged[0][sample] = static_cast<float>(bloomAmount) * hosted[0][sample];
            scavenged[1][sample] = static_cast<float>(bloomAmount) * hosted[1][sample];
        }
        scavenger.process(scavenged[0].data(), scavenged[1].data(),
                          scavenged[0].data(), scavenged[1].data(), frameSamples);

        for (std::size_t sample = 0; sample < frameSamples; ++sample)
        {
            for (std::size_t channel = 0; channel < 2; ++channel)
                tailPre[channel][sample] = static_cast<float>(
                    0.50 * bloomAmount * hosted[channel][sample]
                    + 0.42 * std::sqrt(scavengerAmount) * scavenged[channel][sample]);
        }
        decomposer.setIntensity(static_cast<float>(before.decomposer.dspIntensity));
        decomposer.setGrainSizeMilliseconds(static_cast<float>(
            64.0 - 46.0 * before.decomposer.population));
        decomposer.setDensity(static_cast<float>(
            0.08 + 0.68 * before.decomposer.population));
        decomposer.setScatter(static_cast<float>(
            0.55 + 0.40 * before.decomposer.health));
        decomposer.process(tailPre[0].data(), tailPre[1].data(),
                           tailPost[0].data(), tailPost[1].data(), frameSamples);

        double sourceEnergy = 0.0;
        double hostPreEnergy = 0.0;
        double hostPostEnergy = 0.0;
        double hostCross = 0.0;
        double granularPreEnergy = 0.0;
        double granularPostEnergy = 0.0;
        double granularCross = 0.0;
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            for (std::size_t sample = 0; sample < frameSamples; ++sample)
            {
                const double dry = source[channel][sample];
                const double preHost = bloomAmount * bloomRaw[channel][sample];
                const double postHost = bloomAmount * hosted[channel][sample];
                const double pre = tailPre[channel][sample];
                const double post = tailPost[channel][sample];
                sourceEnergy += dry * dry;
                hostPreEnergy += preHost * preHost;
                hostPostEnergy += postHost * postHost;
                hostCross += preHost * postHost;
                granularPreEnergy += pre * pre;
                granularPostEnergy += post * post;
                granularCross += pre * post;
                maximumOutput = std::max(maximumOutput, std::abs(post));
                if (!std::isfinite(post))
                    ++nonFinite;
            }
        }
        const double count = 2.0 * static_cast<double>(frameSamples);
        const double sourceRms = std::sqrt(sourceEnergy / count);
        const double hostPreRms = std::sqrt(hostPreEnergy / count);
        const double hostGain = std::sqrt(hostPreEnergy / std::max(1.0e-12, hostPostEnergy));
        const double hostDifference = std::max(0.0, hostPreEnergy
            + hostGain * hostGain * hostPostEnergy - 2.0 * hostGain * hostCross);
        const double granularPreRms = std::sqrt(granularPreEnergy / count);
        const double granularGain = std::sqrt(granularPreEnergy
            / std::max(1.0e-12, granularPostEnergy));
        const double granularDifference = std::max(0.0, granularPreEnergy
            + granularGain * granularGain * granularPostEnergy
            - 2.0 * granularGain * granularCross);
        const double granularDifferenceRms = std::sqrt(granularDifference / count);
        modificationEnergy += granularDifference;
        preEnergyTotal += granularPreEnergy;

        const std::array<const float*, 2> sourcePointers {source[0].data(), source[1].data()};
        const std::array<const float*, 2> bloomPointers {bloomRaw[0].data(), bloomRaw[1].data()};
        const std::array<const float*, 2> hostPointers {hosted[0].data(), hosted[1].data()};
        const std::array<const float*, 2> scavengerPointers {scavenged[0].data(), scavenged[1].data()};
        const std::array<const float*, 2> tailPrePointers {tailPre[0].data(), tailPre[1].data()};
        const std::array<const float*, 2> tailPostPointers {tailPost[0].data(), tailPost[1].data()};
        sourceAnalyzer.processBlock(sourcePointers.data(), 2, frameSamples);
        bloomAnalyzer.processBlock(bloomPointers.data(), 2, frameSamples);
        hostAnalyzer.processBlock(hostPointers.data(), 2, frameSamples);
        scavengerAnalyzer.processBlock(scavengerPointers.data(), 2, frameSamples);
        tailPreAnalyzer.processBlock(tailPrePointers.data(), 2, frameSamples);
        tailPostAnalyzer.processBlock(tailPostPointers.data(), 2, frameSamples);
        const auto& sourceMap = sourceAnalyzer.latest().resources;
        const auto& bloomMap = bloomAnalyzer.latest().resources;
        const auto& hostMap = hostAnalyzer.latest().resources;
        const auto& scavengerMap = scavengerAnalyzer.latest().resources;
        const auto& preMap = tailPreAnalyzer.latest().resources;
        const auto& postMap = tailPostAnalyzer.latest().resources;

        longland::Phase4Resources phase4Resources;
        phase4Resources.bloomFood = longland::unit(
            0.55 * sourceMap[longland::ResourceId::Silence]
            + 0.45 * sourceMap[longland::ResourceId::Sparsity]);
        phase4Resources.measuredBloomActivity = longland::unit(
            1.2 * std::sqrt(hostPostEnergy / count) / std::max(0.02, sourceRms));
        phase4Resources.transientEnergy = longland::unit(
            4.0 * sourceMap[longland::ResourceId::Transient]);
        phase4Resources.tailEnergy = longland::unit(
            bloomAmount * hostMap[longland::ResourceId::Tail]);
        phase4Resources.repetition = longland::unit(
            bloomAmount * hostMap[longland::ResourceId::Repetition]);
        phase4Resources.scavengerTail = longland::unit(
            scavengerAmount * scavengerMap[longland::ResourceId::Tail]);

        longland::Phase5Resources phase5Resources;
        phase5Resources.habitat = phase4Resources;
        phase5Resources.measuredHostActivity = longland::unit(
            hostPreRms / std::max(0.02, sourceRms));
        phase5Resources.stableSustain = longland::unit(bloomAmount
            * bloomMap[longland::ResourceId::Sustain]
            * (1.0 - 0.5 * bloomMap[longland::ResourceId::DynamicContrast]));
        phase5Resources.hostTail = longland::unit(
            bloomAmount * bloomMap[longland::ResourceId::Tail]);
        phase5Resources.measuredModification = longland::unit(
            1.5 * std::sqrt(hostDifference / count) / std::max(0.02, hostPreRms));

        const double frameModification = longland::unit(
            1.5 * granularDifferenceRms / std::max(0.02, granularPreRms));
        const double oldBefore = longland::unit(
            0.50 * preMap[longland::ResourceId::Repetition]
            + 0.30 * preMap[longland::ResourceId::Tail]
            + 0.20 * preMap[longland::ResourceId::Sustain]);
        const double oldAfter = longland::unit(
            0.50 * postMap[longland::ResourceId::Repetition]
            + 0.30 * postMap[longland::ResourceId::Tail]
            + 0.20 * postMap[longland::ResourceId::Sustain]);
        const double consumption = frameModification * longland::unit(
            std::max(0.0, oldBefore - oldAfter) / std::max(0.05, oldBefore));

        longland::Phase6Resources phase6Resources;
        phase6Resources.habitat = phase5Resources;
        const double measuredRepetition = std::max({
            sourceMap[longland::ResourceId::Repetition],
            hostMap[longland::ResourceId::Repetition],
            preMap[longland::ResourceId::Repetition]});
        phase6Resources.measuredRepetition = measuredRepetition;
        phase6Resources.stableSustain = longland::unit(
            preMap[longland::ResourceId::Sustain]
            * (1.0 - 0.5 * preMap[longland::ResourceId::DynamicContrast]));
        phase6Resources.oldBloomTail = longland::unit(
            bloomAmount * hostMap[longland::ResourceId::Tail]);
        phase6Resources.oldScavengerTail = longland::unit(
            scavengerAmount * scavengerMap[longland::ResourceId::Tail]);
        phase6Resources.rapidlyChangingFreshMaterial = longland::unit(
            0.65 * sourceMap[longland::ResourceId::Transient]
            + 0.35 * sourceMap[longland::ResourceId::DynamicContrast]);
        phase6Resources.preGranularTransient = preMap[longland::ResourceId::Transient];
        phase6Resources.postGranularTransient = postMap[longland::ResourceId::Transient];
        phase6Resources.measuredGranularModification = frameModification;
        phase6Resources.measuredOldMaterialConsumption = consumption;
        active.step(phase6Resources, controls, 0.02);

        auto bypassResources = phase6Resources;
        bypassResources.postGranularTransient = bypassResources.preGranularTransient;
        bypassResources.measuredGranularModification = 0.0;
        bypassResources.measuredOldMaterialConsumption = 0.0;
        bypassed.step(bypassResources, controls, 0.02);

        const auto& after = active.telemetry();
        const auto& bypassAfter = bypassed.telemetry();
        maximumPopulation = std::max(maximumPopulation, after.decomposer.population);
        maximumConsumption = std::max(maximumConsumption, after.decomposer.measuredConsumption);
        maximumFragments = std::max(maximumFragments, after.decomposer.generatedTransientResource);
        maximumFood = std::max(maximumFood, after.decomposer.foodLevel);
        maximumDspIntensity = std::max(maximumDspIntensity, after.decomposer.dspIntensity);
        maximumSourceRepetition = std::max(maximumSourceRepetition,
            sourceMap[longland::ResourceId::Repetition]);
        maximumHostRepetition = std::max(maximumHostRepetition,
            hostMap[longland::ResourceId::Repetition]);
        maximumTailRepetition = std::max(maximumTailRepetition,
            preMap[longland::ResourceId::Repetition]);
        maximumTailSustain = std::max(maximumTailSustain,
            preMap[longland::ResourceId::Sustain]);
        maximumOldBloomTail = std::max(maximumOldBloomTail, phase6Resources.oldBloomTail);
        maximumOldScavengerTail = std::max(maximumOldScavengerTail, phase6Resources.oldScavengerTail);
        maximumBypassPressure = std::max(maximumBypassPressure,
            bypassAfter.decomposer.habitatPressure);
        maximumBypassFragments = std::max(maximumBypassFragments,
            bypassAfter.decomposer.generatedTransientResource);
        maximumPredatorDivergence = std::max(maximumPredatorDivergence, std::abs(
            after.habitat.habitat.predatorBloom.predatorPopulation
            - bypassAfter.habitat.habitat.predatorBloom.predatorPopulation));
        maximumScavengerDivergence = std::max(maximumScavengerDivergence, std::abs(
            after.habitat.habitat.scavenger.population
            - bypassAfter.habitat.habitat.scavenger.population));
        maximumBloomDivergence = std::max(maximumBloomDivergence, std::abs(
            after.habitat.habitat.predatorBloom.preyPopulation
            - bypassAfter.habitat.habitat.predatorBloom.preyPopulation));

        if (frame < 20u * framesPerSecond || (frame >= 65u * framesPerSecond
                                                && frame < 85u * framesPerSecond))
        {
            freshRepetitionSum += measuredRepetition;
            ++freshRepetitionFrames;
        }
        else
        {
            repeatedRepetitionSum += measuredRepetition;
            ++repeatedRepetitionFrames;
        }
        if (firstGrowthFrame == totalFrames && after.decomposer.population > 0.08)
            firstGrowthFrame = frame;
        if (firstPredatorDivergenceFrame == totalFrames
            && maximumPredatorDivergence > 0.002)
            firstPredatorDivergenceFrame = frame;
        if (frame + 1 == 20u * framesPerSecond) populationAt20 = after.decomposer.population;
        if (frame + 1 == 65u * framesPerSecond) populationAt65 = after.decomposer.population;
        if (frame + 1 == 85u * framesPerSecond) populationAt85 = after.decomposer.population;
        if (frame + 1 == 110u * framesPerSecond) populationAt110 = after.decomposer.population;
    }

    const double freshRepetition = freshRepetitionSum
        / std::max<std::size_t>(1, freshRepetitionFrames);
    const double repeatedRepetition = repeatedRepetitionSum
        / std::max<std::size_t>(1, repeatedRepetitionFrames);
    const double modificationRatio = std::sqrt(modificationEnergy
        / std::max(1.0e-12, preEnergyTotal));
    test.expect(repeatedRepetition > freshRepetition + 0.03,
                "measured repeated-tail resource must exceed fresh material");
    test.expect(populationAt65 > populationAt20 + 0.10 && maximumPopulation > 0.15,
                "repeated real tail audio must grow Decomposer materially");
    test.expect(populationAt85 < 0.70 * populationAt65,
                "fresh changing material must starve Decomposer");
    test.expect(populationAt110 > populationAt85 + 0.05,
                "Decomposer must recover when repeated old material returns");
    test.expect(modificationRatio > 0.03,
                "Decomposer must create a gain-matched audible modification");
    test.expect(maximumConsumption > 0.005,
                "granular audio must measurably consume old material");
    test.expect(maximumFragments > 0.003,
                "granular audio must create measured transient fragments");
    test.expect(maximumPredatorDivergence > 0.005,
                "measured fragments must change Predator trajectory");
    test.expect(maximumScavengerDivergence > 0.005 && maximumBloomDivergence > 0.003,
                "physical tail consumption must change Scavenger and Bloom trajectories");
    test.expect(maximumBypassPressure == 0.0 && maximumBypassFragments == 0.0,
                "DSP bypass must create exactly zero ecological attack or fragments");
    test.expect(firstGrowthFrame < firstPredatorDivergenceFrame,
                "Decomposer growth must causally precede Predator divergence");
    test.expect(maximumOutput < 1.2 && nonFinite == 0,
                "Phase 6 integrated granular output must remain finite with headroom");

    std::cout << "Phase 6 audio metrics: fresh_rep=" << freshRepetition
              << " repeated_rep=" << repeatedRepetition
              << " pop20=" << populationAt20 << " pop65=" << populationAt65
              << " pop85=" << populationAt85 << " pop110=" << populationAt110
              << " pop_max=" << maximumPopulation
              << " modification=" << modificationRatio
              << " consumption=" << maximumConsumption
              << " fragments=" << maximumFragments
              << " food=" << maximumFood << " dsp=" << maximumDspIntensity
              << " source_rep_max=" << maximumSourceRepetition
              << " host_rep_max=" << maximumHostRepetition
              << " tail_rep_max=" << maximumTailRepetition
              << " tail_sustain_max=" << maximumTailSustain
              << " bloom_tail_max=" << maximumOldBloomTail
              << " scav_tail_max=" << maximumOldScavengerTail
              << " predator_delta=" << maximumPredatorDivergence
              << " scavenger_delta=" << maximumScavengerDivergence
              << " bloom_delta=" << maximumBloomDivergence
              << " peak=" << maximumOutput << '\n';
    if (test.failures != 0)
        return 1;
    std::cout << "PASS: Phase 6 real-audio repetition dependence and physical feedback\n";
    return 0;
}
