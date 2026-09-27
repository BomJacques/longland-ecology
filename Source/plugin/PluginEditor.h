#pragma once

#include "plugin/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class LonglandEcologyEditor final : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit LonglandEcologyEditor(LonglandEcologyAudioProcessor&);
    ~LonglandEcologyEditor() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void configureSlider(juce::Slider&, juce::Label&, const juce::String& labelText);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    LonglandEcologyAudioProcessor& processor;
    juce::Slider resourcesSlider;
    juce::Slider stabilitySlider;
    juce::Slider populationSpeedSlider;
    juce::Slider dryWetSlider;
    juce::Slider outputSlider;
    juce::Label resourcesLabel;
    juce::Label stabilityLabel;
    juce::Label populationSpeedLabel;
    juce::Label dryWetLabel;
    juce::Label outputLabel;
    SliderAttachment resourcesAttachment;
    SliderAttachment stabilityAttachment;
    SliderAttachment populationSpeedAttachment;
    SliderAttachment dryWetAttachment;
    SliderAttachment outputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LonglandEcologyEditor)
};
