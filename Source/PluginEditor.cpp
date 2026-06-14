#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    const auto background = juce::Colour::fromString ("ff090b10");
    const auto panel = juce::Colour::fromString ("ff10141c");
    const auto panelLight = juce::Colour::fromString ("ff171d27");
    const auto grid = juce::Colour::fromString ("ff29313d");
    const auto gold = juce::Colour::fromString ("ffffc857");
    const auto warmGold = juce::Colour::fromString ("ffffa928");
    const auto aqua = juce::Colour::fromString ("ff55d9d2");
    const auto text = juce::Colour::fromString ("ffe8edf2");
    const auto muted = juce::Colour::fromString ("ff788596");

    constexpr float minimumFrequency = 20.0f;
    constexpr float maximumFrequency = 20000.0f;
    constexpr float graphMinimumDb = -24.0f;
    constexpr float graphMaximumDb = 24.0f;
    constexpr float knobStart = juce::MathConstants<float>::pi * 1.20f;
    constexpr float knobEnd = juce::MathConstants<float>::pi * 2.80f;

    juce::String formatFrequency (float frequency)
    {
        return frequency >= 1000.0f
                   ? juce::String (frequency / 1000.0f, frequency >= 10000.0f ? 0 : 1) + "k"
                   : juce::String (static_cast<int> (frequency));
    }
}

JuiceEQAudioProcessorEditor::VocalLookAndFeel::VocalLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::outlineColourId, gold.withAlpha (0.35f));
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, gold);
    setColour (juce::PopupMenu::backgroundColourId, panelLight);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, gold.withAlpha (0.22f));
}

void JuiceEQAudioProcessorEditor::VocalLookAndFeel::drawRotarySlider (
    juce::Graphics& g,
    int x,
    int y,
    int width,
    int height,
    float position,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider&)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                          static_cast<float> (y),
                                          static_cast<float> (width),
                                          static_cast<float> (height))
                      .reduced (12.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto dial = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const float radius = diameter * 0.5f;
    const float angle = rotaryStartAngle + position * (rotaryEndAngle - rotaryStartAngle);

    juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.75f),
                                 dial.getCentreX(),
                                 dial.getBottom(),
                                 panelLight,
                                 dial.getCentreX(),
                                 dial.getY(),
                                 false);
    g.setGradientFill (shadow);
    g.fillEllipse (dial.translated (0.0f, 4.0f));

    juce::ColourGradient face (panelLight.brighter (0.08f),
                               dial.getCentreX() - radius * 0.4f,
                               dial.getY(),
                               background,
                               dial.getCentreX() + radius * 0.5f,
                               dial.getBottom(),
                               false);
    g.setGradientFill (face);
    g.fillEllipse (dial);

    juce::Path track;
    track.addCentredArc (dial.getCentreX(), dial.getCentreY(), radius - 8.0f, radius - 8.0f,
                         0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (grid.withAlpha (0.9f));
    g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (dial.getCentreX(), dial.getCentreY(), radius - 8.0f, radius - 8.0f,
                         0.0f, rotaryStartAngle, angle, true);
    g.setColour (gold.withAlpha (0.18f));
    g.strokePath (value, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (gold);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    for (int tick = 0; tick <= 20; ++tick)
    {
        const float tickPosition = static_cast<float> (tick) / 20.0f;
        const float tickAngle = rotaryStartAngle + tickPosition * (rotaryEndAngle - rotaryStartAngle);
        const float innerRadius = radius - (tick % 5 == 0 ? 22.0f : 18.0f);
        const float outerRadius = radius - 13.0f;
        const auto centre = dial.getCentre();
        const juce::Point<float> inner (centre.x + std::cos (tickAngle) * innerRadius,
                                        centre.y + std::sin (tickAngle) * innerRadius);
        const juce::Point<float> outer (centre.x + std::cos (tickAngle) * outerRadius,
                                        centre.y + std::sin (tickAngle) * outerRadius);
        g.setColour (tickPosition <= position ? gold.withAlpha (0.75f) : muted.withAlpha (0.28f));
        g.drawLine ({ inner, outer }, tick % 5 == 0 ? 1.6f : 1.0f);
    }

    juce::Path pointer;
    pointer.addRoundedRectangle (-2.5f, -radius + 29.0f, 5.0f, radius * 0.40f, 2.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                .translated (dial.getCentreX(), dial.getCentreY()));
    g.setColour (warmGold);
    g.fillPath (pointer);

    g.setColour (gold.withAlpha (0.30f));
    g.drawEllipse (dial.reduced (1.0f), 1.0f);
}

void JuiceEQAudioProcessorEditor::VocalLookAndFeel::drawComboBox (
    juce::Graphics& g,
    int width,
    int height,
    bool,
    int,
    int,
    int,
    int,
    juce::ComboBox&)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                                static_cast<float> (width),
                                                static_cast<float> (height))
                            .reduced (0.5f);
    g.setColour (panelLight);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (gold.withAlpha (0.40f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    juce::Path arrow;
    const float cx = static_cast<float> (width - 13);
    const float cy = static_cast<float> (height) * 0.5f;
    arrow.startNewSubPath (cx - 3.5f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 3.5f, cy - 2.0f);
    g.setColour (gold);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void JuiceEQAudioProcessorEditor::VocalLookAndFeel::positionComboBoxText (
    juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (box.getLocalBounds().reduced (8, 1).withTrimmedRight (15));
    label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
}

JuiceEQAudioProcessorEditor::SpectrumDisplay::SpectrumDisplay (JuiceEQAudioProcessor& p)
    : processor (p)
{
    history.fill (0.0f);
    spectrumDb.fill (-100.0f);
    setInterceptsMouseClicks (false, false);
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::refresh()
{
    const int pulled = processor.pullAnalyzerSamples (pullBuffer.data(), pullBufferSize);

    if (pulled > 0)
    {
        const int copyCount = juce::jmin (pulled, fftSize);
        const int keepCount = fftSize - copyCount;
        std::memmove (history.data(), history.data() + copyCount,
                      static_cast<size_t> (keepCount) * sizeof (float));
        std::memcpy (history.data() + keepCount, pullBuffer.data() + pulled - copyCount,
                     static_cast<size_t> (copyCount) * sizeof (float));
        samplesSinceLastTransform += pulled;
    }

    if (samplesSinceLastTransform >= fftSize / 4)
    {
        samplesSinceLastTransform = 0;
        updateSpectrum();
    }

    repaint();
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::updateSpectrum()
{
    std::copy (history.begin(), history.end(), fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double sampleRate = processor.getCurrentSampleRate();

    for (int index = 0; index < displayBins; ++index)
    {
        const float proportion = static_cast<float> (index) / static_cast<float> (displayBins - 1);
        const float frequency = minimumFrequency
                                * std::pow (maximumFrequency / minimumFrequency, proportion);
        const int fftBin = juce::jlimit (
            1, fftBins - 1, static_cast<int> (std::round (frequency * fftSize / sampleRate)));
        const float target = juce::Decibels::gainToDecibels (
            fftData[static_cast<size_t> (fftBin)] / static_cast<float> (fftSize), -100.0f);
        const float previous = spectrumDb[static_cast<size_t> (index)];
        spectrumDb[static_cast<size_t> (index)] = target > previous
                                                      ? previous * 0.35f + target * 0.65f
                                                      : previous * 0.88f + target * 0.12f;
    }
}

float JuiceEQAudioProcessorEditor::SpectrumDisplay::frequencyToX (
    float frequency, juce::Rectangle<float> bounds) const noexcept
{
    const float proportion = std::log (frequency / minimumFrequency)
                             / std::log (maximumFrequency / minimumFrequency);
    return bounds.getX() + bounds.getWidth() * proportion;
}

float JuiceEQAudioProcessorEditor::SpectrumDisplay::decibelsToY (
    float decibels, juce::Rectangle<float> bounds) const noexcept
{
    return juce::jmap (juce::jlimit (graphMinimumDb, graphMaximumDb, decibels),
                       graphMaximumDb, graphMinimumDb, bounds.getY(), bounds.getBottom());
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (grid.withAlpha (0.8f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto graph = bounds.reduced (42.0f, 22.0f);
    graph.removeFromBottom (7.0f);
    drawGrid (g, graph);
    drawSpectrum (g, graph);
    drawResponse (g, graph);
    drawBandMarkers (g, graph);
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::drawGrid (
    juce::Graphics& g, juce::Rectangle<float> bounds)
{
    constexpr std::array<float, 10> frequencies {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };

    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    for (const float frequency : frequencies)
    {
        const float x = frequencyToX (frequency, bounds);
        g.setColour (grid.withAlpha (frequency == 20.0f || frequency == 20000.0f ? 0.7f : 0.45f));
        g.drawVerticalLine (static_cast<int> (std::round (x)), bounds.getY(), bounds.getBottom());
        g.setColour (muted);
        g.drawText (formatFrequency (frequency),
                    juce::Rectangle<float> (x - 22.0f, bounds.getBottom() + 4.0f, 44.0f, 14.0f),
                    juce::Justification::centred);
    }

    for (int db = -24; db <= 24; db += 6)
    {
        const float y = decibelsToY (static_cast<float> (db), bounds);
        g.setColour (db == 0 ? gold.withAlpha (0.22f) : grid.withAlpha (0.42f));
        g.drawHorizontalLine (static_cast<int> (std::round (y)), bounds.getX(), bounds.getRight());
        g.setColour (muted.withAlpha (0.8f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                    juce::Rectangle<float> (4.0f, y - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight);
    }
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::drawSpectrum (
    juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::Path line;
    juce::Path fill;

    for (int index = 0; index < displayBins; ++index)
    {
        const float proportion = static_cast<float> (index) / static_cast<float> (displayBins - 1);
        const float frequency = minimumFrequency
                                * std::pow (maximumFrequency / minimumFrequency, proportion);
        const float x = frequencyToX (frequency, bounds);
        const float analyzerDb = juce::jmap (spectrumDb[static_cast<size_t> (index)],
                                             -100.0f, 0.0f, graphMinimumDb, graphMaximumDb);
        const float y = decibelsToY (analyzerDb, bounds);

        if (index == 0)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, bounds.getBottom());
            fill.lineTo (x, y);
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }

    fill.lineTo (bounds.getRight(), bounds.getBottom());
    fill.closeSubPath();
    g.setColour (aqua.withAlpha (0.07f));
    g.fillPath (fill);
    g.setColour (aqua.withAlpha (0.34f));
    g.strokePath (line, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::drawResponse (
    juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::Path response;
    constexpr int points = 420;

    for (int point = 0; point < points; ++point)
    {
        const float proportion = static_cast<float> (point) / static_cast<float> (points - 1);
        const float frequency = minimumFrequency
                                * std::pow (maximumFrequency / minimumFrequency, proportion);
        const float x = frequencyToX (frequency, bounds);
        const float y = decibelsToY (processor.getMagnitudeResponseDb (frequency), bounds);

        if (point == 0)
            response.startNewSubPath (x, y);
        else
            response.lineTo (x, y);
    }

    g.setColour (gold.withAlpha (0.15f));
    g.strokePath (response, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    g.setColour (gold);
    g.strokePath (response, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
}

void JuiceEQAudioProcessorEditor::SpectrumDisplay::drawBandMarkers (
    juce::Graphics& g, juce::Rectangle<float> bounds)
{
    constexpr std::array<float, 8> bandFrequencies {
        110.0f, 200.0f, 450.0f, 2000.0f, 3600.0f, 8000.0f, 10000.0f, 20000.0f
    };

    for (int band = 0; band < static_cast<int> (bandFrequencies.size()); ++band)
    {
        const float frequency = bandFrequencies[static_cast<size_t> (band)];
        const float x = frequencyToX (frequency, bounds);
        const float y = decibelsToY (processor.getMagnitudeResponseDb (frequency), bounds);
        const bool dynamic = band == 1 || band == 4 || band == 6;
        const float radius = dynamic ? 5.5f : 4.0f;

        g.setColour (dynamic ? aqua.withAlpha (0.18f) : gold.withAlpha (0.18f));
        g.fillEllipse (x - radius * 1.8f, y - radius * 1.8f, radius * 3.6f, radius * 3.6f);
        g.setColour (dynamic ? aqua : gold);
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (background);
        g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
        g.drawText (juce::String (band + 1),
                    juce::Rectangle<float> (x - radius, y - radius, radius * 2.0f, radius * 2.0f),
                    juce::Justification::centred);
    }
}

JuiceEQAudioProcessorEditor::JuiceEQAudioProcessorEditor (JuiceEQAudioProcessor& processor)
    : AudioProcessorEditor (&processor),
      audioProcessor (processor),
      spectrumDisplay (processor)
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (860, 700, 1320, 940);
    setSize (1040, 760);

    addAndMakeVisible (spectrumDisplay);

    cleanKnob.setName ("Clean");
    brightnessKnob.setName ("Brightness");
    configureMainKnob (cleanKnob, " %");
    configureMainKnob (brightnessKnob, " %");

    oversamplingBox.addItemList ({ "1x", "2x", "4x", "8x" }, 1);
    oversamplingBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (oversamplingBox);

    cleanAttachment = std::make_unique<SliderAttachment> (
        audioProcessor.apvts, JuiceEQAudioProcessor::ParameterIDs::clean, cleanKnob);
    brightnessAttachment = std::make_unique<SliderAttachment> (
        audioProcessor.apvts, JuiceEQAudioProcessor::ParameterIDs::brightness, brightnessKnob);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment> (
        audioProcessor.apvts, JuiceEQAudioProcessor::ParameterIDs::oversampling, oversamplingBox);

    startTimerHz (30);
}

JuiceEQAudioProcessorEditor::~JuiceEQAudioProcessorEditor()
{
    stopTimer();
    oversamplingAttachment = nullptr;
    brightnessAttachment = nullptr;
    cleanAttachment = nullptr;
    setLookAndFeel (nullptr);
}

void JuiceEQAudioProcessorEditor::configureMainKnob (juce::Slider& slider,
                                                     const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (knobStart, knobEnd, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 24);
    slider.setTextValueSuffix (suffix);
    slider.setDoubleClickReturnValue (true, slider.getName() == "Clean" ? 100.0 : 25.0);
    addAndMakeVisible (slider);
}

void JuiceEQAudioProcessorEditor::timerCallback()
{
    spectrumDisplay.refresh();
    repaint();
}

void JuiceEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    juce::ColourGradient topGlow (gold.withAlpha (0.10f),
                                  static_cast<float> (getWidth()) * 0.5f,
                                  0.0f,
                                  juce::Colours::transparentBlack,
                                  static_cast<float> (getWidth()) * 0.5f,
                                  230.0f,
                                  false);
    g.setGradientFill (topGlow);
    g.fillRect (getLocalBounds());

    auto title = getLocalBounds().reduced (26).removeFromTop (48);
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (23.0f, juce::Font::bold)));
    g.drawText ("JUICE VOCAL RESTORER", title, juce::Justification::centredLeft);

    g.setColour (gold);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("FIXED EQ  /  DYNAMIC CLEANUP  /  ULTRA-AIR",
                title, juce::Justification::centredRight);

    const auto cleanLabel = cleanKnob.getBounds().translated (0, -22);
    const auto brightnessLabel = brightnessKnob.getBounds().translated (0, -22);
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText ("CLEAN", cleanLabel, juce::Justification::centredTop);
    g.drawText ("BRIGHTNESS", brightnessLabel, juce::Justification::centredTop);

    g.setColour (muted);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("GAIN SCALE  0-200%", cleanLabel.translated (0, 20), juce::Justification::centredTop);
    g.drawText ("18 kHz+ TUBE AIR", brightnessLabel.translated (0, 20), juce::Justification::centredTop);

    const auto optionLabel = oversamplingBox.getBounds().translated (0, -17);
    g.setColour (muted);
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText ("OVERSAMPLING", optionLabel, juce::Justification::centredTop);

    g.setColour (muted.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("Made by Kino",
                getLocalBounds().reduced (24).removeFromBottom (18),
                juce::Justification::centredRight);
}

void JuiceEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (54);
    area.removeFromBottom (20);

    const int graphHeight = juce::jlimit (315, 500, static_cast<int> (area.getHeight() * 0.60f));
    spectrumDisplay.setBounds (area.removeFromTop (graphHeight));
    area.removeFromTop (24);

    const int knobSize = juce::jlimit (154, 220, area.getHeight() - 34);
    const int gap = juce::jlimit (50, 130, area.getWidth() / 8);
    const int totalWidth = knobSize * 2 + gap;
    const int startX = getWidth() / 2 - totalWidth / 2;
    const int knobY = area.getY();

    cleanKnob.setBounds (startX, knobY, knobSize, knobSize);
    brightnessKnob.setBounds (startX + knobSize + gap, knobY, knobSize, knobSize);
    oversamplingBox.setBounds (getWidth() / 2 - 47, area.getBottom() - 28, 94, 24);
}
