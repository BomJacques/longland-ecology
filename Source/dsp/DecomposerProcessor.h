#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace longland
{
// Bounded stereo granular micro-buffer. The ecology layer decides when this
// species is active; this class only turns captured audio into eroded fragments.
// All storage is allocated by prepare(), never by process().
class DecomposerProcessor
{
public:
    static constexpr std::size_t maximumGrains = 32;

    void prepare(double sampleRate, double maximumBufferMilliseconds = 1600.0);
    void reset() noexcept;

    void setIntensity(float normalisedIntensity) noexcept; // 0 .. 1
    void setGrainSizeMilliseconds(float milliseconds) noexcept; // 12 .. 180 ms
    void setDensity(float normalisedDensity) noexcept; // 0 .. 1, sparse .. dense
    void setScatter(float normalisedScatter) noexcept; // lookback, pitch and pan spread
    void setRandomSeed(std::uint32_t seed) noexcept;

    // Buffers may alias. Null inputs or an unprepared processor produce silence
    // when outputs exist. Stereo grains retain input stereo while adding bounded
    // cross-pan variation.
    void process(const float* inputLeft,
                 const float* inputRight,
                 float* outputLeft,
                 float* outputRight,
                 std::size_t samples) noexcept;

    float currentIntensity() const noexcept { return intensity.current; }
    float currentDensity() const noexcept { return density.current; }
    std::size_t activeGrainCount() const noexcept;
    bool isPrepared() const noexcept { return prepared; }

private:
    struct SmoothedValue
    {
        float current {};
        float target {};
        float coefficient {};

        float next() noexcept;
        void snap() noexcept { current = target; }
    };

    struct Grain
    {
        double readPosition {};
        double increment {1.0};
        std::uint32_t age {};
        std::uint32_t length {};
        float pan {};
        float gain {};
        bool active {};
    };

    static constexpr float maximumSignal = 12.0f;
    static constexpr std::uint32_t defaultSeed = 0x6c6f6e67u;

    static float sanitise(float value, float fallback = 0.0f) noexcept;
    static float clampUnit(float value) noexcept;
    static float boundSignal(float value) noexcept;
    static double wrapPosition(double position, std::size_t length) noexcept;
    static float grainWindow(std::uint32_t age, std::uint32_t length) noexcept;

    std::uint32_t nextRandom() noexcept;
    float randomUnit() noexcept;
    float readInterpolated(std::size_t channel, double position) const noexcept;
    void launchGrain(float grainMilliseconds, float scatterAmount) noexcept;

    std::array<std::vector<float>, 2> captureBuffer;
    std::array<Grain, maximumGrains> grains;
    std::size_t writeIndex {};
    std::size_t recordedSamples {};
    double sampleRateHz {48000.0};
    double samplesUntilNextGrain {};
    std::uint32_t randomState {defaultSeed};
    std::uint32_t requestedSeed {defaultSeed};
    SmoothedValue intensity;
    SmoothedValue grainSizeMs {54.0f, 54.0f, 0.0f};
    SmoothedValue density {0.45f, 0.45f, 0.0f};
    SmoothedValue scatter {0.55f, 0.55f, 0.0f};
    bool prepared {};
};
} // namespace longland
