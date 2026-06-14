#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>

class JuiceEQAudioProcessor final : public juce::AudioProcessor
{
public:
    JuiceEQAudioProcessor();
    ~JuiceEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    struct ParameterIDs
    {
        static constexpr auto clean = "clean";
        static constexpr auto brightness = "brightness";
        static constexpr auto oversampling = "oversampling";
    };

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int dynamicBandCount = 3;
    static constexpr int analyzerFifoSize = 32768;

    int pullAnalyzerSamples (float* destination, int maximumSamples) noexcept;
    float getMagnitudeResponseDb (float frequencyHz) const noexcept;
    float getDynamicReductionDb (int band) const noexcept;
    double getCurrentSampleRate() const noexcept { return sampleRateHz.load(); }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    using ArrayCoefficients = juce::dsp::IIR::ArrayCoefficients<float>;

    struct DynamicBand
    {
        Filter detector;
        float envelope = 0.0f;
        float reductionDb = 0.0f;
        float frequency = 1000.0f;
        float q = 1.0f;
        float thresholdDb = -30.0f;
        float ratio = 3.0f;
        float maximumDepthDb = 4.0f;

        DynamicBand();
        void prepare (double processingRate);
        void reset() noexcept;
        float process (float input, float cleanScale) noexcept;

    private:
        float attackCoefficient = 0.0f;
        float releaseCoefficient = 0.0f;
        float gainAttackCoefficient = 0.0f;
        float gainReleaseCoefficient = 0.0f;
    };

    struct ChannelDsp
    {
        Filter lowCut;
        Filter bell450;
        Filter bell2k;
        Filter bell8k;
        Filter shelf20k;
        Filter tubeHighPass;
        Filter tubeWetHighPass;
        Filter brightnessShelf;
        std::array<DynamicBand, dynamicBandCount> dynamicBands;
        double lastProcessingRate = 0.0;
        float lastCleanScale = -1.0f;
        float lastBrightnessAmount = -1.0f;

        ChannelDsp();
        void prepare (double processingRate);
        void updateCoefficients (double processingRate, float cleanScale, float brightnessAmount);
        void reset() noexcept;
        float process (float input, float cleanScale, float brightnessAmount) noexcept;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void rebuildOversamplers (int channels, int maximumBlockSize);
    void updateOversamplingChoice (int choice);
    void updateFilterCoefficients (double processingRate, float cleanScale, float brightnessAmount);
    void processDspBlock (juce::dsp::AudioBlock<float> block,
                          double processingRate,
                          float cleanScale,
                          float brightnessAmount) noexcept;
    void pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer) noexcept;
    void updateDynamicMeters() noexcept;
    void resetDspState() noexcept;

    static float getParameter (const std::atomic<float>* parameter, float fallback) noexcept;
    static float magnitudeForHighPass (float frequency, double sampleRate, float cutoff, float q) noexcept;
    static float magnitudeForPeak (float frequency, double sampleRate, float cutoff, float q, float gainDb) noexcept;
    static float magnitudeForHighShelf (float frequency, double sampleRate, float cutoff, float q, float gainDb) noexcept;

    static constexpr int maximumChannels = 2;

    std::array<ChannelDsp, maximumChannels> channels;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;

    std::atomic<float>* cleanParameter = nullptr;
    std::atomic<float>* brightnessParameter = nullptr;
    std::atomic<float>* oversamplingParameter = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> cleanSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> brightnessSmoother;

    std::atomic<double> sampleRateHz { 44100.0 };
    int preparedChannels = 2;
    int preparedBlockSize = 512;
    int activeOversamplingChoice = -1;

    std::array<std::atomic<float>, dynamicBandCount> dynamicReductionMeters {};

    std::array<float, analyzerFifoSize> analyzerStorage {};
    juce::AbstractFifo analyzerFifo { analyzerFifoSize };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuiceEQAudioProcessor)
};
