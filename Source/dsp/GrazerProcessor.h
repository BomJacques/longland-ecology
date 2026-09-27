#pragma once

#include <array>
#include <cstddef>

namespace longland
{
// Grazer reduces whichever broad spectral region is currently most abundant.
// Population/health-to-intensity mapping deliberately belongs to the ecology
// layer; this class only accepts the resulting normalised intensity.
class GrazerProcessor
{
public:
    enum class Band : std::size_t
    {
        none = 0,
        low,
        mid,
        high
    };

    struct Telemetry
    {
        std::array<float, 3> bandLevels {};
        Band targetBand {Band::none};
        float currentIntensity {};
        float targetAttenuationDb {};
    };

    void prepare(double sampleRate, double smoothingSeconds = 0.03) noexcept;
    void reset() noexcept;

    void setIntensity(float normalisedIntensity) noexcept;

    // The detector is stereo coherent: both channels contribute to one target
    // decision and receive identical per-band gains. Buffers may alias.
    void process(const float* inputLeft,
                 const float* inputRight,
                 float* outputLeft,
                 float* outputRight,
                 std::size_t samples) noexcept;

    Band targetBand() const noexcept { return selectedBand; }
    Telemetry telemetry() const noexcept;
    float currentBandGain(Band band) const noexcept;
    bool isPrepared() const noexcept { return prepared; }

private:
    struct ChannelState
    {
        float lowpassLow {};
        float lowpassHigh {};
    };

    static constexpr float maximumAttenuationDb = 9.0f;
    static constexpr float maximumSignal = 16.0f;

    static float sanitise(float value, float fallback = 0.0f) noexcept;
    static float clampUnit(float value) noexcept;
    static float bounded(float value) noexcept;
    static std::size_t bandIndex(Band band) noexcept;

    std::array<float, 3> split(ChannelState& state, float input) const noexcept;
    void updateDetector(const std::array<float, 3>& left,
                        const std::array<float, 3>& right) noexcept;
    void updateTargetBand() noexcept;

    std::array<ChannelState, 2> channels {};
    std::array<float, 3> energy {};
    std::array<float, 3> bandGain {1.0f, 1.0f, 1.0f};
    float intensity {};
    float targetIntensity {};
    float lowCoefficient {};
    float highCoefficient {};
    float detectorAttack {};
    float detectorRelease {};
    float smoothingCoefficient {};
    Band selectedBand {Band::none};
    bool prepared {};
};
} // namespace longland

