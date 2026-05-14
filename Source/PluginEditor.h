#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// JuiceEQAudioProcessorEditor
//
// JuiceEQ의 화면 전체를 담당하는 클래스입니다.
// Processor는 오디오 스레드에서 FFT와 억제량 계산을 수행하고,
// Editor는 UI 스레드에서 그 결과를 읽어 그래프로 그립니다.
// 오디오 스레드와 UI 스레드가 복잡한 객체를 직접 공유하면 위험하므로,
// Processor는 atomic float 배열만 공개하고 Editor는 Timer에서 그 값을 복사합니다.
//==============================================================================
class JuiceEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit JuiceEQAudioProcessorEditor (JuiceEQAudioProcessor&);
    ~JuiceEQAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==============================================================================
    // MadLabLookAndFeel
    //
    // JUCE 기본 노브와 버튼은 일반 앱처럼 보여 플러그인의 개성이 약합니다.
    // 이 LookAndFeel은 노브를 실험실 계측기처럼, 토글 버튼을 형광 상태등처럼 그립니다.
    class MadLabLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        MadLabLookAndFeel();

        void drawRotarySlider (juce::Graphics& g,
                               int x,
                               int y,
                               int width,
                               int height,
                               float sliderPosProportional,
                               float rotaryStartAngle,
                               float rotaryEndAngle,
                               juce::Slider& slider) override;

        void drawButtonBackground (juce::Graphics& g,
                                   juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) override;

        void drawButtonText (juce::Graphics& g,
                             juce::TextButton& button,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;
    };

    //==============================================================================
    // SpectrumAnalyzer
    //
    // Processor가 계산한 주파수별 억제량을 128개 시각화 bin으로 받아서 그립니다.
    // 여기서는 FFT를 다시 하지 않습니다. 오디오 계산은 Processor에서 끝내고,
    // UI는 요약된 reduction curve만 읽으므로 그래픽이 오디오 성능에 주는 부담을 줄입니다.
    class SpectrumAnalyzer final : public juce::Component
    {
    public:
        explicit SpectrumAnalyzer (JuiceEQAudioProcessor& processorToUse);

        void refreshFromProcessor() noexcept;
        void paint (juce::Graphics& g) override;

    private:
        static constexpr int spectrumBins = JuiceEQAudioProcessor::getVisualizerBinCount();

        static float frequencyToX (float frequency, juce::Rectangle<float> plotArea) noexcept;
        static juce::String formatFrequencyLabel (float frequency);

        JuiceEQAudioProcessor& processor;

        std::array<float, spectrumBins> targetReduction {};
        std::array<float, spectrumBins> displayedReduction {};
    };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    void setupKnob (juce::Slider& slider,
                    juce::Label& label,
                    const juce::String& labelText,
                    const juce::String& suffix);

    void setupToggleButton (juce::TextButton& button,
                            const juce::String& componentID,
                            const juce::String& text);

    void layoutKnob (juce::Slider& slider,
                     juce::Label& label,
                     juce::Rectangle<int> bounds);

    void updateToggleButtonText();

    //==============================================================================
    JuiceEQAudioProcessor& audioProcessor;
    MadLabLookAndFeel madLabLookAndFeel;
    SpectrumAnalyzer spectrumAnalyzer;

    juce::Slider depthSlider;
    juce::Slider sharpnessSlider;
    juce::Slider selectivitySlider;
    juce::Slider mixSlider;

    juce::Label depthLabel;
    juce::Label sharpnessLabel;
    juce::Label selectivityLabel;
    juce::Label mixLabel;

    juce::TextButton softHardButton;
    juce::TextButton deltaButton;
    juce::TextButton sidechainButton;

    std::unique_ptr<SliderAttachment> depthAttachment;
    std::unique_ptr<SliderAttachment> sharpnessAttachment;
    std::unique_ptr<SliderAttachment> selectivityAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;

    std::unique_ptr<ButtonAttachment> softHardAttachment;
    std::unique_ptr<ButtonAttachment> deltaAttachment;
    std::unique_ptr<ButtonAttachment> sidechainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuiceEQAudioProcessorEditor)
};
