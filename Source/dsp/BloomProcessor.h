#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace longland
{
// A small stereo ambience tank intended for the Bloom species. Memory is
// allocated only by prepare(); process() is allocation-free and noexcept.
class BloomProcessor
{
public:
    void prepare(double sampleRate);
    void reset() noexcept;

    // Both controls are normalised to [0, 1]. Invalid values are made safe.
    // Intensity controls the tank send and wet return. Decay controls a
    // deliberately conservative feedback range.
    void setIntensity(float normalisedIntensity) noexcept;
    void setDecay(float normalisedDecay) noexcept;

    // Input and output may alias, which makes in-place processing safe.
    // Null pointers or an unprepared processor produce a silent output.
    void process(const float* inputLeft,
                 const float* inputRight,
                 float* outputLeft,
                 float* outputRight,
                 std::size_t samples) noexcept;

    float currentIntensity() const noexcept { return intensity.current; }
    float currentDecay() const noexcept { return decay.current; }
    bool isPrepared() const noexcept { return prepared; }

private:
    struct SmoothedValue
    {
        float current {};
        float target {};
        float coefficient {};

        float next() noexcept;
        void snapToTarget() noexcept { current = target; }
    };

    struct DelayLine
    {
        std::vector<float> samples;
        std::size_t writeIndex {};
        float damped {};

        void resize(std::size_t length);
        void clear() noexcept;
        float read() const noexcept;
        void write(float value) noexcept;
    };

    struct Allpass
    {
        DelayLine delay;
        float coefficient {0.5f};

        float process(float input) noexcept;
    };

    static float sanitise(float value, float fallback = 0.0f) noexcept;
    static float clampUnit(float value) noexcept;
    static float bounded(float value) noexcept;

    std::array<DelayLine, 4> tank;
    std::array<Allpass, 2> leftDiffusers;
    std::array<Allpass, 2> rightDiffusers;
    SmoothedValue intensity;
    SmoothedValue decay;
    bool prepared {};
};
} // namespace longland

