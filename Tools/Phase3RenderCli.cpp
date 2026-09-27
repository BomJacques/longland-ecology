#include "analysis/ResourceAnalyzer.h"
#include "core/MathSafety.h"
#include "ecology/PredatorPreyModel.h"
#include "dsp/BloomProcessor.h"
#include "dsp/PredatorProcessor.h"

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
constexpr double durationSeconds = 40.0;
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
    std::uint32_t state {0x6d2b79f5u};
};

double smoothGate(const double value) noexcept
{
    const auto x = std::clamp(value, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

double transientVoice(const double localTime, const float noise, const bool right) noexcept
{
    constexpr double beatLength = 0.25;
    const auto beat = static_cast<std::size_t>(localTime / beatLength);
    const double age = std::fmod(localTime, beatLength);
    if ((beat & 1u) == 0u)
    {
        // Analytic downward chirp gives the alternating section a kick voice.
        const double phase = 2.0 * pi * (105.0 * age - 125.0 * age * age);
        const double body = std::sin(phase) * std::exp(-17.0 * age);
        const double click = static_cast<double>(noise) * std::exp(-85.0 * age);
        return 0.64 * body + 0.10 * click;
    }

    // Opposite beats are a short, stereo-offset noise/snare voice.
    const double toneFrequency = right ? 196.0 : 185.0;
    const double tone = std::sin(2.0 * pi * toneFrequency * age);
    return (0.28 * static_cast<double>(noise) + 0.17 * tone) * std::exp(-24.0 * age);
}

double sparsePluck(const double time, const bool right) noexcept
{
    constexpr std::array<double, 5> starts {8.0, 9.6, 12.1, 14.8, 15.55};
    constexpr std::array<double, 5> frequencies {220.0, 293.6648, 246.9417, 329.6276, 196.0};
    double sample = 0.0;
    for (std::size_t note = 0; note < starts.size(); ++note)
    {
        const double age = time - starts[note];
        if (age >= 0.0 && age < 0.72)
        {
            const double frequency = frequencies[note] * (right ? 1.003 : 1.0);
            const double envelope = (1.0 - std::exp(-120.0 * age)) * std::exp(-6.0 * age);
            sample += envelope * (0.39 * std::sin(2.0 * pi * frequency * age)
                                + 0.12 * std::sin(4.0 * pi * frequency * age));
        }
    }
    return sample;
}

double sustainedPad(const double time, const bool right) noexcept
{
    if (time < 16.0 || time >= 26.0)
        return 0.0;

    const double age = time - 16.0;
    const double remaining = 26.0 - time;
    const double envelope = smoothGate(age / 1.2) * smoothGate(remaining / 1.2);
    constexpr std::array<double, 4> chord {110.0, 138.5913, 164.8138, 220.0};
    double sample = 0.0;
    for (std::size_t voice = 0; voice < chord.size(); ++voice)
    {
        const double detune = right ? 1.0015 + 0.0002 * static_cast<double>(voice)
                                    : 0.9988 - 0.0001 * static_cast<double>(voice);
        const double phaseOffset = right ? 0.37 * static_cast<double>(voice + 1) : 0.0;
        sample += std::sin(2.0 * pi * chord[voice] * detune * age + phaseOffset);
    }
    return 0.115 * envelope * sample;
}

Stereo makeFixture()
{
    Stereo result {std::vector<float>(totalSamples), std::vector<float>(totalSamples)};
    DeterministicNoise noise;

    for (std::size_t i = 0; i < totalSamples; ++i)
    {
        const double time = static_cast<double>(i) / sampleRate;
        const float noiseLeft = noise.next();
        const float noiseRight = noise.next();
        double left = 0.0;
        double right = 0.0;

        if (time < 8.0)
        {
            left = transientVoice(time, noiseLeft, false);
            right = transientVoice(time, noiseRight, true);
        }
        else if (time < 16.0)
        {
            left = sparsePluck(time, false);
            right = sparsePluck(time, true);
        }
        else if (time < 26.0)
        {
            left = sustainedPad(time, false);
            right = sustainedPad(time, true);
        }
        else if (time < 32.0)
        {
            left = transientVoice(time - 26.0, noiseLeft, false);
            right = transientVoice(time - 26.0, noiseRight, true);
        }
        // 32 .. 40 seconds is exact digital silence. The processors remain live.

        result.left[i] = static_cast<float>(std::clamp(left, -0.9, 0.9));
        result.right[i] = static_cast<float>(std::clamp(right, -0.9, 0.9));
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
    writeU32(output, 16u);            // PCM fmt chunk size
    writeU16(output, 1u);             // integer PCM
    writeU16(output, 2u);             // stereo
    writeU32(output, 48000u);
    writeU32(output, 48000u * 4u);    // byte rate
    writeU16(output, 4u);             // block align
    writeU16(output, 16u);            // bits per sample
    output.write("data", 4);
    writeU32(output, dataBytes);
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        writeU16(output, static_cast<std::uint16_t>(toPcm16(audio.left[i])));
        writeU16(output, static_cast<std::uint16_t>(toPcm16(audio.right[i])));
    }
    if (!output)
        throw std::runtime_error("failed while writing output: " + path);
}

struct RenderResult
{
    Stereo audio;
    Range predatorPopulation;
    Range bloomPopulation;
    Range predatorIntensity;
    Range bloomIntensity;
};

double routedIntensity(const double population, const double health) noexcept
{
    return std::sqrt(longland::smoothStep(longland::unit(population) * longland::unit(health)));
}

RenderResult renderEcology(const Stereo& dry)
{
    using namespace longland;
    RenderResult result {Stereo {std::vector<float>(totalSamples), std::vector<float>(totalSamples)}};
    PredatorPreyModel ecology;
    ecology.reset(0.45, 0.05); // the validated Prey role is realised by Bloom DSP
    ResourceAnalyzer sourceAnalyzer;
    ResourceAnalyzer habitatAnalyzer;
    sourceAnalyzer.prepare({sampleRate, static_cast<double>(controlRateHz), 2});
    habitatAnalyzer.prepare({sampleRate, static_cast<double>(controlRateHz), 2});

    std::array<PredatorProcessor, 2> predators;
    for (auto& predator : predators)
        predator.prepare(sampleRate);
    BloomProcessor bloom;
    bloom.prepare(sampleRate);

    std::array<std::vector<float>, 2> predatorBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> bloomBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    std::array<std::vector<float>, 2> habitatBuffer {
        std::vector<float>(frameSamples), std::vector<float>(frameSamples)};
    Stereo alignedDry {std::vector<float>(totalSamples), std::vector<float>(totalSamples)};
    for (std::size_t i = PredatorProcessor::latencySamples; i < totalSamples; ++i)
    {
        alignedDry.left[i] = dry.left[i - PredatorProcessor::latencySamples];
        alignedDry.right[i] = dry.right[i - PredatorProcessor::latencySamples];
    }

    const auto recordRanges = [&]
    {
        const auto& state = ecology.telemetry();
        result.predatorPopulation.include(state.predatorPopulation);
        result.bloomPopulation.include(state.preyPopulation);
        result.predatorIntensity.include(routedIntensity(state.predatorPopulation, state.predatorHealth));
        result.bloomIntensity.include(routedIntensity(state.preyPopulation, state.preyHealth));
    };
    recordRanges();

    for (std::size_t offset = 0; offset < totalSamples; offset += frameSamples)
    {
        // Strictly causal: this frame is rendered only from the state produced by
        // earlier frames. Its analysis advances the ecology after audio rendering.
        const auto& state = ecology.telemetry();
        const double predatorIntensity = routedIntensity(state.predatorPopulation, state.predatorHealth);
        const double bloomIntensity = routedIntensity(state.preyPopulation, state.preyHealth);
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            auto& processor = predators[channel];
            processor.setIntensity(predatorIntensity);
            processor.setDriveDb(4.0 + 26.0 * state.predatorPopulation);
            processor.setAsymmetry(0.12 + 0.58 * state.predatorPopulation);
            processor.setOutputGainDb(-1.5);
            const float* input = channel == 0 ? dry.left.data() + offset : dry.right.data() + offset;
            processor.process(input, predatorBuffer[channel].data(), frameSamples);
        }

        bloom.setIntensity(static_cast<float>(bloomIntensity));
        bloom.setDecay(static_cast<float>(0.25 + 0.72 * state.preyPopulation));
        bloom.process(alignedDry.left.data() + offset,
                      alignedDry.right.data() + offset,
                      bloomBuffer[0].data(),
                      bloomBuffer[1].data(),
                      frameSamples);

        const double rawPredatorWeight = 0.65 * predatorIntensity;
        const double rawBloomWeight = 0.65 * bloomIntensity;
        const double totalSpeciesWeight = rawPredatorWeight + rawBloomWeight;
        const double normaliser = totalSpeciesWeight > 1.0 ? 1.0 / totalSpeciesWeight : 1.0;
        const float predatorWeight = static_cast<float>(rawPredatorWeight * normaliser);
        const float bloomWeight = static_cast<float>(rawBloomWeight * normaliser);
        const float dryWeight = static_cast<float>(std::max(0.0, 1.0 - totalSpeciesWeight));

        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            const float* input = channel == 0 ? alignedDry.left.data() + offset : alignedDry.right.data() + offset;
            float* destination = channel == 0 ? result.audio.left.data() + offset
                                              : result.audio.right.data() + offset;
            for (std::size_t sample = 0; sample < frameSamples; ++sample)
            {
                const float mixed = dryWeight * input[sample]
                                  + predatorWeight * predatorBuffer[channel][sample]
                                  + bloomWeight * bloomBuffer[channel][sample];
                habitatBuffer[channel][sample] = std::clamp(std::isfinite(mixed) ? mixed : 0.0f, -0.99f, 0.99f);
                destination[sample] = habitatBuffer[channel][sample];
            }
        }

        const std::array<const float*, 2> sourcePointers {
            dry.left.data() + offset, dry.right.data() + offset};
        const std::array<const float*, 2> habitatPointers {
            habitatBuffer[0].data(), habitatBuffer[1].data()};
        sourceAnalyzer.processBlock(sourcePointers.data(), 2, frameSamples);
        habitatAnalyzer.processBlock(habitatPointers.data(), 2, frameSamples);

        const auto& sourceResources = sourceAnalyzer.latest().resources;
        const auto& habitatResources = habitatAnalyzer.latest().resources;
        const double bloomFood = unit(0.50 * sourceResources[ResourceId::Silence]
                                    + 0.35 * sourceResources[ResourceId::Sparsity]
                                    + 0.15 * habitatResources[ResourceId::Tail]);
        double bloomEnergy = 0.0;
        double sourceEnergy = 0.0;
        for (std::size_t channel = 0; channel < 2; ++channel)
            for (std::size_t sample = 0; sample < frameSamples; ++sample)
            {
                const double value = bloomWeight * bloomBuffer[channel][sample];
                bloomEnergy += value * value;
                const float* input = channel == 0 ? alignedDry.left.data() + offset
                                                  : alignedDry.right.data() + offset;
                sourceEnergy += static_cast<double>(input[sample]) * input[sample];
            }
        const double bloomRms = std::sqrt(bloomEnergy / (2.0 * frameSamples));
        const double sourceRms = std::sqrt(sourceEnergy / (2.0 * frameSamples));
        const double measuredBloomActivity = std::max(unit(1.2 * bloomRms / std::max(0.02, sourceRms)),
                                                      bloomIntensity);

        PredatorPreyControls controls;
        controls.resources = 0.92 * bloomFood;
        controls.stability = 0.25;
        controls.populationSpeed = 0.5;
        ecology.step(measuredBloomActivity, controls, 1.0 / static_cast<double>(controlRateHz));
        recordRanges();
    }
    return result;
}

double differenceRms(const Stereo& a, const Stereo& b) noexcept
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.left.size(); ++i)
    {
        const double left = static_cast<double>(a.left[i]) - b.left[i];
        const double right = static_cast<double>(a.right[i]) - b.right[i];
        sum += left * left + right * right;
    }
    return std::sqrt(sum / static_cast<double>(2u * a.left.size()));
}
} // namespace

int main(const int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: phase3_render_cli <dry.wav> <processed.wav>\n";
        return 2;
    }

    try
    {
        const auto dry = makeFixture();
        const auto rendered = renderEcology(dry);
        const double deltaRms = differenceRms(dry, rendered.audio);
        if (rendered.predatorPopulation.maximum < 0.40
            || rendered.bloomPopulation.minimum > 0.10
            || rendered.bloomPopulation.maximum < 0.70
            || !std::isfinite(deltaRms) || deltaRms < 0.01)
            throw std::runtime_error("Phase 3 closed-loop audio/ecology acceptance thresholds failed");
        writeWave(argv[1], dry);
        writeWave(argv[2], rendered.audio);

        std::cout << std::fixed << std::setprecision(6)
                  << "render: 48000 Hz, stereo PCM16, 40.0 s, 2000 exact 50 Hz causal frames\n"
                  << "fixture: [0,8) alternating transients; [8,16) sparse pauses; "
                     "[16,26) sustained pad; [26,32) alternating transients; [32,40) silence\n"
                  << "predator population range: " << rendered.predatorPopulation.minimum
                  << " .. " << rendered.predatorPopulation.maximum << '\n'
                  << "bloom population range: " << rendered.bloomPopulation.minimum
                  << " .. " << rendered.bloomPopulation.maximum << '\n'
                  << "predator routed intensity range: " << rendered.predatorIntensity.minimum
                  << " .. " << rendered.predatorIntensity.maximum << '\n'
                  << "bloom routed intensity range: " << rendered.bloomIntensity.minimum
                  << " .. " << rendered.bloomIntensity.maximum << '\n'
                  << "dry/processed difference RMS: " << deltaRms << '\n'
                  << "dry: " << argv[1] << '\n'
                  << "processed: " << argv[2] << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "phase3 render failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
