#pragma once

#include "analysis/ResourceAnalyzer.h"
#include "dsp/BloomProcessor.h"
#include "dsp/DecomposerProcessor.h"
#include "dsp/GrazerProcessor.h"
#include "dsp/ParasiteProcessor.h"
#include "dsp/PredatorProcessor.h"
#include "dsp/ScavengerProcessor.h"
#include "ecology/Phase6EcologyModel.h"
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

class LonglandEcologyAudioProcessor final : public juce::AudioProcessor
{
public:
    LonglandEcologyAudioProcessor();
    ~LonglandEcologyAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 24.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    double bloomPopulation() const noexcept { return bloomSnapshot.load(std::memory_order_relaxed); }
    double predatorPopulation() const noexcept { return predatorSnapshot.load(std::memory_order_relaxed); }
    double bloomFood() const noexcept { return bloomFoodSnapshot.load(std::memory_order_relaxed); }
    double predatorFood() const noexcept { return predatorFoodSnapshot.load(std::memory_order_relaxed); }
    double scavengerPopulation() const noexcept { return scavengerSnapshot.load(std::memory_order_relaxed); }
    double grazerPopulation() const noexcept { return grazerSnapshot.load(std::memory_order_relaxed); }
    double parasitePopulation() const noexcept { return parasiteSnapshot.load(std::memory_order_relaxed); }
    double scavengerFood() const noexcept { return scavengerFoodSnapshot.load(std::memory_order_relaxed); }
    double grazerFood() const noexcept { return grazerFoodSnapshot.load(std::memory_order_relaxed); }
    double parasiteFood() const noexcept { return parasiteFoodSnapshot.load(std::memory_order_relaxed); }
    double parasitePressure() const noexcept { return parasitePressureSnapshot.load(std::memory_order_relaxed); }
    double decomposerPopulation() const noexcept { return decomposerSnapshot.load(std::memory_order_relaxed); }
    double decomposerFood() const noexcept { return decomposerFoodSnapshot.load(std::memory_order_relaxed); }
    double decomposerConsumption() const noexcept { return decomposerConsumptionSnapshot.load(std::memory_order_relaxed); }
    double decomposerFragments() const noexcept { return decomposerFragmentsSnapshot.load(std::memory_order_relaxed); }
    int grazerTargetBand() const noexcept { return grazerBandSnapshot.load(std::memory_order_relaxed); }
    double grazerAttenuationDb() const noexcept { return grazerAttenuationSnapshot.load(std::memory_order_relaxed); }
    double attackInfluence() const noexcept { return attackSnapshot.load(std::memory_order_relaxed); }
    double inputActivity() const noexcept { return activitySnapshot.load(std::memory_order_relaxed); }
    double transientResource() const noexcept { return transientSnapshot.load(std::memory_order_relaxed); }
    double silenceResource() const noexcept { return silenceSnapshot.load(std::memory_order_relaxed); }
    double sparsityResource() const noexcept { return sparsitySnapshot.load(std::memory_order_relaxed); }
    double tailResource() const noexcept { return tailSnapshot.load(std::memory_order_relaxed); }
    double repetitionResource() const noexcept { return repetitionSnapshot.load(std::memory_order_relaxed); }
    double highResource() const noexcept { return highSnapshot.load(std::memory_order_relaxed); }
    double sustainResource() const noexcept { return sustainSnapshot.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    longland::ResourceAnalyzer sourceAnalyzer;
    longland::ResourceAnalyzer habitatAnalyzer;
    longland::ResourceAnalyzer bloomAnalyzer;
    longland::ResourceAnalyzer predatorAnalyzer;
    longland::ResourceAnalyzer grazedPredatorAnalyzer;
    longland::ResourceAnalyzer scavengerAnalyzer;
    longland::ResourceAnalyzer parasiteAnalyzer;
    longland::ResourceAnalyzer decomposerInputAnalyzer;
    longland::ResourceAnalyzer decomposerOutputAnalyzer;
    longland::Phase6EcologyModel ecology;
    std::array<longland::PredatorProcessor, 2> predatorProcessors;
    longland::BloomProcessor bloomProcessor;
    longland::ScavengerProcessor scavengerProcessor;
    longland::GrazerProcessor grazerProcessor;
    longland::ParasiteProcessor parasiteProcessor;
    longland::DecomposerProcessor decomposerProcessor;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> alignedDryBuffer;
    juce::AudioBuffer<float> bloomBuffer;
    juce::AudioBuffer<float> predatorBuffer;
    juce::AudioBuffer<float> grazedPredatorBuffer;
    juce::AudioBuffer<float> scavengerBuffer;
    juce::AudioBuffer<float> parasitizedBloomBuffer;
    juce::AudioBuffer<float> decomposerInputBuffer;
    juce::AudioBuffer<float> decomposerOutputBuffer;
    juce::AudioBuffer<float> ecosystemBaseBuffer;
    juce::AudioBuffer<float> habitatBuffer;
    std::array<std::array<float, longland::PredatorProcessor::latencySamples>, 2> alignmentDelay {};
    std::size_t alignmentWrite {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryWetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> habitatDryWeightSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> predatorWeightSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bloomWeightSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> scavengerWeightSmoothed;
    double currentSampleRate {48000.0};
    int controlFrameSamples {960};
    int samplesUntilControlTick {960};
    double bloomEnergyAccumulator {};
    std::size_t bloomSamplesAccumulator {};
    double sourceEnergyAccumulator {};
    std::size_t sourceSamplesAccumulator {};
    double hostPreEnergyAccumulator {};
    double hostPostEnergyAccumulator {};
    double hostCrossAccumulator {};
    double decomposerPreEnergyAccumulator {};
    double decomposerPostEnergyAccumulator {};
    double decomposerCrossAccumulator {};
    std::atomic<double> bloomSnapshot {0.45};
    std::atomic<double> predatorSnapshot {0.05};
    std::atomic<double> scavengerSnapshot {0.08};
    std::atomic<double> grazerSnapshot {0.04};
    std::atomic<double> parasiteSnapshot {0.008};
    std::atomic<double> decomposerSnapshot {0.012};
    std::atomic<double> bloomFoodSnapshot {};
    std::atomic<double> predatorFoodSnapshot {};
    std::atomic<double> scavengerFoodSnapshot {};
    std::atomic<double> grazerFoodSnapshot {};
    std::atomic<double> parasiteFoodSnapshot {};
    std::atomic<double> parasitePressureSnapshot {};
    std::atomic<double> decomposerFoodSnapshot {};
    std::atomic<double> decomposerConsumptionSnapshot {};
    std::atomic<double> decomposerFragmentsSnapshot {};
    std::atomic<int> grazerBandSnapshot {};
    std::atomic<double> grazerAttenuationSnapshot {};
    std::atomic<double> attackSnapshot {};
    std::atomic<double> activitySnapshot {};
    std::atomic<double> transientSnapshot {};
    std::atomic<double> silenceSnapshot {};
    std::atomic<double> sparsitySnapshot {};
    std::atomic<double> tailSnapshot {};
    std::atomic<double> repetitionSnapshot {};
    std::atomic<double> highSnapshot {};
    std::atomic<double> sustainSnapshot {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LonglandEcologyAudioProcessor)
};
