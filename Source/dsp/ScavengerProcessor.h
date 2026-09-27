#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace longland
{
// Stereo, wet-only delay branch for the Scavenger species. prepare() is the
// only operation which allocates; process() is suitable for the audio thread.
class ScavengerProcessor
{
public:
    void prepare(double sampleRate, double maximumDelaySeconds = 2.0);
    void reset() noexcept;

    // Intensity and diffusion are normalised. Intensity controls how much new
    // material enters the organism, while an existing tail remains free to
    // decay naturally. Feedback is an amplitude ratio hard-limited below unity.
    // Delay time is milliseconds. Invalid controls become conservative values.
    void setIntensity(double populationIntensity) noexcept;
    void setFeedback(double feedbackAmount) noexcept;
    void setDelayTimeMs(double milliseconds) noexcept;
    void setDiffusion(double normalisedDiffusion) noexcept;

    // Produces only the Scavenger return; the caller owns the dry/parallel mix.
    // Input and output may alias. Null inputs or an unprepared processor yield
    // silence when valid output buffers are supplied.
    void process(const float* inputLeft,
                 const float* inputRight,
                 float* outputLeft,
                 float* outputRight,
                 std::size_t samples) noexcept;

    bool isPrepared() const noexcept { return prepared; }
    double currentIntensity() const noexcept { return intensity.current; }
    double currentFeedback() const noexcept { return feedback.current; }
    double currentDelayTimeMs() const noexcept { return delayTimeMs.current; }
    double currentDiffusion() const noexcept { return diffusion.current; }

private:
    struct SmoothedValue
    {
        double current {};
        double target {};
        double coefficient {};

        double next() noexcept;
        void snap() noexcept { current = target; }
    };

    struct FeedbackFilter
    {
        double lowPass {};
        double previousInput {};
        double highPass {};

        void clear() noexcept;
        double process(double input,
                       double lowPassCoefficient,
                       double highPassPole) noexcept;
    };

    static double sanitise(double value, double fallback = 0.0) noexcept;
    static double clampUnit(double value) noexcept;
    static double boundSignal(double value) noexcept;

    double readInterpolated(std::size_t channel, double delaySamples) const noexcept;
    void write(std::size_t channel, double value) noexcept;

    std::array<std::vector<float>, 2> delayBuffers;
    std::array<FeedbackFilter, 2> feedbackFilters;
    std::size_t writeIndex {};
    double sampleRateHz {48000.0};
    double maximumDelayMs {2000.0};
    double lowPassCoefficient {0.48};
    double highPassPole {0.997};
    SmoothedValue intensity;
    SmoothedValue feedback;
    SmoothedValue delayTimeMs;
    SmoothedValue diffusion;
    bool prepared {};
};
} // namespace longland
