#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    const juce::Colour labBackground = juce::Colour::fromString ("ff050805");
    const juce::Colour labPanel = juce::Colour::fromString ("ff0b120c");
    const juce::Colour labPanelBright = juce::Colour::fromString ("ff111b13");
    const juce::Colour juiceGreen = juce::Colour::fromString ("ff00ff41");
    const juce::Colour labText = juce::Colour::fromString ("ddc9ffd4");
    const juce::Colour labMutedText = juce::Colour::fromString ("8896b89d");
    const juce::Colour warningRed = juce::Colour::fromString ("ffff3155");

    constexpr float rotaryStartAngle = juce::MathConstants<float>::pi * 1.20f;
    constexpr float rotaryEndAngle = juce::MathConstants<float>::pi * 2.80f;

    constexpr float analyzerMinimumFrequency = 20.0f;
    constexpr float analyzerMaximumFrequency = 20000.0f;

    void drawLabGrid (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setColour (juiceGreen.withAlpha (0.045f));

        for (float x = bounds.getX(); x < bounds.getRight(); x += 24.0f)
            g.drawVerticalLine (static_cast<int> (x), bounds.getY(), bounds.getBottom());

        for (float y = bounds.getY(); y < bounds.getBottom(); y += 24.0f)
            g.drawHorizontalLine (static_cast<int> (y), bounds.getX(), bounds.getRight());
    }
}

//==============================================================================
JuiceEQAudioProcessorEditor::MadLabLookAndFeel::MadLabLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, labText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, labText);
    setColour (juce::TextButton::textColourOffId, labMutedText);
    setColour (juce::TextButton::textColourOnId, labText);
    setColour (juce::TextButton::buttonColourId, labPanelBright);
    setColour (juce::TextButton::buttonOnColourId, juiceGreen);
}

void JuiceEQAudioProcessorEditor::MadLabLookAndFeel::drawRotarySlider
(
    juce::Graphics& g,
    int x,
    int y,
    int width,
    int height,
    float sliderPosProportional,
    float rotaryStart,
    float rotaryEnd,
    juce::Slider& slider
)
{
    juce::ignoreUnused (slider);

    const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                static_cast<float> (y),
                                                static_cast<float> (width),
                                                static_cast<float> (height))
                                                .reduced (8.0f);

    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getCentreY();
    const float angle = rotaryStart + sliderPosProportional * (rotaryEnd - rotaryStart);

    // 검은 금속 베이스입니다. 노브가 배경에서 뜨지 않도록 아주 어두운 색만 씁니다.
    g.setColour (juce::Colour::fromString ("ff070d08"));
    g.fillEllipse (bounds);

    g.setColour (juce::Colour::fromString ("ff1a271d"));
    g.drawEllipse (bounds, 2.0f);

    g.setColour (juiceGreen.withAlpha (0.11f));
    g.drawEllipse (bounds.reduced (6.0f), 1.0f);

    // 계측기 눈금입니다. 값을 읽는 기능보다 "실험실 장비" 느낌을 주는 장식에 가깝습니다.
    for (int tick = 0; tick <= 14; ++tick)
    {
        const float proportion = static_cast<float> (tick) / 14.0f;
        const float tickAngle = rotaryStart + proportion * (rotaryEnd - rotaryStart);
        const bool major = tick % 7 == 0 || tick == 7;
        const float inner = radius - (major ? 15.0f : 11.0f);
        const float outer = radius - 4.0f;

        const juce::Point<float> p1 (centreX + std::cos (tickAngle) * inner,
                                     centreY + std::sin (tickAngle) * inner);
        const juce::Point<float> p2 (centreX + std::cos (tickAngle) * outer,
                                     centreY + std::sin (tickAngle) * outer);

        g.setColour (major ? juiceGreen.withAlpha (0.68f)
                           : labMutedText.withAlpha (0.34f));
        g.drawLine (juce::Line<float> (p1, p2), major ? 1.6f : 1.0f);
    }

    juce::Path glowArc;
    glowArc.addCentredArc (centreX,
                           centreY,
                           radius - 17.0f,
                           radius - 17.0f,
                           0.0f,
                           rotaryStart,
                           angle,
                           true);

    g.setColour (juiceGreen.withAlpha (0.20f));
    g.strokePath (glowArc, juce::PathStrokeType (10.0f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    g.setColour (juiceGreen);
    g.strokePath (glowArc, juce::PathStrokeType (3.0f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    // 노브 포인터입니다. 선명한 단일 바늘을 써서 값의 방향이 바로 보이게 합니다.
    juce::Path pointer;
    pointer.addRoundedRectangle (-3.0f,
                                 -radius + 24.0f,
                                 6.0f,
                                 radius * 0.58f,
                                 3.0f);

    pointer.applyTransform (juce::AffineTransform::rotation (angle)
                            .translated (centreX, centreY));

    g.setColour (juiceGreen.withAlpha (0.95f));
    g.fillPath (pointer);

    g.setColour (labBackground);
    g.fillEllipse (centreX - radius * 0.29f,
                   centreY - radius * 0.29f,
                   radius * 0.58f,
                   radius * 0.58f);

    g.setColour (juiceGreen.withAlpha (0.62f));
    g.drawEllipse (centreX - radius * 0.29f,
                   centreY - radius * 0.29f,
                   radius * 0.58f,
                   radius * 0.58f,
                   1.2f);
}

void JuiceEQAudioProcessorEditor::MadLabLookAndFeel::drawButtonBackground
(
    juce::Graphics& g,
    juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown
)
{
    juce::ignoreUnused (backgroundColour);

    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool active = button.getToggleState();
    const bool isDelta = button.getComponentID() == "delta";

    const juce::Colour accent = isDelta && active ? warningRed : juiceGreen;
    const float corner = 7.0f;

    g.setColour (labPanelBright);
    g.fillRoundedRectangle (bounds, corner);

    if (active)
    {
        g.setColour (accent.withAlpha (isDelta ? 0.28f : 0.20f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), corner - 2.0f);
    }

    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
    {
        g.setColour (juce::Colours::white.withAlpha (shouldDrawButtonAsDown ? 0.12f : 0.07f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), corner - 2.0f);
    }

    g.setColour (active ? accent.withAlpha (0.92f) : labMutedText.withAlpha (0.42f));
    g.drawRoundedRectangle (bounds, corner, active ? 1.8f : 1.0f);

    // 왼쪽 작은 상태등입니다. 버튼을 길게 읽지 않아도 켜진 상태가 보입니다.
    auto lampArea = bounds;
    const auto lamp = lampArea.removeFromLeft (14.0f).withSizeKeepingCentre (6.0f, 20.0f);
    g.setColour (active ? accent : labMutedText.withAlpha (0.30f));
    g.fillRoundedRectangle (lamp, 3.0f);
}

void JuiceEQAudioProcessorEditor::MadLabLookAndFeel::drawButtonText
(
    juce::Graphics& g,
    juce::TextButton& button,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown
)
{
    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    const bool active = button.getToggleState();
    const bool isDelta = button.getComponentID() == "delta";
    const juce::Colour accent = isDelta && active ? warningRed : juiceGreen;

    g.setColour (active ? accent : labMutedText);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (18, 0),
                      juce::Justification::centred,
                      1);
}

//==============================================================================
JuiceEQAudioProcessorEditor::SpectrumAnalyzer::SpectrumAnalyzer (JuiceEQAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    targetReduction.fill (0.0f);
    displayedReduction.fill (0.0f);
}

void JuiceEQAudioProcessorEditor::SpectrumAnalyzer::refreshFromProcessor() noexcept
{
    processor.copySpectrumReductionData (targetReduction.data(),
                                         static_cast<int> (targetReduction.size()));

    for (size_t index = 0; index < displayedReduction.size(); ++index)
    {
        const float target = juce::jlimit (0.0f, 1.0f, targetReduction[index]);
        const float current = displayedReduction[index];

        // 공격은 빠르게, 복귀는 천천히 합니다. 실제 오디오에는 영향을 주지 않는
        // UI 전용 스무딩이라 그래프가 덜 떨리고, CPU 비용도 128개 float 연산뿐입니다.
        const float coefficient = target > current ? 0.38f : 0.13f;
        displayedReduction[index] = current + (target - current) * coefficient;
    }

    repaint();
}

void JuiceEQAudioProcessorEditor::SpectrumAnalyzer::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (labPanel);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (juiceGreen.withAlpha (0.24f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.2f);

    auto header = getLocalBounds().removeFromTop (32).reduced (16, 0);

    g.setColour (juiceGreen);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText ("JUICE SPECTRUM ANALYZER", header, juce::Justification::centredLeft);

    g.setColour (labMutedText);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("REDUCTION CURVE", header, juce::Justification::centredRight);

    const auto plotArea = bounds.reduced (18.0f, 38.0f).withTrimmedBottom (18.0f);

    g.setColour (juce::Colour::fromString ("ff071009"));
    g.fillRoundedRectangle (plotArea, 5.0f);

    g.saveState();
    g.reduceClipRegion (plotArea.toNearestInt());

    // 수평선은 억제 깊이 기준선입니다. 위쪽은 0 dB, 아래로 내려갈수록 더 많이 깎인 것입니다.
    for (int line = 0; line <= 4; ++line)
    {
        const float proportion = static_cast<float> (line) / 4.0f;
        const float y = juce::jmap (proportion, plotArea.getY(), plotArea.getBottom());

        g.setColour (juiceGreen.withAlpha (line == 0 ? 0.18f : 0.075f));
        g.drawHorizontalLine (static_cast<int> (std::round (y)),
                              plotArea.getX(),
                              plotArea.getRight());
    }

    const std::array<float, 10> frequencyMarks {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };

    for (float frequency : frequencyMarks)
    {
        const float x = frequencyToX (frequency, plotArea);
        g.setColour (juiceGreen.withAlpha (0.075f));
        g.drawVerticalLine (static_cast<int> (std::round (x)),
                            plotArea.getY(),
                            plotArea.getBottom());
    }

    juce::Path filledReduction;
    juce::Path reductionLine;

    for (int bin = 0; bin < spectrumBins; ++bin)
    {
        const float x = plotArea.getX()
                        + plotArea.getWidth() * static_cast<float> (bin)
                        / static_cast<float> (spectrumBins - 1);

        const float reduction = juce::jlimit (0.0f, 1.0f, displayedReduction[static_cast<size_t> (bin)]);

        // 억제량은 위에서 아래로 파고드는 형태로 그립니다.
        // soothe류 플러그인처럼 "어느 주파수가 얼마나 깎였는지" 바로 읽히는 모양입니다.
        const float y = plotArea.getY() + reduction * plotArea.getHeight() * 0.88f;

        if (bin == 0)
        {
            filledReduction.startNewSubPath (x, plotArea.getY());
            filledReduction.lineTo (x, y);
            reductionLine.startNewSubPath (x, y);
        }
        else
        {
            filledReduction.lineTo (x, y);
            reductionLine.lineTo (x, y);
        }
    }

    filledReduction.lineTo (plotArea.getRight(), plotArea.getY());
    filledReduction.closeSubPath();

    g.setColour (juiceGreen.withAlpha (0.24f));
    g.fillPath (filledReduction);

    g.setColour (juiceGreen.withAlpha (0.62f));
    g.strokePath (reductionLine, juce::PathStrokeType (7.0f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    g.setColour (juiceGreen);
    g.strokePath (reductionLine, juce::PathStrokeType (2.0f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    g.restoreState();

    auto labelArea = getLocalBounds().toFloat().reduced (18.0f, 0.0f).removeFromBottom (20.0f);

    g.setColour (labMutedText.withAlpha (0.78f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    for (float frequency : frequencyMarks)
    {
        const float x = frequencyToX (frequency, plotArea);
        const auto text = formatFrequencyLabel (frequency);
        const auto textBounds = juce::Rectangle<float> (x - 22.0f,
                                                        labelArea.getY(),
                                                        44.0f,
                                                        labelArea.getHeight());

        g.drawFittedText (text, textBounds.toNearestInt(), juce::Justification::centred, 1);
    }
}

float JuiceEQAudioProcessorEditor::SpectrumAnalyzer::frequencyToX
(
    float frequency,
    juce::Rectangle<float> plotArea
) noexcept
{
    const float safeFrequency = juce::jlimit (analyzerMinimumFrequency,
                                              analyzerMaximumFrequency,
                                              frequency);

    const float proportion = std::log (safeFrequency / analyzerMinimumFrequency)
                             / std::log (analyzerMaximumFrequency / analyzerMinimumFrequency);

    return plotArea.getX() + plotArea.getWidth() * proportion;
}

juce::String JuiceEQAudioProcessorEditor::SpectrumAnalyzer::formatFrequencyLabel (float frequency)
{
    if (frequency >= 1000.0f)
        return juce::String (frequency / 1000.0f, frequency >= 10000.0f ? 0 : 1) + "k";

    return juce::String (static_cast<int> (frequency));
}

//==============================================================================
JuiceEQAudioProcessorEditor::JuiceEQAudioProcessorEditor (JuiceEQAudioProcessor& processor)
    : AudioProcessorEditor (&processor),
      audioProcessor (processor),
      spectrumAnalyzer (processor)
{
    setLookAndFeel (&madLabLookAndFeel);
    setResizable (true, true);
    setResizeLimits (820, 620, 1240, 840);
    setSize (980, 660);

    addAndMakeVisible (spectrumAnalyzer);

    setupKnob (depthSlider, depthLabel, "DEPTH", " %");
    setupKnob (sharpnessSlider, sharpnessLabel, "SHARPNESS", " %");
    setupKnob (selectivitySlider, selectivityLabel, "SELECTIVITY", " %");
    setupKnob (mixSlider, mixLabel, "MIX", " %");

    setupToggleButton (softHardButton, "softHard", "SOFT");
    setupToggleButton (deltaButton, "delta", "DELTA");
    setupToggleButton (sidechainButton, "sidechain", "SIDECHAIN");

    depthAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, "depth", depthSlider);
    sharpnessAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, "sharpness", sharpnessSlider);
    selectivityAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, "selectivity", selectivitySlider);
    mixAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, "mix", mixSlider);

    softHardAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "softHard", softHardButton);
    deltaAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "delta", deltaButton);
    sidechainAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "sidechain", sidechainButton);

    updateToggleButtonText();

    // 60fps로 Processor의 atomic reduction 데이터를 복사합니다.
    // 복사량은 float 128개뿐이라 오디오 스레드를 잠그지 않습니다.
    startTimerHz (60);
}

JuiceEQAudioProcessorEditor::~JuiceEQAudioProcessorEditor()
{
    stopTimer();

    sidechainAttachment = nullptr;
    deltaAttachment = nullptr;
    softHardAttachment = nullptr;

    mixAttachment = nullptr;
    selectivityAttachment = nullptr;
    sharpnessAttachment = nullptr;
    depthAttachment = nullptr;

    setLookAndFeel (nullptr);
}

//==============================================================================
void JuiceEQAudioProcessorEditor::setupKnob (juce::Slider& slider,
                                             juce::Label& label,
                                             const juce::String& labelText,
                                             const juce::String& suffix)
{
    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, labText);
    label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (rotaryStartAngle, rotaryEndAngle, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::textBoxTextColourId, labText);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void JuiceEQAudioProcessorEditor::setupToggleButton (juce::TextButton& button,
                                                     const juce::String& componentID,
                                                     const juce::String& text)
{
    button.setComponentID (componentID);
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    button.setColour (juce::TextButton::buttonColourId, labPanelBright);
    button.setColour (juce::TextButton::buttonOnColourId,
                      componentID == "delta" ? warningRed : juiceGreen);
    addAndMakeVisible (button);
}

void JuiceEQAudioProcessorEditor::layoutKnob (juce::Slider& slider,
                                              juce::Label& label,
                                              juce::Rectangle<int> bounds)
{
    label.setBounds (bounds.removeFromTop (24));
    slider.setBounds (bounds);
}

void JuiceEQAudioProcessorEditor::updateToggleButtonText()
{
    softHardButton.setButtonText (softHardButton.getToggleState() ? "HARD" : "SOFT");
    deltaButton.setButtonText (deltaButton.getToggleState() ? "DELTA ON" : "DELTA");
    sidechainButton.setButtonText (sidechainButton.getToggleState() ? "SC ON" : "SIDECHAIN");
}

void JuiceEQAudioProcessorEditor::timerCallback()
{
    spectrumAnalyzer.refreshFromProcessor();
    updateToggleButtonText();
}

//==============================================================================
void JuiceEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (labBackground);
    drawLabGrid (g, getLocalBounds().toFloat());

    auto titleArea = getLocalBounds().removeFromTop (64);

    g.setColour (juiceGreen);
    g.setFont (juce::Font (juce::FontOptions (27.0f, juce::Font::bold)));
    g.drawText ("JuiceEQ", titleArea.removeFromTop (38), juce::Justification::centred);

    g.setColour (labMutedText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("MAD LAB SPECTRAL RESONANCE SUPPRESSOR",
                titleArea,
                juce::Justification::centred);

    g.setColour (juiceGreen.withAlpha (0.25f));
    g.drawLine (28.0f, 68.0f, static_cast<float> (getWidth() - 28), 68.0f, 1.0f);

    g.setColour (labMutedText.withAlpha (0.74f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Made by Kino",
                getLocalBounds().reduced (24).removeFromBottom (22),
                juce::Justification::centredRight);
}

void JuiceEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (58);

    const int analyzerHeight = juce::jlimit (210, 270, getHeight() / 2 - 60);
    spectrumAnalyzer.setBounds (area.removeFromTop (analyzerHeight));

    area.removeFromTop (18);

    auto toggleRow = area.removeFromTop (48);
    const int toggleGap = 12;
    const int toggleWidth = juce::jlimit (150, 220, (toggleRow.getWidth() - toggleGap * 2) / 3);
    const int totalToggleWidth = toggleWidth * 3 + toggleGap * 2;
    int toggleX = toggleRow.getCentreX() - totalToggleWidth / 2;

    softHardButton.setBounds (toggleX, toggleRow.getY(), toggleWidth, toggleRow.getHeight());
    toggleX += toggleWidth + toggleGap;
    deltaButton.setBounds (toggleX, toggleRow.getY(), toggleWidth, toggleRow.getHeight());
    toggleX += toggleWidth + toggleGap;
    sidechainButton.setBounds (toggleX, toggleRow.getY(), toggleWidth, toggleRow.getHeight());

    area.removeFromTop (20);

    auto knobRow = area.removeFromTop (juce::jmin (190, area.getHeight() - 20));
    const int knobGap = 18;
    const int knobWidth = juce::jlimit (132, 166, (knobRow.getWidth() - knobGap * 3) / 4);
    const int totalKnobWidth = knobWidth * 4 + knobGap * 3;
    int knobX = knobRow.getCentreX() - totalKnobWidth / 2;

    layoutKnob (depthSlider, depthLabel, { knobX, knobRow.getY(), knobWidth, knobRow.getHeight() });
    knobX += knobWidth + knobGap;
    layoutKnob (sharpnessSlider, sharpnessLabel, { knobX, knobRow.getY(), knobWidth, knobRow.getHeight() });
    knobX += knobWidth + knobGap;
    layoutKnob (selectivitySlider, selectivityLabel, { knobX, knobRow.getY(), knobWidth, knobRow.getHeight() });
    knobX += knobWidth + knobGap;
    layoutKnob (mixSlider, mixLabel, { knobX, knobRow.getY(), knobWidth, knobRow.getHeight() });
}
