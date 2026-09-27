#include "plugin/PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto background = 0xff0c1211;
constexpr auto panel = 0xff111c19;
constexpr auto text = 0xffe0ecd9;
constexpr auto secondaryText = 0xff8ea997;
constexpr auto bloomColour = 0xff4fa07a;
constexpr auto predatorColour = 0xffcd5a47;
constexpr auto scavengerColour = 0xff6697c8;
constexpr auto grazerColour = 0xffd0a44f;
constexpr auto parasiteColour = 0xffa56dcc;
constexpr auto habitatColour = 0xff5d7e75;

float safeUnitValue(const double value) noexcept
{
    if (!std::isfinite(value))
        return 0.0f;

    return std::clamp(static_cast<float>(value), 0.0f, 1.0f);
}

juce::String formatValue(const double value)
{
    return std::isfinite(value) ? juce::String(value, 3) : juce::String("--");
}

juce::String grazerBandName(const int band)
{
    if (band == static_cast<int>(longland::GrazerProcessor::Band::low))
        return "LOW";
    if (band == static_cast<int>(longland::GrazerProcessor::Band::mid))
        return "MID";
    if (band == static_cast<int>(longland::GrazerProcessor::Band::high))
        return "HIGH";
    return "NONE";
}

void drawNode(juce::Graphics& g,
              const juce::Rectangle<float> bounds,
              const juce::String& name,
              const float population,
              const juce::Colour colour)
{
    const auto value = std::clamp(population, 0.0f, 1.0f);
    const auto diameter = 54.0f + 72.0f * value;
    const auto node = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());

    g.setColour(colour.withAlpha(0.18f));
    g.fillEllipse(node.expanded(7.0f));
    g.setColour(colour.withAlpha(0.55f + 0.45f * value));
    g.fillEllipse(node);
    g.setColour(colour.brighter(0.25f));
    g.drawEllipse(node, 1.5f);

    const auto labelArea = juce::Rectangle<int>(static_cast<int>(bounds.getX()),
                                                 static_cast<int>(bounds.getBottom()) - 34,
                                                 static_cast<int>(bounds.getWidth()),
                                                 18);
    const auto valueArea = labelArea.translated(0, 18);
    g.setColour(juce::Colour(text));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawFittedText(name, labelArea, juce::Justification::centred, 1);
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText(juce::String(value, 3), valueArea, juce::Justification::centred, 1);
}

void drawMeter(juce::Graphics& g,
               juce::Rectangle<int> bounds,
               const juce::String& label,
               const double value)
{
    auto labelArea = bounds.removeFromTop(17);
    g.setColour(juce::Colour(secondaryText));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawFittedText(label, labelArea, juce::Justification::centred, 1);
    g.setColour(juce::Colour(text));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(formatValue(value), bounds, juce::Justification::centred, 1);
}
}

LonglandEcologyEditor::LonglandEcologyEditor(LonglandEcologyAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      resourcesAttachment(p.parameters, "resources", resourcesSlider),
      stabilityAttachment(p.parameters, "stability", stabilitySlider),
      populationSpeedAttachment(p.parameters, "populationSpeed", populationSpeedSlider),
      dryWetAttachment(p.parameters, "dryWet", dryWetSlider),
      outputAttachment(p.parameters, "output", outputSlider)
{
    configureSlider(resourcesSlider, resourcesLabel, "RESOURCES");
    configureSlider(stabilitySlider, stabilityLabel, "STABILITY");
    configureSlider(populationSpeedSlider, populationSpeedLabel, "POPULATION SPEED");
    configureSlider(dryWetSlider, dryWetLabel, "DRY / WET");
    configureSlider(outputSlider, outputLabel, "OUTPUT DB");
    outputSlider.setTextValueSuffix(" dB");

    setResizable(false, false);
    setSize(920, 700);
    startTimerHz(20);
}

void LonglandEcologyEditor::configureSlider(juce::Slider& slider,
                                             juce::Label& label,
                                             const juce::String& labelText)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(habitatColour));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff263a34));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(text));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(text));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(background));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(secondaryText));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

void LonglandEcologyEditor::resized()
{
    auto content = getLocalBounds().reduced(24);
    content.removeFromTop(58);
    auto controls = content.removeFromBottom(104).reduced(8, 4);

    std::array<juce::Slider*, 5> sliders {
        &resourcesSlider, &stabilitySlider, &populationSpeedSlider, &dryWetSlider, &outputSlider};
    std::array<juce::Label*, 5> labels {
        &resourcesLabel, &stabilityLabel, &populationSpeedLabel, &dryWetLabel, &outputLabel};

    for (std::size_t index = 0; index < sliders.size(); ++index)
    {
        const int remainingColumns = static_cast<int>(sliders.size() - index);
        auto column = controls.removeFromLeft(controls.getWidth() / remainingColumns);
        labels[index]->setBounds(column.removeFromTop(18));
        sliders[index]->setBounds(column.reduced(7, 0));
    }
}

void LonglandEcologyEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(background));

    auto content = getLocalBounds().reduced(24);
    auto header = content.removeFromTop(58);
    auto controls = content.removeFromBottom(104);
    content.removeFromBottom(10);
    auto telemetry = content.removeFromBottom(96);
    content.removeFromBottom(8);
    auto resourceTelemetry = content.removeFromBottom(58);
    content.removeFromBottom(8);

    g.setColour(juce::Colour(text));
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawFittedText("LONGLAND ECOLOGY", header.removeFromTop(32),
                     juce::Justification::centredLeft, 1);
    g.setColour(juce::Colour(secondaryText));
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText("LIVE ECOLOGY TELEMETRY", header,
                     juce::Justification::centredLeft, 1);

    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(controls.toFloat(), 7.0f);
    g.setColour(juce::Colour(habitatColour).withAlpha(0.38f));
    g.drawRoundedRectangle(controls.toFloat(), 7.0f, 1.0f);

    const auto bloom = safeUnitValue(processor.bloomPopulation());
    const auto predator = safeUnitValue(processor.predatorPopulation());
    const auto scavenger = safeUnitValue(processor.scavengerPopulation());
    const auto grazer = safeUnitValue(processor.grazerPopulation());
    const auto parasite = safeUnitValue(processor.parasitePopulation());
    auto ecosystem = content.toFloat().reduced(8.0f, 2.0f);
    const auto fullEcosystem = ecosystem;
    auto topRow = ecosystem.removeFromTop(ecosystem.getHeight() * 0.5f);
    const auto topWidth = topRow.getWidth() / 3.0f;
    const auto bloomBounds = topRow.removeFromLeft(topWidth);
    const auto parasiteBounds = topRow.removeFromLeft(topWidth);
    const auto scavengerBounds = topRow;
    const auto bottomWidth = ecosystem.getWidth() / 3.0f;
    const auto grazerBounds = ecosystem.removeFromLeft(bottomWidth);
    const auto decomposerBounds = ecosystem.removeFromLeft(bottomWidth);
    const auto predatorBounds = ecosystem;

    g.setColour(juce::Colour(habitatColour).withAlpha(0.55f));
    g.drawLine(juce::Line<float>(bloomBounds.getCentre(), parasiteBounds.getCentre()), 1.5f);
    g.drawLine(juce::Line<float>(bloomBounds.getCentre(), scavengerBounds.getCentre()), 1.2f);
    g.drawLine(juce::Line<float>(predatorBounds.getCentre(), bloomBounds.getCentre()), 1.5f);
    g.drawLine(juce::Line<float>(predatorBounds.getCentre(), grazerBounds.getCentre()), 1.5f);
    g.drawLine(juce::Line<float>(scavengerBounds.getCentre(), decomposerBounds.getCentre()), 1.5f);
    g.drawLine(juce::Line<float>(decomposerBounds.getCentre(), predatorBounds.getCentre()), 1.5f);

    drawNode(g, bloomBounds, "BLOOM", bloom, juce::Colour(bloomColour));
    drawNode(g, parasiteBounds, "PARASITE", parasite, juce::Colour(parasiteColour));
    drawNode(g, scavengerBounds, "SCAVENGER", scavenger, juce::Colour(scavengerColour));
    drawNode(g, grazerBounds, "GRAZER", grazer, juce::Colour(grazerColour));
    drawNode(g, decomposerBounds, "DECOMPOSER",
             safeUnitValue(processor.decomposerPopulation()), juce::Colour(0xffba7b58));
    drawNode(g, predatorBounds, "PREDATOR", predator, juce::Colour(predatorColour));

    const auto activity = safeUnitValue(processor.inputActivity());
    const auto habitatDiameter = 42.0f + 18.0f * activity;
    const auto habitatNode = juce::Rectangle<float>(habitatDiameter, habitatDiameter)
                                 .withCentre(fullEcosystem.getCentre());
    g.setColour(juce::Colour(background));
    g.fillEllipse(habitatNode.expanded(5.0f));
    g.setColour(juce::Colour(habitatColour).withAlpha(0.5f + 0.5f * activity));
    g.fillEllipse(habitatNode);
    g.setColour(juce::Colour(text));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawFittedText("AUDIO", habitatNode.toNearestInt(), juce::Justification::centred, 1);

    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(telemetry.toFloat(), 7.0f);
    auto meters = telemetry.reduced(8, 5);
    auto firstMeterRow = meters.removeFromTop(meters.getHeight() / 2);
    auto secondMeterRow = meters;
    const auto firstMeterWidth = firstMeterRow.getWidth() / 6;
    const auto secondMeterWidth = secondMeterRow.getWidth() / 6;
    const auto inputArea = firstMeterRow.removeFromLeft(firstMeterWidth);
    const auto bloomFoodArea = firstMeterRow.removeFromLeft(firstMeterWidth);
    const auto scavengerFoodArea = firstMeterRow.removeFromLeft(firstMeterWidth);
    const auto grazerFoodArea = firstMeterRow.removeFromLeft(firstMeterWidth);
    const auto parasiteFoodArea = firstMeterRow.removeFromLeft(firstMeterWidth);
    const auto decomposerFoodArea = firstMeterRow;
    const auto grazerBandArea = secondMeterRow.removeFromLeft(secondMeterWidth);
    const auto parasitePressureArea = secondMeterRow.removeFromLeft(secondMeterWidth);
    const auto decomposerConsumptionArea = secondMeterRow.removeFromLeft(secondMeterWidth);
    const auto decomposerFragmentsArea = secondMeterRow.removeFromLeft(secondMeterWidth);
    const auto predatorFoodArea = secondMeterRow.removeFromLeft(secondMeterWidth);
    const auto attackArea = secondMeterRow;
    drawMeter(g, inputArea, "INPUT", processor.inputActivity());
    drawMeter(g, bloomFoodArea, "BLOOM FOOD", processor.bloomFood());
    drawMeter(g, scavengerFoodArea, "SCAVENGER FOOD", processor.scavengerFood());
    drawMeter(g, grazerFoodArea, "GRAZER FOOD", processor.grazerFood());
    drawMeter(g, grazerBandArea, "GRAZER " + grazerBandName(processor.grazerTargetBand()),
              processor.grazerAttenuationDb());
    drawMeter(g, parasiteFoodArea, "PARA FOOD", processor.parasiteFood());
    drawMeter(g, decomposerFoodArea, "DECOMP FOOD", processor.decomposerFood());
    drawMeter(g, parasitePressureArea, "HOST PRESS", processor.parasitePressure());
    drawMeter(g, decomposerConsumptionArea, "CONSUMED", processor.decomposerConsumption());
    drawMeter(g, decomposerFragmentsArea, "FRAGMENTS", processor.decomposerFragments());
    drawMeter(g, predatorFoodArea, "PREDATOR FOOD", processor.predatorFood());
    drawMeter(g, attackArea, "ATTACK", processor.attackInfluence());

    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(resourceTelemetry.toFloat(), 7.0f);
    auto resourceMeters = resourceTelemetry.reduced(8, 5);
    const auto resourceWidth = resourceMeters.getWidth() / 7;
    const auto transientArea = resourceMeters.removeFromLeft(resourceWidth);
    const auto silenceArea = resourceMeters.removeFromLeft(resourceWidth);
    const auto sparsityArea = resourceMeters.removeFromLeft(resourceWidth);
    const auto sustainArea = resourceMeters.removeFromLeft(resourceWidth);
    const auto tailArea = resourceMeters.removeFromLeft(resourceWidth);
    const auto repetitionArea = resourceMeters.removeFromLeft(resourceWidth);
    const auto highArea = resourceMeters;
    drawMeter(g, transientArea, "TRANSIENT", processor.transientResource());
    drawMeter(g, silenceArea, "SILENCE", processor.silenceResource());
    drawMeter(g, sparsityArea, "SPARSITY", processor.sparsityResource());
    drawMeter(g, sustainArea, "SUSTAIN", processor.sustainResource());
    drawMeter(g, tailArea, "TAIL", processor.tailResource());
    drawMeter(g, repetitionArea, "REPETITION", processor.repetitionResource());
    drawMeter(g, highArea, "HIGHS", processor.highResource());
}
