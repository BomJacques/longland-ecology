#include "analysis/ResourceAnalyzer.h"
#include "core/MathSafety.h"
#include "dsp/BloomProcessor.h"
#include "dsp/GrazerProcessor.h"
#include "dsp/PredatorProcessor.h"
#include "dsp/ScavengerProcessor.h"
#include "ecology/Phase4EcologyModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr std::size_t controlRateHz = 50;
constexpr std::size_t frameSamples = static_cast<std::size_t>(sampleRate) / controlRateHz;
constexpr double durationSeconds = 64.0;
constexpr std::size_t totalSamples = static_cast<std::size_t>(sampleRate * durationSeconds);
constexpr double pi = 3.1415926535897932384626433832795;

static_assert(frameSamples == 960);
static_assert(totalSamples % frameSamples == 0);

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

struct Range
{
    double minimum {std::numeric_limits<double>::infinity()};
    double maximum {-std::numeric_limits<double>::infinity()};

    void include(const double value) noexcept
    {
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    double span() const noexcept { return maximum - minimum; }
};

class DeterministicNoise
{
public:
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(static_cast<double>(state) / 2147483648.0 - 1.0);
    }

private:
    std::uint32_t state {0x91e10da5u};
};

double smoothGate(const double value) noexcept
{
    const double x = std::clamp(value, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

double brightTransient(const double localTime, const float noise, const bool right) noexcept
{
    constexpr double beatLength = 0.20;
    const double age = std::fmod(localTime, beatLength);
    const auto beat = static_cast<std::size_t>(localTime / beatLength);
    const double envelope = std::exp(-28.0 * age);
    const double fundamental = (beat % 3u == 0u) ? 132.0 : 196.0;
    const double tone = std::sin(2.0 * pi * fundamental * age);
    const double bright = std::sin(2.0 * pi * (right ? 7100.0 : 6600.0) * age);
    return envelope * (0.34 * tone + 0.22 * bright + 0.14 * static_cast<double>(noise));
}

double sparsePlucks(const double time, const bool right) noexcept
{
    constexpr std::array<double, 9> starts {
        12.0, 14.25, 16.8, 20.1, 23.9, 27.0, 30.6, 33.2, 35.4};
    constexpr std::array<double, 9> frequencies {
        220.0, 329.6276, 246.9417, 392.0, 293.6648, 440.0, 196.0, 369.9944, 261.6256};
    double result = 0.0;
    for (std::size_t note = 0; note < starts.size(); ++note)
    {
        const double age = time - starts[note];
        if (age >= 0.0 && age < 1.15)
        {
            const double detune = right ? 1.0024 : 0.9987;
            const double envelope = (1.0 - std::exp(-100.0 * age)) * std::exp(-4.4 * age);
            result += envelope * (0.34 * std::sin(2.0 * pi * frequencies[note] * detune * age)
                                + 0.12 * std::sin(4.0 * pi * frequencies[note] * detune * age));
        }
    }
    return result;
}

double brightSustain(const double time, const bool right) noexcept
{
    if (time < 38.0 || time >= 50.0)
        return 0.0;
    const double age = time - 38.0;
    const double remaining = 50.0 - time;
    const double envelope = smoothGate(age / 0.4) * smoothGate(remaining / 0.8);
    const double offset = right ? 0.31 : 0.0;
    // Deliberately upper-band dominated, but still ordinary audio: Grazer food
    // must come from the Predator branch analyzer rather than a test-only value.
    return envelope * (0.42 * std::sin(2.0 * pi * 6200.0 * age + offset)
                     + 0.24 * std::sin(2.0 * pi * 8700.0 * age + 0.7 * offset));
}

Stereo makeFixture()
{
    Stereo result {std::vector<float>(totalSamples), std::vector<float>(totalSamples)};
    DeterministicNoise noise;
    for (std::size_t sample = 0; sample < totalSamples; ++sample)
    {
        const double time = static_cast<double>(sample) / sampleRate;
        double left = 0.0;
        double right = 0.0;
        if (time < 12.0)
        {
            // The continuous upper-band bed lets the initially rare Grazer
            // establish before the later sparse/tail habitat takes over.
            left = 0.64 * brightTransient(time, noise.next(), false)
                 + 0.38 * std::sin(2.0 * pi * 6200.0 * time);
            right = 0.64 * brightTransient(time, noise.next(), true)
                  + 0.38 * std::sin(2.0 * pi * 7100.0 * time + 0.27);
        }
        else if (time < 38.0)
        {
            left = sparsePlucks(time, false);
            right = sparsePlucks(time, true);
        }
        else if (time < 50.0)
        {
            left = brightSustain(time, false);
            right = brightSustain(time, true);
        }
        // The final fourteen seconds are exact silence. Bloom and Scavenger
        // remain live, so every sample heard there was created by their tails.
        result.left[sample] = static_cast<float>(std::clamp(left, -0.88, 0.88));
        result.right[sample] = static_cast<float>(std::clamp(right, -0.88, 0.88));
    }
    return result;
}

void writeU16(std::ostream& output, const std::uint16_t value)
{
    const std::array<char, 2> bytes {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ostream& output, const std::uint32_t value)
{
    const std::array<char, 4> bytes {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::int16_t toPcm16(const float value) noexcept
{
    const double bounded = std::clamp(std::isfinite(value) ? static_cast<double>(value) : 0.0,
                                      -1.0,
                                      1.0);
    return static_cast<std::int16_t>(std::lrint(bounded * (bounded < 0.0 ? 32768.0 : 32767.0)));
}

void writeWave(const std::string& path, const Stereo& audio)
{
    if (audio.left.size() != audio.right.size())
        throw std::runtime_error("channel length mismatch");
    if (audio.left.size() > (std::numeric_limits<std::uint32_t>::max() - 36u) / 4u)
        throw std::runtime_error("audio is too long for a RIFF/WAVE file");

    const auto dataBytes = static_cast<std::uint32_t>(audio.left.size() * 4u);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot open output: " + path);

    output.write("RIFF", 4);
    writeU32(output, 36u + dataBytes);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    writeU32(output, 16u);
    writeU16(output, 1u);
    writeU16(output, 2u);
    writeU32(output, 48000u);
    writeU32(output, 48000u * 4u);
    writeU16(output, 4u);
    writeU16(output, 16u);
    output.write("data", 4);
    writeU32(output, dataBytes);
    for (std::size_t sample = 0; sample < audio.left.size(); ++sample)
    {
        writeU16(output, static_cast<std::uint16_t>(toPcm16(audio.left[sample])));
        writeU16(output, static_cast<std::uint16_t>(toPcm16(audio.right[sample])));
    }
    if (!output)
        throw std::runtime_error("failed while writing output: " + path);
}

double routedIntensity(const double population, const double health) noexcept
{
    return std::sqrt(longland::smoothStep(longland::unit(population) * longland::unit(health)));
}

double auxiliaryIntensity(const double population, const double health) noexcept
{
    return std::pow(longland::unit(population) * longland::unit(health), 0.35);
}

struct RenderMetrics
{
    Range bloomPopulation;
    Range predatorPopulation;
    Range scavengerPopulation;
    Range grazerPopulation;
    Range scavengerFood;
    Range grazerFood;
    Range measuredBloomActivity;
    Range predatorFood;
    Range bloomTailResource;
    Range predatorHighResource;
    Range grazedHighResource;
    Range scavengerSupport;
    Range grazerSuppression;
    double maximumPhysicalHighReduction {};
    double maximumPredatorFoodCounterfactualDelta {};
    double maximumPredatorCounterfactualDelta {};
    double maximumBloomCounterfactualDelta {};
    double scavengerBranchEnergy {};
    double routedScavengerEnergy {};
    double maximumRoutedScavengerMagnitude {};
    double grazerDifferenceEnergy {};
    double scavengerAtPluckStart {std::numeric_limits<double>::quiet_NaN()};
    double scavengerAfterTenSecondsOfPlucks {std::numeric_limits<double>::quiet_NaN()};
    double scavengerAfterTwentySecondsOfPlucks {std::numeric_limits<double>::quiet_NaN()};
    double maximumScavengerDuringPlucks {};
    std::size_t audioSamples {};
    std::size_t nonFiniteAudio {};
    std::size_t invalidPopulations {};
};

struct RenderResult
{
    Stereo audio;
    Stereo alignedDry;
    RenderMetrics metrics;
};

void includePopulationMetrics(RenderMetrics& metrics,
                              const longland::Phase4Telemetry& state) noexcept
{
    metrics.bloomPopulation.include(state.predatorBloom.preyPopulation);
    metrics.predatorPopulation.include(state.predatorBloom.predatorPopulation);
    metrics.scavengerPopulation.include(state.scavenger.population);
    metrics.grazerPopulation.include(state.grazer.population);
    metrics.scavengerFood.include(state.scavenger.foodLevel);
    metrics.grazerFood.include(state.grazer.foodLevel);
    metrics.predatorFood.include(state.predatorBloom.filteredPredatorFood);
    metrics.scavengerSupport.include(state.scavengerBloomSupport);
    metrics.grazerSuppression.include(state.grazerPredatorSuppression);

    for (const double population : {state.predatorBloom.preyPopulation,
                                    state.predatorBloom.predatorPopulation,
                                    state.scavenger.population,
                                    state.grazer.population})
        if (!std::isfinite(population) || population < 0.0 || population > 1.0)
            ++metrics.invalidPopulations;
}

RenderResult renderEcology(const Stereo& dry)
{
    using namespace longland;
    RenderResult result {
        Stereo {std::vector<float>(totalSamples), std::vector<float>(totalSamples)},
        Stereo {std::vector<float>(totalSamples), std::vector<float>(totalSamples)}};

    for (std::size_t sample = PredatorProcessor::latencySamples; sample < totalSamples; ++sample)
    {
        result.alignedDry.left[sample] = dry.left[sample - PredatorProcessor::latencySamples];
        result.alignedDry.right[sample] = dry.right[sample - PredatorProcessor::latencySamples];
    }

    Phase4EcologyModel ecology;
    ecology.reset();
    PredatorPreyControls controls;
    controls.resources = 0.98;
    controls.stability = 0.18;
    controls.populationSpeed = 0.65;

    ResourceAnalyzer sourceAnalyzer;
    ResourceAnalyzer habitatAnalyzer;
    ResourceAnalyzer bloomAnalyzer;
    ResourceAnalyzer predatorAnalyzer;
    ResourceAnalyzer grazedPredatorAnalyzer;
    ResourceAnalyzer scavengerAnalyzer;
    for (auto* analyzer : {&sourceAnalyzer, &habitatAnalyzer, &bloomAnalyzer,
                           &predatorAnalyzer, &grazedPredatorAnalyzer, &scavengerAnalyzer})
        analyzer->prepare({sampleRate, static_cast<double>(controlRateHz), 2});

    // These models consume the same measured frame resources as the audible
    // run. Each removes exactly one relationship input, isolating causality
    // without changing the source fixture, DSP, or timing.
    Phase4EcologyModel withoutGrazerReduction;
    Phase4EcologyModel withoutScavengerSupport;
    withoutGrazerReduction.reset();
    withoutScavengerSupport.reset();

    std::array<PredatorProcessor, 2> predators;
    for (auto& predator : predators)
        predator.prepare(sampleRate);
    BloomProcessor bloom;
    bloom.prepare(sampleRate);
    ScavengerProcessor scavenger;
    scavenger.prepare(sampleRate);
    GrazerProcessor grazer;
    grazer.prepare(sampleRate);

    std::array<std::vector<float>, 2> predatorBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> grazedPredatorBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> bloomBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> scavengerBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> habitatBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};

    includePopulationMetrics(result.metrics, ecology.telemetry());

    for (std::size_t offset = 0; offset < totalSamples; offset += frameSamples)
    {
        // Render from the preceding state. Measurements below advance ecology
        // only after this complete 20 ms frame, preserving the production loop's
        // causal delay and the validated Predator/Prey core.
        const auto before = ecology.telemetry();
        const auto& core = before.predatorBloom;
        const double predatorIntensity = routedIntensity(core.predatorPopulation, core.predatorHealth);
        const double bloomIntensity = routedIntensity(core.preyPopulation, core.preyHealth);
        const double scavengerIntensity = auxiliaryIntensity(before.scavenger.population,
                                                             before.scavenger.health);
        const double grazerIntensity = auxiliaryIntensity(before.grazer.population,
                                                          before.grazer.health);

        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            auto& processor = predators[channel];
            processor.setIntensity(predatorIntensity);
            processor.setDriveDb(4.0 + 26.0 * core.predatorPopulation);
            processor.setAsymmetry(0.12 + 0.58 * core.predatorPopulation);
            processor.setOutputGainDb(-1.5);
            const float* input = channel == 0 ? dry.left.data() + offset
                                              : dry.right.data() + offset;
            processor.process(input, predatorBuffer[channel].data(), frameSamples);
        }

        bloom.setIntensity(static_cast<float>(bloomIntensity));
        bloom.setDecay(static_cast<float>(0.25 + 0.72 * core.preyPopulation));
        bloom.process(result.alignedDry.left.data() + offset,
                      result.alignedDry.right.data() + offset,
                      bloomBuffer[0].data(), bloomBuffer[1].data(), frameSamples);

        grazer.setIntensity(static_cast<float>(grazerIntensity));
        grazer.process(predatorBuffer[0].data(), predatorBuffer[1].data(),
                       grazedPredatorBuffer[0].data(), grazedPredatorBuffer[1].data(),
                       frameSamples);

        scavenger.setIntensity(scavengerIntensity);
        scavenger.setFeedback(0.24 + 0.58 * before.scavenger.population);
        scavenger.setDelayTimeMs(125.0 + 430.0 * (1.0 - before.scavenger.health));
        scavenger.setDiffusion(0.20 + 0.70 * before.scavenger.health);
        for (std::size_t sample = 0; sample < frameSamples; ++sample)
        {
            scavengerBuffer[0][sample] = static_cast<float>(bloomIntensity) * bloomBuffer[0][sample];
            scavengerBuffer[1][sample] = static_cast<float>(bloomIntensity) * bloomBuffer[1][sample];
        }
        scavenger.process(scavengerBuffer[0].data(), scavengerBuffer[1].data(),
                          scavengerBuffer[0].data(), scavengerBuffer[1].data(),
                          frameSamples);

        const double rawPredatorWeight = 0.52 * predatorIntensity;
        const double rawBloomWeight = 0.50 * bloomIntensity;
        const double rawScavengerWeight = 0.42 * std::sqrt(scavengerIntensity);
        const double totalSpeciesWeight = rawPredatorWeight + rawBloomWeight + rawScavengerWeight;
        const double normaliser = totalSpeciesWeight > 1.0 ? 1.0 / totalSpeciesWeight : 1.0;
        const float predatorWeight = static_cast<float>(rawPredatorWeight * normaliser);
        const float bloomWeight = static_cast<float>(rawBloomWeight * normaliser);
        const float scavengerWeight = static_cast<float>(rawScavengerWeight * normaliser);
        const float dryWeight = static_cast<float>(std::max(0.0, 1.0 - totalSpeciesWeight));

        double bloomEnergy = 0.0;
        double sourceEnergy = 0.0;
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            const float* aligned = channel == 0 ? result.alignedDry.left.data() + offset
                                                : result.alignedDry.right.data() + offset;
            float* destination = channel == 0 ? result.audio.left.data() + offset
                                              : result.audio.right.data() + offset;
            for (std::size_t sample = 0; sample < frameSamples; ++sample)
            {
                const double scavengerSample = scavengerBuffer[channel][sample];
                const double grazerDelta = static_cast<double>(predatorBuffer[channel][sample])
                                         - grazedPredatorBuffer[channel][sample];
                result.metrics.scavengerBranchEnergy += scavengerSample * scavengerSample;
                const double routedScavenger = scavengerWeight * scavengerSample;
                result.metrics.routedScavengerEnergy += routedScavenger * routedScavenger;
                result.metrics.maximumRoutedScavengerMagnitude = std::max(
                    result.metrics.maximumRoutedScavengerMagnitude,
                    std::abs(routedScavenger));
                result.metrics.grazerDifferenceEnergy += grazerDelta * grazerDelta;
                ++result.metrics.audioSamples;

                const double mixed = dryWeight * aligned[sample]
                                   + predatorWeight * grazedPredatorBuffer[channel][sample]
                                   + bloomWeight * bloomBuffer[channel][sample]
                                   + scavengerWeight * scavengerBuffer[channel][sample];
                const float safe = static_cast<float>(std::clamp(mixed, -0.99, 0.99));
                if (!std::isfinite(mixed) || !std::isfinite(scavengerSample)
                    || !std::isfinite(grazerDelta))
                    ++result.metrics.nonFiniteAudio;
                habitatBuffer[channel][sample] = std::isfinite(safe) ? safe : 0.0f;
                destination[sample] = habitatBuffer[channel][sample];

                const double bloomSample = bloomIntensity * bloomBuffer[channel][sample];
                bloomEnergy += bloomSample * bloomSample;
                sourceEnergy += static_cast<double>(aligned[sample]) * aligned[sample];
            }
        }

        const std::array<const float*, 2> sourcePointers {
            dry.left.data() + offset, dry.right.data() + offset};
        const std::array<const float*, 2> habitatPointers {
            habitatBuffer[0].data(), habitatBuffer[1].data()};
        const std::array<const float*, 2> bloomPointers {
            bloomBuffer[0].data(), bloomBuffer[1].data()};
        const std::array<const float*, 2> predatorPointers {
            predatorBuffer[0].data(), predatorBuffer[1].data()};
        const std::array<const float*, 2> grazedPredatorPointers {
            grazedPredatorBuffer[0].data(), grazedPredatorBuffer[1].data()};
        const std::array<const float*, 2> scavengerPointers {
            scavengerBuffer[0].data(), scavengerBuffer[1].data()};
        sourceAnalyzer.processBlock(sourcePointers.data(), 2, frameSamples);
        habitatAnalyzer.processBlock(habitatPointers.data(), 2, frameSamples);
        bloomAnalyzer.processBlock(bloomPointers.data(), 2, frameSamples);
        predatorAnalyzer.processBlock(predatorPointers.data(), 2, frameSamples);
        grazedPredatorAnalyzer.processBlock(grazedPredatorPointers.data(), 2, frameSamples);
        scavengerAnalyzer.processBlock(scavengerPointers.data(), 2, frameSamples);

        const auto& sourceResources = sourceAnalyzer.latest().resources;
        const auto& bloomResources = bloomAnalyzer.latest().resources;
        const auto& predatorResources = predatorAnalyzer.latest().resources;
        const auto& grazedPredatorResources = grazedPredatorAnalyzer.latest().resources;
        const auto& scavengerResources = scavengerAnalyzer.latest().resources;
        const double bloomFood = unit(0.55 * sourceResources[ResourceId::Silence]
                                    + 0.45 * sourceResources[ResourceId::Sparsity]);
        const double bloomRms = std::sqrt(bloomEnergy / (2.0 * frameSamples));
        const double sourceRms = std::sqrt(sourceEnergy / (2.0 * frameSamples));

        Phase4Resources resources;
        resources.bloomFood = bloomFood;
        resources.measuredBloomActivity = unit(1.2 * bloomRms / std::max(0.02, sourceRms));
        result.metrics.measuredBloomActivity.include(resources.measuredBloomActivity);
        resources.transientEnergy = unit(4.0 * sourceResources[ResourceId::Transient]);
        // These are deliberately identical to the production resource mapping;
        // this render is a proof of the real synth path, not a test-only boost.
        resources.tailEnergy = unit(bloomIntensity * bloomResources[ResourceId::Tail]);
        resources.repetition = unit(bloomIntensity * bloomResources[ResourceId::Repetition]);
        resources.scavengerTail = unit(scavengerIntensity
                                    * scavengerResources[ResourceId::Tail]);
        resources.highEnergy = unit(predatorIntensity
                                  * predatorResources[ResourceId::High]);
        resources.grazedHighEnergy = unit(predatorIntensity
                                       * grazedPredatorResources[ResourceId::High]);
        resources.spectralDominance = unit(predatorIntensity
                                         * predatorResources[ResourceId::SpectralDominance]);
        result.metrics.bloomTailResource.include(resources.tailEnergy);
        result.metrics.predatorHighResource.include(resources.highEnergy);
        result.metrics.grazedHighResource.include(resources.grazedHighEnergy);
        result.metrics.maximumPhysicalHighReduction = std::max(
            result.metrics.maximumPhysicalHighReduction,
            std::max(0.0, resources.highEnergy - resources.grazedHighEnergy));

        ecology.step(resources, controls, 1.0 / static_cast<double>(controlRateHz));
        auto noGrazerResources = resources;
        noGrazerResources.grazedHighEnergy = noGrazerResources.highEnergy;
        withoutGrazerReduction.step(noGrazerResources, controls,
                                    1.0 / static_cast<double>(controlRateHz));
        auto noScavengerSupportResources = resources;
        noScavengerSupportResources.scavengerTail = 0.0;
        withoutScavengerSupport.step(noScavengerSupportResources, controls,
                                     1.0 / static_cast<double>(controlRateHz));

        const auto& live = ecology.telemetry().predatorBloom;
        result.metrics.maximumPredatorCounterfactualDelta = std::max(
            result.metrics.maximumPredatorCounterfactualDelta,
            std::abs(live.predatorPopulation
                   - withoutGrazerReduction.telemetry().predatorBloom.predatorPopulation));
        result.metrics.maximumPredatorFoodCounterfactualDelta = std::max(
            result.metrics.maximumPredatorFoodCounterfactualDelta,
            std::abs(live.filteredPredatorFood
                   - withoutGrazerReduction.telemetry().predatorBloom.filteredPredatorFood));
        result.metrics.maximumBloomCounterfactualDelta = std::max(
            result.metrics.maximumBloomCounterfactualDelta,
            std::abs(live.preyPopulation
                   - withoutScavengerSupport.telemetry().predatorBloom.preyPopulation));
        includePopulationMetrics(result.metrics, ecology.telemetry());

        const std::size_t completedSample = offset + frameSamples;
        const double currentScavenger = ecology.telemetry().scavenger.population;
        if (completedSample == static_cast<std::size_t>(12.0 * sampleRate))
            result.metrics.scavengerAtPluckStart = currentScavenger;
        if (completedSample == static_cast<std::size_t>(22.0 * sampleRate))
            result.metrics.scavengerAfterTenSecondsOfPlucks = currentScavenger;
        if (completedSample == static_cast<std::size_t>(32.0 * sampleRate))
            result.metrics.scavengerAfterTwentySecondsOfPlucks = currentScavenger;
        if (completedSample > static_cast<std::size_t>(12.0 * sampleRate)
            && completedSample <= static_cast<std::size_t>(38.0 * sampleRate))
            result.metrics.maximumScavengerDuringPlucks = std::max(
                result.metrics.maximumScavengerDuringPlucks, currentScavenger);
    }
    return result;
}

double differenceRms(const Stereo& a, const Stereo& b) noexcept
{
    if (a.left.size() != b.left.size() || a.right.size() != b.right.size()
        || a.left.size() != a.right.size())
        return std::numeric_limits<double>::quiet_NaN();
    double energy = 0.0;
    for (std::size_t sample = 0; sample < a.left.size(); ++sample)
    {
        const double left = static_cast<double>(a.left[sample]) - b.left[sample];
        const double right = static_cast<double>(a.right[sample]) - b.right[sample];
        energy += left * left + right * right;
    }
    return std::sqrt(energy / static_cast<double>(2u * a.left.size()));
}
} // namespace

int main(const int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: phase4_render_cli <dry.wav> <processed.wav>\n";
        return 2;
    }

    try
    {
        const auto dry = makeFixture();
        const auto rendered = renderEcology(dry);
        const auto& metrics = rendered.metrics;
        const double outputDifference = differenceRms(rendered.alignedDry, rendered.audio);
        const double scavengerRms = std::sqrt(metrics.scavengerBranchEnergy
                                             / std::max<std::size_t>(1, metrics.audioSamples));
        const double routedScavengerRms = std::sqrt(metrics.routedScavengerEnergy
                                                   / std::max<std::size_t>(1, metrics.audioSamples));
        const double grazerDifferenceRms = std::sqrt(metrics.grazerDifferenceEnergy
                                                    / std::max<std::size_t>(1, metrics.audioSamples));

        const bool finiteMetrics = std::isfinite(outputDifference)
                                && std::isfinite(scavengerRms)
                                && std::isfinite(routedScavengerRms)
                                && std::isfinite(grazerDifferenceRms);
        const bool changedAudio = outputDifference > 5.0e-3
                               && scavengerRms > 1.0e-2
                               && routedScavengerRms > 5.0e-3
                               && metrics.maximumRoutedScavengerMagnitude > 0.03
                               && grazerDifferenceRms > 1.0e-3;
        const bool scavengerResponded = metrics.bloomTailResource.maximum > 0.10
                                     && metrics.scavengerFood.maximum > 0.30
                                     && metrics.scavengerAtPluckStart < 0.055
                                     && metrics.scavengerAfterTwentySecondsOfPlucks
                                            > metrics.scavengerAtPluckStart + 0.20
                                     && metrics.maximumScavengerDuringPlucks > 0.30;
        const bool grazerResponded = metrics.predatorHighResource.maximum > 0.20
                                  && metrics.grazerFood.maximum > 0.02
                                  && metrics.grazerPopulation.maximum > 0.06;
        const bool physicalRelationships = metrics.maximumPhysicalHighReduction > 5.0e-3
                                        && metrics.maximumPredatorFoodCounterfactualDelta > 2.0e-3
                                        && metrics.maximumPredatorCounterfactualDelta > 2.0e-3
                                        && metrics.scavengerSupport.maximum > 5.0e-3
                                        && metrics.maximumBloomCounterfactualDelta > 1.0e-3;
        const bool safe = metrics.nonFiniteAudio == 0 && metrics.invalidPopulations == 0;
        if (!finiteMetrics || !changedAudio || !scavengerResponded || !grazerResponded
            || !physicalRelationships || !safe)
        {
            std::cerr << std::setprecision(9)
                      << "acceptance diagnostics: output_delta=" << outputDifference
                      << " scavenger_rms=" << scavengerRms
                      << " routed_scavenger_rms=" << routedScavengerRms
                      << " routed_scavenger_peak=" << metrics.maximumRoutedScavengerMagnitude
                      << " grazer_delta=" << grazerDifferenceRms
                      << " bloom_tail_max=" << metrics.bloomTailResource.maximum
                      << " scavenger_food_max=" << metrics.scavengerFood.maximum
                      << " scavenger_population_span=" << metrics.scavengerPopulation.span()
                      << " scavenger_at_pluck_start=" << metrics.scavengerAtPluckStart
                      << " scavenger_after_10s=" << metrics.scavengerAfterTenSecondsOfPlucks
                      << " scavenger_after_20s=" << metrics.scavengerAfterTwentySecondsOfPlucks
                      << " scavenger_pluck_max=" << metrics.maximumScavengerDuringPlucks
                      << " predator_high_max=" << metrics.predatorHighResource.maximum
                      << " bloom_activity_max=" << metrics.measuredBloomActivity.maximum
                      << " predator_food_max=" << metrics.predatorFood.maximum
                      << " grazed_high_max=" << metrics.grazedHighResource.maximum
                      << " physical_high_reduction=" << metrics.maximumPhysicalHighReduction
                      << " grazer_food_max=" << metrics.grazerFood.maximum
                      << " grazer_population_span=" << metrics.grazerPopulation.span()
                      << " predator_counterfactual_delta="
                      << metrics.maximumPredatorCounterfactualDelta
                      << " predator_food_counterfactual_delta="
                      << metrics.maximumPredatorFoodCounterfactualDelta
                      << " bloom_counterfactual_delta="
                      << metrics.maximumBloomCounterfactualDelta
                      << " predator_population_max=" << metrics.predatorPopulation.maximum
                      << " bloom_population_max=" << metrics.bloomPopulation.maximum
                      << " nonfinite_audio=" << metrics.nonFiniteAudio
                      << " invalid_populations=" << metrics.invalidPopulations << '\n';
            throw std::runtime_error("Phase 4 real-audio closed-loop acceptance thresholds failed");
        }

        writeWave(argv[1], dry);
        writeWave(argv[2], rendered.audio);

        std::cout << std::fixed << std::setprecision(6)
                  << "render: 48000 Hz, stereo PCM16, 64.0 s, 3200 exact 50 Hz causal frames\n"
                  << "fixture: [0,12) bright transients; [12,38) sparse plucks; "
                     "[38,50) bright sustain; [50,64) silence/tail observation\n"
                  << "Bloom/Scavenger: measured tail max=" << metrics.bloomTailResource.maximum
                  << ", Scavenger food max=" << metrics.scavengerFood.maximum
                  << ", population=" << metrics.scavengerPopulation.minimum << " .. "
                  << metrics.scavengerPopulation.maximum
                  << ", raw return RMS=" << scavengerRms
                  << ", routed return RMS=" << routedScavengerRms
                  << ", routed peak=" << metrics.maximumRoutedScavengerMagnitude
                  << ", Bloom support max=" << metrics.scavengerSupport.maximum << '\n'
                  << "Scavenger timing: dry-transient end=" << metrics.scavengerAtPluckStart
                  << ", +10 s plucks=" << metrics.scavengerAfterTenSecondsOfPlucks
                  << ", +20 s plucks=" << metrics.scavengerAfterTwentySecondsOfPlucks
                  << ", pluck-phase max=" << metrics.maximumScavengerDuringPlucks << '\n'
                  << "Predator/Grazer: measured high max=" << metrics.predatorHighResource.maximum
                  << ", post-Grazer high max=" << metrics.grazedHighResource.maximum
                  << ", max physical high reduction=" << metrics.maximumPhysicalHighReduction
                  << ", Grazer food max=" << metrics.grazerFood.maximum
                  << ", population=" << metrics.grazerPopulation.minimum << " .. "
                  << metrics.grazerPopulation.maximum
                  << ", branch delta RMS=" << grazerDifferenceRms
                  << ", Predator-food suppression max=" << metrics.grazerSuppression.maximum
                  << ", Predator-food counterfactual delta="
                  << metrics.maximumPredatorFoodCounterfactualDelta
                  << ", Predator counterfactual delta="
                  << metrics.maximumPredatorCounterfactualDelta << '\n'
                  << "Scavenger-support Bloom counterfactual delta="
                  << metrics.maximumBloomCounterfactualDelta << '\n'
                  << "core populations: Bloom=" << metrics.bloomPopulation.minimum << " .. "
                  << metrics.bloomPopulation.maximum << ", Predator="
                  << metrics.predatorPopulation.minimum << " .. "
                  << metrics.predatorPopulation.maximum << '\n'
                  << "aligned dry/processed difference RMS: " << outputDifference << '\n'
                  << "safety: nonfinite_audio=" << metrics.nonFiniteAudio
                  << ", invalid_populations=" << metrics.invalidPopulations << '\n'
                  << "dry: " << argv[1] << '\n'
                  << "processed: " << argv[2] << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "phase4 render failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
