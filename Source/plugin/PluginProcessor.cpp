#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "core/MathSafety.h"
#include <algorithm>
#include <cmath>

namespace
{
float safeParameter(const std::atomic<float>* parameter,
                    const float fallback,
                    const float minimum,
                    const float maximum) noexcept
{
    const float value = parameter != nullptr ? parameter->load() : fallback;
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

float softSafetyLimit(const double value) noexcept
{
    if (!std::isfinite(value))
        return 0.0f;
    const double magnitude = std::abs(value);
    if (magnitude <= 0.95)
        return static_cast<float>(value);
    const double limited = 0.95 + 0.05 * std::tanh((magnitude - 0.95) / 0.05);
    return static_cast<float>(std::copysign(limited, value));
}
}

LonglandEcologyAudioProcessor::LonglandEcologyAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "STATE", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout LonglandEcologyAudioProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"resources", 1}, "Resources", Range {0.0f, 1.0f}, 0.98f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"stability", 1}, "Stability", Range {0.0f, 1.0f}, 0.18f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"populationSpeed", 1}, "Population Speed", Range {0.0f, 1.0f}, 0.65f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"dryWet", 1}, "Dry/Wet", Range {0.0f, 1.0f}, 0.72f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {"output", 1}, "Output", Range {-24.0f, 6.0f}, 0.0f));
    return layout;
}

void LonglandEcologyAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto channels = static_cast<std::size_t>(std::max(getTotalNumInputChannels(), 1));
    sourceAnalyzer.prepare({sampleRate, 50.0, channels});
    habitatAnalyzer.prepare({sampleRate, 50.0, channels});
    bloomAnalyzer.prepare({sampleRate, 50.0, channels});
    predatorAnalyzer.prepare({sampleRate, 50.0, channels});
    grazedPredatorAnalyzer.prepare({sampleRate, 50.0, channels});
    scavengerAnalyzer.prepare({sampleRate, 50.0, channels});
    parasiteAnalyzer.prepare({sampleRate, 50.0, channels});
    decomposerInputAnalyzer.prepare({sampleRate, 50.0, channels});
    decomposerOutputAnalyzer.prepare({sampleRate, 50.0, channels});
    ecology.reset();
    for (auto& processor : predatorProcessors)
        processor.prepare(sampleRate);
    bloomProcessor.prepare(sampleRate);
    scavengerProcessor.prepare(sampleRate);
    grazerProcessor.prepare(sampleRate);
    parasiteProcessor.prepare(sampleRate);
    decomposerProcessor.setRandomSeed(0x4c45434fu);
    decomposerProcessor.prepare(sampleRate);
    const int scratchChannels = std::max(2, static_cast<int>(channels));
    const int scratchSamples = std::max(samplesPerBlock, 16384);
    dryBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    alignedDryBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    bloomBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    predatorBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    grazedPredatorBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    scavengerBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    parasitizedBloomBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    decomposerInputBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    decomposerOutputBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    ecosystemBaseBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    habitatBuffer.setSize(scratchChannels, scratchSamples, false, false, true);
    alignmentDelay = {};
    alignmentWrite = 0;
    setLatencySamples(static_cast<int>(longland::PredatorProcessor::latencySamples));
    const float initialWet = safeParameter(parameters.getRawParameterValue("dryWet"), 0.72f, 0.0f, 1.0f);
    const float initialOutput = juce::Decibels::decibelsToGain(
        safeParameter(parameters.getRawParameterValue("output"), 0.0f, -24.0f, 6.0f));
    for (auto* smoother : {&dryWetSmoothed, &outputGainSmoothed, &habitatDryWeightSmoothed,
                           &predatorWeightSmoothed, &bloomWeightSmoothed,
                           &scavengerWeightSmoothed})
        smoother->reset(sampleRate, 0.02);
    dryWetSmoothed.setCurrentAndTargetValue(initialWet);
    outputGainSmoothed.setCurrentAndTargetValue(initialOutput);
    habitatDryWeightSmoothed.setCurrentAndTargetValue(1.0f);
    predatorWeightSmoothed.setCurrentAndTargetValue(0.0f);
    bloomWeightSmoothed.setCurrentAndTargetValue(0.0f);
    scavengerWeightSmoothed.setCurrentAndTargetValue(0.0f);
    controlFrameSamples = std::max(1, static_cast<int>(std::llround(sampleRate / 50.0)));
    samplesUntilControlTick = controlFrameSamples;
    bloomEnergyAccumulator = 0.0;
    bloomSamplesAccumulator = 0;
    sourceEnergyAccumulator = 0.0;
    sourceSamplesAccumulator = 0;
    hostPreEnergyAccumulator = 0.0;
    hostPostEnergyAccumulator = 0.0;
    hostCrossAccumulator = 0.0;
    decomposerPreEnergyAccumulator = 0.0;
    decomposerPostEnergyAccumulator = 0.0;
    decomposerCrossAccumulator = 0.0;
}

void LonglandEcologyAudioProcessor::releaseResources()
{
}

bool LonglandEcologyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && output == layouts.getMainInputChannelSet();
}

void LonglandEcologyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    if (samples > dryBuffer.getNumSamples() || channels > dryBuffer.getNumChannels())
    {
        // Never allocate on the audio thread. In the unlikely event that a
        // host exceeds the prepared scratch contract, retain the declared
        // latency and finite-output guarantee while safely bypassing ecology.
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < std::min(channels, 2); ++channel)
            {
                const float source = std::clamp(std::isfinite(buffer.getSample(channel, sample))
                    ? buffer.getSample(channel, sample) : 0.0f, -8.0f, 8.0f);
                const auto index = static_cast<std::size_t>(channel);
                const float aligned = alignmentDelay[index][alignmentWrite];
                alignmentDelay[index][alignmentWrite] = source;
                buffer.setSample(channel, sample, softSafetyLimit(aligned));
            }
            alignmentWrite = (alignmentWrite + 1) % longland::PredatorProcessor::latencySamples;
        }
        for (int channel = 2; channel < channels; ++channel)
            buffer.clear(channel, 0, samples);
        return;
    }

    for (int sample = 0; sample < samples; ++sample)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            const float source = std::clamp(std::isfinite(buffer.getSample(channel, sample))
                                                ? buffer.getSample(channel, sample) : 0.0f,
                                            -8.0f, 8.0f);
            dryBuffer.setSample(channel, sample, source);
            alignedDryBuffer.setSample(channel, sample,
                                       alignmentDelay[static_cast<std::size_t>(channel)][alignmentWrite]);
            alignmentDelay[static_cast<std::size_t>(channel)][alignmentWrite] = source;
        }
        alignmentWrite = (alignmentWrite + 1) % longland::PredatorProcessor::latencySamples;
    }
    if (channels == 1)
    {
        dryBuffer.copyFrom(1, 0, dryBuffer, 0, 0, samples);
        alignedDryBuffer.copyFrom(1, 0, alignedDryBuffer, 0, 0, samples);
    }

    dryWetSmoothed.setTargetValue(safeParameter(parameters.getRawParameterValue("dryWet"), 0.72f, 0.0f, 1.0f));
    outputGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(
        safeParameter(parameters.getRawParameterValue("output"), 0.0f, -24.0f, 6.0f)));
    int offset = 0;
    while (offset < samples)
    {
        const int segment = std::min(samples - offset, samplesUntilControlTick);
        const auto& phase6State = ecology.telemetry();
        const auto& phase5State = phase6State.habitat;
        const auto& phase4State = phase5State.habitat;
        const auto& ecologyState = phase4State.predatorBloom;
        // The ecology engine deliberately exposes a conservative smoothstep
        // activity. A square-root perceptual curve makes seed populations
        // audible without turning population into a linear wet control.
        const double predatorIntensity = std::sqrt(longland::smoothStep(
            ecologyState.predatorPopulation * ecologyState.predatorHealth));
        const double bloomIntensity = std::sqrt(longland::smoothStep(
            ecologyState.preyPopulation * ecologyState.preyHealth));
        const double scavengerIntensity = std::pow(longland::unit(
            phase4State.scavenger.population * phase4State.scavenger.health), 0.35);
        const double grazerIntensity = std::pow(longland::unit(
            phase4State.grazer.population * phase4State.grazer.health), 0.35);
        const double parasiteIntensity = phase5State.parasite.dspIntensity;
        const double decomposerIntensity = phase6State.decomposer.dspIntensity;
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& predator = predatorProcessors[static_cast<std::size_t>(channel)];
            predator.setIntensity(predatorIntensity);
            predator.setDriveDb(4.0 + 26.0 * ecologyState.predatorPopulation);
            predator.setAsymmetry(0.12 + 0.58 * ecologyState.predatorPopulation);
            predator.setOutputGainDb(-1.5);
            predator.process(dryBuffer.getReadPointer(channel, offset),
                             predatorBuffer.getWritePointer(channel, offset),
                             static_cast<std::size_t>(segment));
        }
        if (channels == 1)
            predatorBuffer.copyFrom(1, offset, predatorBuffer, 0, offset, segment);

        bloomProcessor.setIntensity(static_cast<float>(bloomIntensity));
        bloomProcessor.setDecay(static_cast<float>(0.25 + 0.72 * ecologyState.preyPopulation));
        bloomProcessor.process(alignedDryBuffer.getReadPointer(0, offset), alignedDryBuffer.getReadPointer(1, offset),
                               bloomBuffer.getWritePointer(0, offset), bloomBuffer.getWritePointer(1, offset),
                               static_cast<std::size_t>(segment));

        // Parasite is an insert on its Bloom host, never an independent wet
        // branch. At zero host-dependent intensity this is an exact bypass.
        parasiteProcessor.setIntensity(static_cast<float>(parasiteIntensity));
        parasiteProcessor.setDetuneCents(static_cast<float>(6.0
            + 20.0 * phase5State.parasite.population));
        parasiteProcessor.setSmear(static_cast<float>(0.18
            + 0.72 * phase5State.parasite.health));
        parasiteProcessor.setFeedback(static_cast<float>(0.04
            + 0.22 * phase5State.parasite.population));
        parasiteProcessor.process(bloomBuffer.getReadPointer(0, offset),
                                  bloomBuffer.getReadPointer(1, offset),
                                  parasitizedBloomBuffer.getWritePointer(0, offset),
                                  parasitizedBloomBuffer.getWritePointer(1, offset),
                                  static_cast<std::size_t>(segment));

        // Grazer inhabits the Predator path: its spectral feeding is audible as
        // adaptive attenuation, not as an unrelated parallel wet return.
        grazerProcessor.setIntensity(static_cast<float>(grazerIntensity));
        grazerProcessor.process(predatorBuffer.getReadPointer(0, offset), predatorBuffer.getReadPointer(1, offset),
                                grazedPredatorBuffer.getWritePointer(0, offset),
                                grazedPredatorBuffer.getWritePointer(1, offset),
                                static_cast<std::size_t>(segment));

        // Scavenger consumes Bloom's processed branch and creates a measured
        // delayed tail which can subsequently support Bloom in the ecology.
        scavengerProcessor.setIntensity(scavengerIntensity);
        scavengerProcessor.setFeedback(0.24 + 0.58 * phase4State.scavenger.population);
        scavengerProcessor.setDelayTimeMs(125.0 + 430.0 * (1.0 - phase4State.scavenger.health));
        scavengerProcessor.setDiffusion(0.20 + 0.70 * phase4State.scavenger.health);
        for (int sample = 0; sample < segment; ++sample)
        {
            const int index = offset + sample;
            scavengerBuffer.setSample(0, index, static_cast<float>(bloomIntensity)
                * parasitizedBloomBuffer.getSample(0, index));
            scavengerBuffer.setSample(1, index, static_cast<float>(bloomIntensity)
                * parasitizedBloomBuffer.getSample(1, index));
        }
        scavengerProcessor.process(scavengerBuffer.getReadPointer(0, offset),
                                   scavengerBuffer.getReadPointer(1, offset),
                                   scavengerBuffer.getWritePointer(0, offset),
                                   scavengerBuffer.getWritePointer(1, offset),
                                   static_cast<std::size_t>(segment));

        decomposerProcessor.setIntensity(static_cast<float>(decomposerIntensity));
        decomposerProcessor.setGrainSizeMilliseconds(static_cast<float>(
            64.0 - 46.0 * phase6State.decomposer.population));
        decomposerProcessor.setDensity(static_cast<float>(
            0.08 + 0.68 * phase6State.decomposer.population));
        decomposerProcessor.setScatter(static_cast<float>(
            0.55 + 0.40 * phase6State.decomposer.health));

        const double rawPredatorWeight = 0.52 * predatorIntensity;
        const double rawBloomWeight = 0.50 * bloomIntensity;
        const double rawScavengerWeight = 0.42 * std::sqrt(scavengerIntensity);
        const double totalSpeciesWeight = rawPredatorWeight + rawBloomWeight + rawScavengerWeight;
        const double normaliser = totalSpeciesWeight > 1.0 ? 1.0 / totalSpeciesWeight : 1.0;
        predatorWeightSmoothed.setTargetValue(static_cast<float>(rawPredatorWeight * normaliser));
        bloomWeightSmoothed.setTargetValue(static_cast<float>(rawBloomWeight * normaliser));
        scavengerWeightSmoothed.setTargetValue(static_cast<float>(rawScavengerWeight * normaliser));
        habitatDryWeightSmoothed.setTargetValue(static_cast<float>(std::max(0.0, 1.0 - totalSpeciesWeight)));

        // Construct the actual weighted tail once. Decomposer is a serial
        // insert on Bloom + Scavenger habitat, not an additional wet branch.
        for (int sample = 0; sample < segment; ++sample)
        {
            const float predatorWeight = predatorWeightSmoothed.getNextValue();
            const float bloomWeight = bloomWeightSmoothed.getNextValue();
            const float scavengerWeight = scavengerWeightSmoothed.getNextValue();
            const float habitatDryWeight = habitatDryWeightSmoothed.getNextValue();
            for (int channel = 0; channel < 2; ++channel)
            {
                const int index = offset + sample;
                const float drySample = alignedDryBuffer.getSample(channel, index);
                ecosystemBaseBuffer.setSample(channel, index,
                    habitatDryWeight * drySample
                    + predatorWeight * grazedPredatorBuffer.getSample(channel, index));
                decomposerInputBuffer.setSample(channel, index,
                    bloomWeight * parasitizedBloomBuffer.getSample(channel, index)
                    + scavengerWeight * scavengerBuffer.getSample(channel, index));
            }
        }
        decomposerProcessor.process(decomposerInputBuffer.getReadPointer(0, offset),
                                    decomposerInputBuffer.getReadPointer(1, offset),
                                    decomposerOutputBuffer.getWritePointer(0, offset),
                                    decomposerOutputBuffer.getWritePointer(1, offset),
                                    static_cast<std::size_t>(segment));

        for (int sample = 0; sample < segment; ++sample)
        {
            const float wet = dryWetSmoothed.getNextValue();
            const float dry = 1.0f - wet;
            const float outputGain = outputGainSmoothed.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                const int index = offset + sample;
                const float drySample = alignedDryBuffer.getSample(channel, index);
                const double ecosystemSample = ecosystemBaseBuffer.getSample(channel, index)
                    + decomposerOutputBuffer.getSample(channel, index);
                const float safeEcosystem = softSafetyLimit(ecosystemSample);
                habitatBuffer.setSample(channel, index, safeEcosystem);
                buffer.setSample(channel, index,
                                 softSafetyLimit(outputGain * (dry * drySample + wet * safeEcosystem)));
                const double preHostSample = bloomIntensity * bloomBuffer.getSample(channel, index);
                const double bloomOrganismSample = bloomIntensity
                    * parasitizedBloomBuffer.getSample(channel, index);
                bloomEnergyAccumulator += bloomOrganismSample * bloomOrganismSample;
                ++bloomSamplesAccumulator;
                hostPreEnergyAccumulator += preHostSample * preHostSample;
                hostPostEnergyAccumulator += bloomOrganismSample * bloomOrganismSample;
                hostCrossAccumulator += preHostSample * bloomOrganismSample;
                const double decomposerPre = decomposerInputBuffer.getSample(channel, index);
                const double decomposerPost = decomposerOutputBuffer.getSample(channel, index);
                decomposerPreEnergyAccumulator += decomposerPre * decomposerPre;
                decomposerPostEnergyAccumulator += decomposerPost * decomposerPost;
                decomposerCrossAccumulator += decomposerPre * decomposerPost;
                sourceEnergyAccumulator += static_cast<double>(drySample) * drySample;
                ++sourceSamplesAccumulator;
            }
        }

        std::array<const float*, 2> sourcePointers {};
        std::array<const float*, 2> habitatPointers {};
        std::array<const float*, 2> bloomPointers {};
        std::array<const float*, 2> predatorPointers {};
        std::array<const float*, 2> grazedPredatorPointers {};
        std::array<const float*, 2> scavengerPointers {};
        std::array<const float*, 2> parasitePointers {};
        std::array<const float*, 2> decomposerInputPointers {};
        std::array<const float*, 2> decomposerOutputPointers {};
        for (int channel = 0; channel < channels; ++channel)
        {
            sourcePointers[static_cast<std::size_t>(channel)] = dryBuffer.getReadPointer(channel, offset);
            habitatPointers[static_cast<std::size_t>(channel)] = habitatBuffer.getReadPointer(channel, offset);
            bloomPointers[static_cast<std::size_t>(channel)] = bloomBuffer.getReadPointer(channel, offset);
            predatorPointers[static_cast<std::size_t>(channel)] = predatorBuffer.getReadPointer(channel, offset);
            grazedPredatorPointers[static_cast<std::size_t>(channel)] = grazedPredatorBuffer.getReadPointer(channel, offset);
            scavengerPointers[static_cast<std::size_t>(channel)] = scavengerBuffer.getReadPointer(channel, offset);
            parasitePointers[static_cast<std::size_t>(channel)] = parasitizedBloomBuffer.getReadPointer(channel, offset);
            decomposerInputPointers[static_cast<std::size_t>(channel)] = decomposerInputBuffer.getReadPointer(channel, offset);
            decomposerOutputPointers[static_cast<std::size_t>(channel)] = decomposerOutputBuffer.getReadPointer(channel, offset);
        }
        sourceAnalyzer.processBlock(sourcePointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        habitatAnalyzer.processBlock(habitatPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        bloomAnalyzer.processBlock(bloomPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        predatorAnalyzer.processBlock(predatorPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        grazedPredatorAnalyzer.processBlock(grazedPredatorPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        scavengerAnalyzer.processBlock(scavengerPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        parasiteAnalyzer.processBlock(parasitePointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        decomposerInputAnalyzer.processBlock(decomposerInputPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        decomposerOutputAnalyzer.processBlock(decomposerOutputPointers.data(), static_cast<std::size_t>(channels), static_cast<std::size_t>(segment));
        offset += segment;
        samplesUntilControlTick -= segment;

        if (samplesUntilControlTick == 0)
        {
            const auto& sourceResources = sourceAnalyzer.latest().resources;
            const auto& bloomResources = bloomAnalyzer.latest().resources;
            const auto& predatorResources = predatorAnalyzer.latest().resources;
            const auto& grazedPredatorResources = grazedPredatorAnalyzer.latest().resources;
            const auto& scavengerResources = scavengerAnalyzer.latest().resources;
            const auto& parasiteResources = parasiteAnalyzer.latest().resources;
            const auto& decomposerInputResources = decomposerInputAnalyzer.latest().resources;
            const auto& decomposerOutputResources = decomposerOutputAnalyzer.latest().resources;
            const double bloomFood = longland::unit(0.55 * sourceResources[longland::ResourceId::Silence]
                                                   + 0.45 * sourceResources[longland::ResourceId::Sparsity]);
            const double bloomRms = std::sqrt(bloomEnergyAccumulator
                                            / std::max<std::size_t>(1, bloomSamplesAccumulator));
            const double sourceRms = std::sqrt(sourceEnergyAccumulator
                                             / std::max<std::size_t>(1, sourceSamplesAccumulator));
            const double measuredBranchRatio = longland::unit(
                1.2 * bloomRms / std::max(0.02, sourceRms));
            const double measuredBloomActivity = measuredBranchRatio;

            longland::PredatorPreyControls controls;
            controls.resources = safeParameter(parameters.getRawParameterValue("resources"), 0.98f, 0.0f, 1.0f);
            controls.stability = safeParameter(parameters.getRawParameterValue("stability"), 0.18f, 0.0f, 1.0f);
            controls.populationSpeed = safeParameter(parameters.getRawParameterValue("populationSpeed"), 0.65f, 0.0f, 1.0f);
            longland::Phase4Resources habitatResources;
            habitatResources.bloomFood = bloomFood;
            habitatResources.measuredBloomActivity = measuredBloomActivity;
            habitatResources.transientEnergy = longland::unit(
                4.0 * sourceResources[longland::ResourceId::Transient]);
            habitatResources.tailEnergy = longland::unit(bloomIntensity
                * parasiteResources[longland::ResourceId::Tail]);
            habitatResources.repetition = longland::unit(bloomIntensity
                * parasiteResources[longland::ResourceId::Repetition]);
            habitatResources.scavengerTail = longland::unit(scavengerIntensity
                * scavengerResources[longland::ResourceId::Tail]);
            habitatResources.highEnergy = longland::unit(predatorIntensity
                * predatorResources[longland::ResourceId::High]);
            habitatResources.grazedHighEnergy = longland::unit(predatorIntensity
                * grazedPredatorResources[longland::ResourceId::High]);
            habitatResources.spectralDominance = longland::unit(predatorIntensity
                * predatorResources[longland::ResourceId::SpectralDominance]);
            const double hostPreRms = std::sqrt(hostPreEnergyAccumulator
                / std::max<std::size_t>(1, bloomSamplesAccumulator));
            const double gainMatch = std::sqrt(hostPreEnergyAccumulator
                / std::max(1.0e-12, hostPostEnergyAccumulator));
            const double matchedDifferenceEnergy = std::max(0.0,
                hostPreEnergyAccumulator
                + gainMatch * gainMatch * hostPostEnergyAccumulator
                - 2.0 * gainMatch * hostCrossAccumulator);
            const double matchedDifferenceRms = std::sqrt(matchedDifferenceEnergy
                / std::max<std::size_t>(1, bloomSamplesAccumulator));
            longland::Phase5Resources phase5Resources;
            phase5Resources.habitat = habitatResources;
            phase5Resources.measuredHostActivity = longland::unit(
                hostPreRms / std::max(0.02, sourceRms));
            phase5Resources.stableSustain = longland::unit(bloomIntensity
                * bloomResources[longland::ResourceId::Sustain]
                * (1.0 - 0.5 * bloomResources[longland::ResourceId::DynamicContrast]));
            phase5Resources.hostTail = longland::unit(bloomIntensity
                * bloomResources[longland::ResourceId::Tail]);
            phase5Resources.measuredModification = longland::unit(
                1.5 * matchedDifferenceRms / std::max(0.02, hostPreRms));
            const double decomposerPreRms = std::sqrt(decomposerPreEnergyAccumulator
                / std::max<std::size_t>(1, bloomSamplesAccumulator));
            const double decomposerGainMatch = std::sqrt(decomposerPreEnergyAccumulator
                / std::max(1.0e-12, decomposerPostEnergyAccumulator));
            const double decomposerDifferenceEnergy = std::max(0.0,
                decomposerPreEnergyAccumulator
                + decomposerGainMatch * decomposerGainMatch * decomposerPostEnergyAccumulator
                - 2.0 * decomposerGainMatch * decomposerCrossAccumulator);
            const double decomposerDifferenceRms = std::sqrt(decomposerDifferenceEnergy
                / std::max<std::size_t>(1, bloomSamplesAccumulator));
            const double decomposerModification = longland::unit(
                1.5 * decomposerDifferenceRms / std::max(0.02, decomposerPreRms));
            const double oldMaterialBefore = longland::unit(
                0.50 * decomposerInputResources[longland::ResourceId::Repetition]
                + 0.30 * decomposerInputResources[longland::ResourceId::Tail]
                + 0.20 * decomposerInputResources[longland::ResourceId::Sustain]);
            const double oldMaterialAfter = longland::unit(
                0.50 * decomposerOutputResources[longland::ResourceId::Repetition]
                + 0.30 * decomposerOutputResources[longland::ResourceId::Tail]
                + 0.20 * decomposerOutputResources[longland::ResourceId::Sustain]);
            const double measuredConsumption = decomposerModification * longland::unit(
                std::max(0.0, oldMaterialBefore - oldMaterialAfter)
                / std::max(0.05, oldMaterialBefore));
            longland::Phase6Resources phase6Resources;
            phase6Resources.habitat = phase5Resources;
            phase6Resources.measuredRepetition = std::max({
                sourceResources[longland::ResourceId::Repetition],
                parasiteResources[longland::ResourceId::Repetition],
                decomposerInputResources[longland::ResourceId::Repetition]});
            phase6Resources.stableSustain = longland::unit(
                decomposerInputResources[longland::ResourceId::Sustain]
                * (1.0 - 0.5 * decomposerInputResources[longland::ResourceId::DynamicContrast]));
            phase6Resources.oldBloomTail = longland::unit(
                bloomIntensity * parasiteResources[longland::ResourceId::Tail]);
            phase6Resources.oldScavengerTail = longland::unit(
                scavengerIntensity * scavengerResources[longland::ResourceId::Tail]);
            phase6Resources.rapidlyChangingFreshMaterial = longland::unit(
                0.65 * sourceResources[longland::ResourceId::Transient]
                + 0.35 * sourceResources[longland::ResourceId::DynamicContrast]);
            phase6Resources.preGranularTransient = decomposerInputResources[longland::ResourceId::Transient];
            phase6Resources.postGranularTransient = decomposerOutputResources[longland::ResourceId::Transient];
            phase6Resources.measuredGranularModification = decomposerModification;
            phase6Resources.measuredOldMaterialConsumption = measuredConsumption;
            ecology.step(phase6Resources, controls, controlFrameSamples / currentSampleRate);
            bloomFoodSnapshot.store(bloomFood, std::memory_order_relaxed);
            bloomEnergyAccumulator = 0.0;
            bloomSamplesAccumulator = 0;
            sourceEnergyAccumulator = 0.0;
            sourceSamplesAccumulator = 0;
            hostPreEnergyAccumulator = 0.0;
            hostPostEnergyAccumulator = 0.0;
            hostCrossAccumulator = 0.0;
            decomposerPreEnergyAccumulator = 0.0;
            decomposerPostEnergyAccumulator = 0.0;
            decomposerCrossAccumulator = 0.0;
            samplesUntilControlTick = controlFrameSamples;
        }
    }

    const auto& phase6State = ecology.telemetry();
    const auto& phase5State = phase6State.habitat;
    const auto& phase4State = phase5State.habitat;
    const auto& state = phase4State.predatorBloom;
    bloomSnapshot.store(state.preyPopulation, std::memory_order_relaxed);
    predatorSnapshot.store(state.predatorPopulation, std::memory_order_relaxed);
    scavengerSnapshot.store(phase4State.scavenger.population, std::memory_order_relaxed);
    grazerSnapshot.store(phase4State.grazer.population, std::memory_order_relaxed);
    parasiteSnapshot.store(phase5State.parasite.population, std::memory_order_relaxed);
    decomposerSnapshot.store(phase6State.decomposer.population, std::memory_order_relaxed);
    predatorFoodSnapshot.store(state.filteredPredatorFood, std::memory_order_relaxed);
    scavengerFoodSnapshot.store(phase4State.scavenger.foodLevel, std::memory_order_relaxed);
    grazerFoodSnapshot.store(phase4State.grazer.foodLevel, std::memory_order_relaxed);
    parasiteFoodSnapshot.store(phase5State.parasite.foodLevel, std::memory_order_relaxed);
    parasitePressureSnapshot.store(phase5State.parasite.hostPressure, std::memory_order_relaxed);
    decomposerFoodSnapshot.store(phase6State.decomposer.foodLevel, std::memory_order_relaxed);
    decomposerConsumptionSnapshot.store(phase6State.decomposer.measuredConsumption, std::memory_order_relaxed);
    decomposerFragmentsSnapshot.store(phase6State.decomposer.generatedTransientResource, std::memory_order_relaxed);
    const auto grazerTelemetry = grazerProcessor.telemetry();
    grazerBandSnapshot.store(static_cast<int>(grazerTelemetry.targetBand), std::memory_order_relaxed);
    grazerAttenuationSnapshot.store(grazerTelemetry.targetAttenuationDb, std::memory_order_relaxed);
    attackSnapshot.store(state.attackInfluence, std::memory_order_relaxed);
    activitySnapshot.store(sourceAnalyzer.latest().resources[longland::ResourceId::Activity], std::memory_order_relaxed);
    const auto& sourceResources = sourceAnalyzer.latest().resources;
    const auto& bloomResources = bloomAnalyzer.latest().resources;
    const auto& postHostResources = parasiteAnalyzer.latest().resources;
    transientSnapshot.store(sourceResources[longland::ResourceId::Transient], std::memory_order_relaxed);
    silenceSnapshot.store(sourceResources[longland::ResourceId::Silence], std::memory_order_relaxed);
    sparsitySnapshot.store(sourceResources[longland::ResourceId::Sparsity], std::memory_order_relaxed);
    tailSnapshot.store(postHostResources[longland::ResourceId::Tail], std::memory_order_relaxed);
    repetitionSnapshot.store(postHostResources[longland::ResourceId::Repetition], std::memory_order_relaxed);
    highSnapshot.store(predatorAnalyzer.latest().resources[longland::ResourceId::High], std::memory_order_relaxed);
    sustainSnapshot.store(bloomResources[longland::ResourceId::Sustain], std::memory_order_relaxed);
}

juce::AudioProcessorEditor* LonglandEcologyAudioProcessor::createEditor()
{
    return new LonglandEcologyEditor(*this);
}

void LonglandEcologyAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void LonglandEcologyAudioProcessor::setStateInformation(const void* data, const int bytes)
{
    if (auto xml = getXmlFromBinary(data, bytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LonglandEcologyAudioProcessor();
}
