#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

//==============================================================================
// JuiceEQAudioProcessor
//
// JuiceEQ는 실시간 스펙트럴 EQ/레조넌스 억제기입니다.
// 일반 EQ처럼 고정 주파수를 수동으로 깎는 방식이 아니라, 2048pt FFT로 입력을
// 계속 분석하면서 주변 대역보다 튀어나온 공진만 자동으로 눌러 줍니다.
//
// 핵심 구조:
// 1. 2048 샘플을 모아 FFT로 주파수 영역으로 변환합니다.
// 2. 각 FFT bin의 에너지가 주변 평균보다 얼마나 돌출되었는지 계산합니다.
// 3. depth / sharpness / selectivity / softHard 값으로 억제 곡선을 만듭니다.
// 4. 주파수별 gain을 곱한 뒤 inverse FFT로 시간 영역에 되돌립니다.
// 5. 512 샘플 hop의 4중 overlap-add로 프레임 경계가 들리지 않게 합칩니다.
//
// Delta 모드에서는 최종 처리음이 아니라 FFT에서 제거된 성분만 출력합니다.
// Sidechain 모드에서는 외부 사이드체인 신호의 주파수 분포를 분석해서,
// 그 대역을 메인 신호에서 동적으로 비워 줍니다.
//==============================================================================
class JuiceEQAudioProcessor final : public juce::AudioProcessor
{
public:
    JuiceEQAudioProcessor();
    ~JuiceEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // UI가 파라미터를 안전하게 연결할 수 있도록 APVTS를 public으로 둡니다.
    juce::AudioProcessorValueTreeState apvts;

    // Juice Spectrum Visualizer용 데이터입니다.
    // 값의 의미: 0.0 = 억제 없음, 1.0 = 매우 강한 억제.
    static constexpr int getVisualizerBinCount() noexcept { return visualizerBinCount; }
    void copySpectrumReductionData (float* destination, int destinationSize) const noexcept;

    // 이전 에디터/미터 코드와의 호환을 위한 간단한 미터 접근자입니다.
    float getVisualLevel() const noexcept;
    float getDuckingDepth() const noexcept;
    float getAverageSuppression() const noexcept;

private:
    //==============================================================================
    // APVTS 파라미터 ID입니다.
    //
    // 이 문자열은 DAW 자동화와 프리셋 저장에 들어가므로,
    // 배포 후에는 함부로 바꾸지 않는 편이 안전합니다.
    struct ParameterIDs
    {
        static constexpr const char* depth       = "depth";
        static constexpr const char* sharpness   = "sharpness";
        static constexpr const char* selectivity = "selectivity";
        static constexpr const char* softHard    = "softHard";
        static constexpr const char* delta       = "delta";
        static constexpr const char* sidechain   = "sidechain";
        static constexpr const char* mix         = "mix";
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    // FFT / STFT 고정값
    //
    // fftOrder 11 => 2^11 = 2048pt FFT.
    // hopSize 512 => 2048 샘플 프레임이 75% 겹칩니다.
    // sqrt-Hann 분석 윈도우와 sqrt-Hann 합성 윈도우를 같이 쓰면 실제로는
    // Hann 윈도우가 4장 겹치는 형태가 되고, hop = N/4에서 합이 일정해집니다.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int fftDataSize = fftSize * 2;
    static constexpr int numFrequencyBins = fftSize / 2 + 1;
    static constexpr int maximumAudioChannels = 2;
    static constexpr int visualizerBinCount = 128;

    // 분석 sqrt-Hann * 합성 sqrt-Hann = Hann이고, 4중 overlap에서 Hann 합이
    // 2가 되므로 0.5를 곱해 원래 레벨로 맞춥니다.
    static constexpr float overlapAddGain = 0.5f;

    struct FrameParameters
    {
        float depth = 0.0f;
        float sharpness = 0.0f;
        float selectivity = 0.0f;
        float softHard = 0.0f;
    };

    struct MainChannelState
    {
        std::array<float, fftSize> inputRing {};
        std::vector<float> dryDelay;
        std::vector<float> processedOverlap;
        std::vector<float> deltaOverlap;

        int inputWriteIndex = 0;
        int dryDelayIndex = 0;
        int overlapReadIndex = 0;

        void prepare (int overlapRingSize, int latencySamples);
        void clear() noexcept;
    };

    struct SidechainChannelState
    {
        std::array<float, fftSize> inputRing {};
        int inputWriteIndex = 0;

        void clear() noexcept;
    };

    //==============================================================================
    float getParameterValue (const char* parameterID) const noexcept;
    void resetSmoothers();
    void clearDspState();
    void initialiseWindow() noexcept;

    void pushMainSample (int channel, float sample) noexcept;
    void pushSidechainSample (int channel, float sample) noexcept;
    float readDryDelayThenStoreInput (int channel, float input) noexcept;
    float readAndClearProcessedSample (int channel) noexcept;
    float readAndClearDeltaSample (int channel) noexcept;
    void advanceChannelCursors (int channel) noexcept;

    void processSpectralFrame (int numMainChannels,
                               int numSidechainChannels,
                               bool useSidechain,
                               const FrameParameters& parameters) noexcept;

    void buildMagnitudeAnalysis (int numMainChannels,
                                 int numSidechainChannels,
                                 bool analyseSidechain) noexcept;

    void calculateReductionCurve (bool useSidechain,
                                  const FrameParameters& parameters) noexcept;

    void renderChannelFrame (int channel) noexcept;
    void addFrameToOverlapBuffers (int channel) noexcept;
    void updateVisualizerData() noexcept;

    void fillFftDataFromRing (const std::array<float, fftSize>& ring,
                              int writeIndex) noexcept;

    void smoothAcrossFrequency (const std::array<float, numFrequencyBins>& source,
                                std::array<float, numFrequencyBins>& destination,
                                int radius) const noexcept;

    static float softLimitSample (float input) noexcept;
    static float smoothStep (float value) noexcept;
    static float safeDecibels (float linearGain) noexcept;
    static float normalisedParameter (float percentValue) noexcept;
    static float binToFrequency (int bin, double sampleRate) noexcept;
    static int frequencyToBin (float frequency, double sampleRate) noexcept;
    static bool isFiniteAndPositive (double value) noexcept;

    //==============================================================================
    juce::dsp::FFT fft { fftOrder };

    std::array<MainChannelState, maximumAudioChannels> mainChannelState;
    std::array<SidechainChannelState, maximumAudioChannels> sidechainChannelState;

    std::array<float, fftSize> sqrtHannWindow {};

    std::array<float, fftDataSize> fftData {};
    std::array<float, fftDataSize> deltaFftData {};

    std::array<float, numFrequencyBins> mainMagnitudeDb {};
    std::array<float, numFrequencyBins> sidechainMagnitudeDb {};
    std::array<float, numFrequencyBins> mainLocalAverageDb {};
    std::array<float, numFrequencyBins> sidechainLocalAverageDb {};
    std::array<float, numFrequencyBins> rawReduction {};
    std::array<float, numFrequencyBins> spreadReduction {};
    std::array<float, numFrequencyBins> smoothedReduction {};
    std::array<float, numFrequencyBins> binGain {};

    int overlapRingSize = fftSize * 4;
    int latencySamples = fftSize;
    int samplesSinceLastFrame = 0;
    double sampleRateHz = 44100.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sharpnessSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> selectivitySmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> softHardSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;

    std::atomic<float> visualLevel { 0.0f };
    std::atomic<float> averageSuppression { 0.0f };
    std::array<std::atomic<float>, visualizerBinCount> visualReduction {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuiceEQAudioProcessor)
};
