#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <memory>

class JuiceEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit JuiceEQAudioProcessorEditor (JuiceEQAudioProcessor&);
    ~JuiceEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class VocalLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        VocalLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int, int, int, int, float,
                               float, float, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int,
                           juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    };

    class SpectrumDisplay final : public juce::Component
    {
    public:
        explicit SpectrumDisplay (JuiceEQAudioProcessor&);

        void refresh();
        void paint (juce::Graphics&) override;

    private:
        static constexpr int fftOrder = 11;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int fftBins = fftSize / 2;
        static constexpr int displayBins = 256;
        static constexpr int pullBufferSize = 4096;

        float frequencyToX (float frequency, juce::Rectangle<float> bounds) const noexcept;
        float decibelsToY (float decibels, juce::Rectangle<float> bounds) const noexcept;
        void updateSpectrum();
        void drawGrid (juce::Graphics&, juce::Rectangle<float>);
        void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
        void drawResponse (juce::Graphics&, juce::Rectangle<float>);
        void drawBandMarkers (juce::Graphics&, juce::Rectangle<float>);

        JuiceEQAudioProcessor& processor;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window {
            fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris, true
        };

        std::array<float, fftSize * 2> fftData {};
        std::array<float, fftSize> history {};
        std::array<float, pullBufferSize> pullBuffer {};
        std::array<float, displayBins> spectrumDb {};
        int samplesSinceLastTransform = 0;
    };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void configureMainKnob (juce::Slider&, const juce::String& suffix);

    JuiceEQAudioProcessor& audioProcessor;
    VocalLookAndFeel lookAndFeel;
    SpectrumDisplay spectrumDisplay;

    juce::Slider cleanKnob;
    juce::Slider brightnessKnob;
    juce::ComboBox oversamplingBox;

    std::unique_ptr<SliderAttachment> cleanAttachment;
    std::unique_ptr<SliderAttachment> brightnessAttachment;
    std::unique_ptr<ComboBoxAttachment> oversamplingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuiceEQAudioProcessorEditor)
};
