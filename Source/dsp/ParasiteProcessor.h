#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace longland
{
// Stereo micro-pitch processor intended to inhabit another species' branch.
// The ecology layer supplies its population * host dependency as intensity;
// this class contains no population or host-selection behaviour.
class ParasiteProcessor
{
public:
    void prepare(double sampleRate, double maximumDelayMilliseconds = 60.0);
    void reset() noexcept;

    void setIntensity(float normalisedIntensity) noexcept;
    void setDetuneCents(float cents) noexcept;       // 0 .. 30 cents
    void setSmear(float normalisedSmear) noexcept;  // 0 .. 1
    void setFeedback(float feedbackAmount) noexcept; // 0 .. 0.32

    // Both channels share deterministic modulation state. Buffers may alias.
    // Null inputs or an unprepared processor produce silence when outputs exist.
    void process(const float* inputLeft,
                 const float* inputRight,
                 float* outputLeft,
                 float* outputRight,
                 std::size_t samples) noexcept;

    float currentIntensity() const noexcept { return intensity.current; }
    float currentPitchRatio() const noexcept { return pitchRatio.current; }
    float currentSmear() const noexcept { return smear.current; }
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

    static constexpr float maximumSignal = 12.0f;
    static constexpr float maximumFeedback = 0.32f;
    static constexpr float defaultPitchRatio = 1.00695555f; // +12 cents

    static float sanitise(float value, float fallback = 0.0f) noexcept;
    static float clampUnit(float value) noexcept;
    static float boundSignal(float value) noexcept;
    static double wrapUnit(double phase) noexcept;
    static double window(double phase) noexcept;

    float readInterpolated(std::size_t channel, double delaySamples) const noexcept;
    float pitchRead(std::size_t channel, double phase, double smearOffset) const noexcept;

    std::array<std::vector<float>, 2> delayBuffers;
    std::array<float, 2> previousWet {};
    std::size_t writeIndex {};
    double sampleRateHz {48000.0};
    double minimumDelaySamples {};
    double delayRangeSamples {};
    double upwardPhase {0.25};
    double downwardPhase {0.75};
    double smearPhase {};
    double smearPhaseIncrement {};
    SmoothedValue intensity;
    SmoothedValue pitchRatio {defaultPitchRatio, defaultPitchRatio, 0.0f};
    SmoothedValue smear {0.45f, 0.45f, 0.0f};
    SmoothedValue feedback {0.12f, 0.12f, 0.0f};
    bool prepared {};
};
} // namespace longland

