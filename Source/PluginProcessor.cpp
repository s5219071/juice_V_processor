#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <limits>

namespace
{
    constexpr float minimumDecibels = -140.0f;
    constexpr float minimumUsefulFrequency = 20.0f;

    // 시각화용 로그 주파수 범위입니다. 20 Hz부터 거의 Nyquist까지를
    // 128칸으로 나누어 저역은 촘촘하게, 고역은 넓게 보여 줍니다.
    constexpr float visualMinimumFrequency = 20.0f;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
JuiceEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto percentRange = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);

    // Depth: 최대로 얼마나 깊게 깎을지입니다.
    // 0%는 완전 무처리, 100%는 매우 강한 레조넌스 제거입니다.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::depth, 1 }, "Depth", percentRange, 45.0f));

    // Sharpness: 억제 곡선의 폭입니다.
    // 낮으면 넓은 붓으로 부드럽게 누르고, 높으면 좁은 칼날처럼 특정 bin 주변만 누릅니다.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::sharpness, 1 }, "Sharpness", percentRange, 62.0f));

    // Selectivity: "튀어나왔다"고 판단하는 기준입니다.
    // 높을수록 정말 두드러지는 공진만 잡고, 낮을수록 더 많은 대역을 적극적으로 잡습니다.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::selectivity, 1 }, "Selectivity", percentRange, 58.0f));

    // Soft/Hard: 억제 반응의 무릎(knee)입니다.
    // Soft는 마스터링용으로 자연스럽게, Hard는 문제 대역을 더 단호하게 처리합니다.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::softHard, 1 }, "Soft / Hard", percentRange, 35.0f));

    // Delta: 켜면 최종 처리음이 아니라 "사라지는 소리"만 출력합니다.
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::delta, 1 }, "Delta", false));

    // Sidechain: 켜면 외부 사이드체인 입력을 분석하여 같은 주파수 대역을 메인에서 비웁니다.
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParameterIDs::sidechain, 1 }, "Sidechain", false));

    // Mix: latency가 맞춰진 dry와 spectral processed 신호의 블렌드입니다.
    // Delta 모드에서는 요구사항에 맞게 mix를 무시하고 억제 성분만 출력합니다.
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::mix, 1 }, "Mix", percentRange, 100.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations

JuiceEQAudioProcessor::JuiceEQAudioProcessor()
    : AudioProcessor (BusesProperties()
        #if ! JucePlugin_IsMidiEffect
         #if ! JucePlugin_IsSynth
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
         #endif
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        #endif
      ),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    initialiseWindow();
}

#else

JuiceEQAudioProcessor::JuiceEQAudioProcessor()
    : apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    initialiseWindow();
}

#endif

JuiceEQAudioProcessor::~JuiceEQAudioProcessor() = default;

//==============================================================================
const juce::String JuiceEQAudioProcessor::getName() const
{
    return "JuiceEQ";
}

bool JuiceEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool JuiceEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool JuiceEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double JuiceEQAudioProcessor::getTailLengthSeconds() const
{
    // JuiceEQ는 리버브처럼 입력이 멈춘 뒤 꼬리가 남는 플러그인이 아닙니다.
    // 다만 FFT 처리를 위해 latencySamples만큼의 지연은 DAW에 따로 보고합니다.
    return 0.0;
}

int JuiceEQAudioProcessor::getNumPrograms()
{
    return 1;
}

int JuiceEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void JuiceEQAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String JuiceEQAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void JuiceEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void JuiceEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    sampleRateHz = isFiniteAndPositive (sampleRate) ? sampleRate : 44100.0;

    // 이 구현은 입력 샘플이 2048 샘플 뒤에 정확히 재생되도록 overlap-add를
    // 스케줄합니다. DAW가 트랙 보정(PDC)을 할 수 있게 반드시 알려 줍니다.
    latencySamples = fftSize;
    setLatencySamples (latencySamples);

    // overlap ring은 현재 읽는 위치보다 미래에 2048 샘플짜리 프레임을 계속 더해야
    // 하므로 FFT 크기보다 넉넉하게 둡니다. 실시간 처리 중 재할당하지 않습니다.
    overlapRingSize = fftSize * 4;

    for (auto& channel : mainChannelState)
        channel.prepare (overlapRingSize, latencySamples);

    resetSmoothers();
    clearDspState();
}

void JuiceEQAudioProcessor::releaseResources()
{
    clearDspState();
}

#ifndef JucePlugin_PreferredChannelConfigurations

bool JuiceEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (mainInput != mainOutput)
        return false;
   #endif

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechainLayout = layouts.getChannelSet (true, 1);

        if (! sidechainLayout.isDisabled()
            && sidechainLayout != juce::AudioChannelSet::mono()
            && sidechainLayout != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

#endif

//==============================================================================
void JuiceEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto mainInputBuffer = getBusBuffer (buffer, true, 0);
    auto mainOutputBuffer = getBusBuffer (buffer, false, 0);

    const int numSamples = mainOutputBuffer.getNumSamples();
    const int numMainInputChannels = mainInputBuffer.getNumChannels();
    const int numMainOutputChannels = mainOutputBuffer.getNumChannels();
    const int numMainChannels = juce::jmin (maximumAudioChannels,
                                            juce::jmin (numMainInputChannels, numMainOutputChannels));

    for (int channel = numMainChannels; channel < numMainOutputChannels; ++channel)
        mainOutputBuffer.clear (channel, 0, numSamples);

    if (numMainChannels <= 0 || numSamples <= 0)
        return;

    const bool sidechainParameterEnabled = getParameterValue (ParameterIDs::sidechain) > 0.5f;
    const bool deltaEnabled = getParameterValue (ParameterIDs::delta) > 0.5f;

    std::array<const float*, maximumAudioChannels> sidechainReadPointers {};
    bool sidechainBusAvailable = false;
    int numSidechainChannels = 0;

    if (getBusCount (true) > 1)
    {
        if (auto* sidechainBus = getBus (true, 1))
        {
            if (sidechainBus->isEnabled())
            {
                auto sidechainBuffer = getBusBuffer (buffer, true, 1);
                numSidechainChannels = juce::jmin (maximumAudioChannels, sidechainBuffer.getNumChannels());
                sidechainBusAvailable = numSidechainChannels > 0;

                for (int channel = 0; channel < numSidechainChannels; ++channel)
                    sidechainReadPointers[static_cast<size_t> (channel)] =
                        sidechainBuffer.getReadPointer (channel);
            }
        }
    }

    depthSmoother.setTargetValue (normalisedParameter (getParameterValue (ParameterIDs::depth)));
    sharpnessSmoother.setTargetValue (normalisedParameter (getParameterValue (ParameterIDs::sharpness)));
    selectivitySmoother.setTargetValue (normalisedParameter (getParameterValue (ParameterIDs::selectivity)));
    softHardSmoother.setTargetValue (normalisedParameter (getParameterValue (ParameterIDs::softHard)));
    mixSmoother.setTargetValue (normalisedParameter (getParameterValue (ParameterIDs::mix)));

    FrameParameters frameParameters;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        frameParameters.depth = depthSmoother.getNextValue();
        frameParameters.sharpness = sharpnessSmoother.getNextValue();
        frameParameters.selectivity = selectivitySmoother.getNextValue();
        frameParameters.softHard = softHardSmoother.getNextValue();

        const float mix = mixSmoother.getNextValue();
        const bool useSidechainThisSample = sidechainParameterEnabled && numSidechainChannels > 0;

        std::array<float, maximumAudioChannels> dryDelayed {};
        std::array<float, maximumAudioChannels> processedDelayed {};
        std::array<float, maximumAudioChannels> deltaDelayed {};

        for (int channel = 0; channel < numMainChannels; ++channel)
        {
            const float input = mainInputBuffer.getSample (channel, sample);

            dryDelayed[static_cast<size_t> (channel)] = readDryDelayThenStoreInput (channel, input);
            processedDelayed[static_cast<size_t> (channel)] = readAndClearProcessedSample (channel);
            deltaDelayed[static_cast<size_t> (channel)] = readAndClearDeltaSample (channel);

            pushMainSample (channel, input);
        }

        // mono 입력일 때도 두 번째 내부 상태를 깨끗하게 유지합니다.
        for (int channel = numMainChannels; channel < maximumAudioChannels; ++channel)
        {
            readDryDelayThenStoreInput (channel, 0.0f);
            readAndClearProcessedSample (channel);
            readAndClearDeltaSample (channel);
            pushMainSample (channel, 0.0f);
        }

        for (int channel = 0; channel < maximumAudioChannels; ++channel)
        {
            float sidechainSample = 0.0f;

            if (sidechainBusAvailable)
            {
                const int sourceChannel = channel < numSidechainChannels ? channel : 0;

                if (const float* source = sidechainReadPointers[static_cast<size_t> (sourceChannel)])
                    sidechainSample = source[sample];
            }

            pushSidechainSample (channel, sidechainSample);
        }

        ++samplesSinceLastFrame;

        if (samplesSinceLastFrame >= hopSize)
        {
            samplesSinceLastFrame = 0;
            processSpectralFrame (numMainChannels,
                                  numSidechainChannels,
                                  useSidechainThisSample,
                                  frameParameters);
        }

        for (int channel = 0; channel < numMainChannels; ++channel)
        {
            float output = 0.0f;

            if (deltaEnabled)
            {
                // Delta 모드는 "Input - Processed"를 실시간으로 새로 계산하는 방식이 아닙니다.
                // FFT bin에서 gain 때문에 제거된 성분만 별도로 inverse FFT/OLA한 신호를 냅니다.
                output = deltaDelayed[static_cast<size_t> (channel)];
            }
            else
            {
                const float dry = dryDelayed[static_cast<size_t> (channel)];
                const float wet = processedDelayed[static_cast<size_t> (channel)];
                output = dry + (wet - dry) * mix;
            }

            mainOutputBuffer.setSample (channel, sample, softLimitSample (output));
        }

        for (int channel = 0; channel < maximumAudioChannels; ++channel)
            advanceChannelCursors (channel);
    }
}

//==============================================================================
bool JuiceEQAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* JuiceEQAudioProcessor::createEditor()
{
    return new JuiceEQAudioProcessorEditor (*this);
}

//==============================================================================
void JuiceEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
    else
        destData.setSize (0);
}

void JuiceEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
void JuiceEQAudioProcessor::copySpectrumReductionData (float* destination,
                                                       int destinationSize) const noexcept
{
    if (destination == nullptr || destinationSize <= 0)
        return;

    const int count = juce::jmin (destinationSize, visualizerBinCount);

    for (int index = 0; index < count; ++index)
        destination[index] = visualReduction[static_cast<size_t> (index)].load();

    for (int index = count; index < destinationSize; ++index)
        destination[index] = 0.0f;
}

float JuiceEQAudioProcessor::getVisualLevel() const noexcept
{
    return visualLevel.load();
}

float JuiceEQAudioProcessor::getDuckingDepth() const noexcept
{
    return averageSuppression.load();
}

float JuiceEQAudioProcessor::getAverageSuppression() const noexcept
{
    return averageSuppression.load();
}

//==============================================================================
float JuiceEQAudioProcessor::getParameterValue (const char* parameterID) const noexcept
{
    if (auto* value = apvts.getRawParameterValue (parameterID))
        return value->load();

    jassertfalse;
    return 0.0f;
}

void JuiceEQAudioProcessor::resetSmoothers()
{
    auto initialise = [this] (auto& smoother, const char* parameterID)
    {
        smoother.reset (sampleRateHz, 0.045);
        smoother.setCurrentAndTargetValue (normalisedParameter (getParameterValue (parameterID)));
    };

    initialise (depthSmoother,       ParameterIDs::depth);
    initialise (sharpnessSmoother,   ParameterIDs::sharpness);
    initialise (selectivitySmoother, ParameterIDs::selectivity);
    initialise (softHardSmoother,    ParameterIDs::softHard);
    initialise (mixSmoother,         ParameterIDs::mix);
}

void JuiceEQAudioProcessor::clearDspState()
{
    for (auto& channel : mainChannelState)
        channel.clear();

    for (auto& channel : sidechainChannelState)
        channel.clear();

    mainMagnitudeDb.fill (minimumDecibels);
    sidechainMagnitudeDb.fill (minimumDecibels);
    mainLocalAverageDb.fill (minimumDecibels);
    sidechainLocalAverageDb.fill (minimumDecibels);
    rawReduction.fill (0.0f);
    spreadReduction.fill (0.0f);
    smoothedReduction.fill (0.0f);
    binGain.fill (1.0f);

    samplesSinceLastFrame = 0;
    visualLevel.store (0.0f);
    averageSuppression.store (0.0f);

    for (auto& value : visualReduction)
        value.store (0.0f);
}

void JuiceEQAudioProcessor::initialiseWindow() noexcept
{
    for (int index = 0; index < fftSize; ++index)
    {
        // periodic Hann: 0.5 - 0.5*cos(2*pi*n/N)
        // sqrt를 씌우면 분석/합성에 같은 윈도우를 써도 곱은 Hann이 됩니다.
        const float phase = juce::MathConstants<float>::twoPi
                            * static_cast<float> (index)
                            / static_cast<float> (fftSize);

        const float hann = 0.5f - 0.5f * std::cos (phase);
        sqrtHannWindow[static_cast<size_t> (index)] = std::sqrt (juce::jmax (0.0f, hann));
    }
}

//==============================================================================
void JuiceEQAudioProcessor::pushMainSample (int channel, float sample) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    state.inputRing[static_cast<size_t> (state.inputWriteIndex)] = std::isfinite (sample) ? sample : 0.0f;
    state.inputWriteIndex = (state.inputWriteIndex + 1) % fftSize;
}

void JuiceEQAudioProcessor::pushSidechainSample (int channel, float sample) noexcept
{
    auto& state = sidechainChannelState[static_cast<size_t> (channel)];

    state.inputRing[static_cast<size_t> (state.inputWriteIndex)] = std::isfinite (sample) ? sample : 0.0f;
    state.inputWriteIndex = (state.inputWriteIndex + 1) % fftSize;
}

float JuiceEQAudioProcessor::readDryDelayThenStoreInput (int channel, float input) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    if (state.dryDelay.empty())
        return input;

    const size_t index = static_cast<size_t> (state.dryDelayIndex);
    const float output = state.dryDelay[index];
    state.dryDelay[index] = std::isfinite (input) ? input : 0.0f;

    return output;
}

float JuiceEQAudioProcessor::readAndClearProcessedSample (int channel) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    if (state.processedOverlap.empty())
        return 0.0f;

    const size_t index = static_cast<size_t> (state.overlapReadIndex);
    const float output = state.processedOverlap[index];
    state.processedOverlap[index] = 0.0f;

    return output;
}

float JuiceEQAudioProcessor::readAndClearDeltaSample (int channel) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    if (state.deltaOverlap.empty())
        return 0.0f;

    const size_t index = static_cast<size_t> (state.overlapReadIndex);
    const float output = state.deltaOverlap[index];
    state.deltaOverlap[index] = 0.0f;

    return output;
}

void JuiceEQAudioProcessor::advanceChannelCursors (int channel) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    if (! state.dryDelay.empty())
        state.dryDelayIndex = (state.dryDelayIndex + 1) % static_cast<int> (state.dryDelay.size());

    if (! state.processedOverlap.empty())
        state.overlapReadIndex = (state.overlapReadIndex + 1) % static_cast<int> (state.processedOverlap.size());
}

//==============================================================================
void JuiceEQAudioProcessor::processSpectralFrame (int numMainChannels,
                                                  int numSidechainChannels,
                                                  bool useSidechain,
                                                  const FrameParameters& parameters) noexcept
{
    buildMagnitudeAnalysis (numMainChannels, numSidechainChannels, useSidechain);
    calculateReductionCurve (useSidechain, parameters);

    for (int channel = 0; channel < numMainChannels; ++channel)
        renderChannelFrame (channel);

    updateVisualizerData();
}

void JuiceEQAudioProcessor::buildMagnitudeAnalysis (int numMainChannels,
                                                    int numSidechainChannels,
                                                    bool analyseSidechain) noexcept
{
    std::array<float, numFrequencyBins> mainLinear {};
    std::array<float, numFrequencyBins> sidechainLinear {};

    const int safeMainChannels = juce::jlimit (1, maximumAudioChannels, numMainChannels);

    for (int channel = 0; channel < safeMainChannels; ++channel)
    {
        const auto& state = mainChannelState[static_cast<size_t> (channel)];
        fillFftDataFromRing (state.inputRing, state.inputWriteIndex);

        fft.performRealOnlyForwardTransform (fftData.data(), true);

        for (int bin = 0; bin < numFrequencyBins; ++bin)
        {
            const float real = fftData[static_cast<size_t> (bin * 2)];
            const float imaginary = fftData[static_cast<size_t> (bin * 2 + 1)];
            const float scale = (bin == 0 || bin == numFrequencyBins - 1) ? 1.0f : 2.0f;
            const float magnitude = std::sqrt (real * real + imaginary * imaginary)
                                    * scale
                                    / static_cast<float> (fftSize);

            mainLinear[static_cast<size_t> (bin)] += magnitude;
        }
    }

    for (int bin = 0; bin < numFrequencyBins; ++bin)
    {
        const float averagedMagnitude = mainLinear[static_cast<size_t> (bin)]
                                        / static_cast<float> (safeMainChannels);
        mainMagnitudeDb[static_cast<size_t> (bin)] = safeDecibels (averagedMagnitude);
    }

    if (analyseSidechain && numSidechainChannels > 0)
    {
        const int safeSidechainChannels = juce::jlimit (1, maximumAudioChannels, numSidechainChannels);

        for (int channel = 0; channel < safeSidechainChannels; ++channel)
        {
            const auto& state = sidechainChannelState[static_cast<size_t> (channel)];
            fillFftDataFromRing (state.inputRing, state.inputWriteIndex);

            fft.performRealOnlyForwardTransform (fftData.data(), true);

            for (int bin = 0; bin < numFrequencyBins; ++bin)
            {
                const float real = fftData[static_cast<size_t> (bin * 2)];
                const float imaginary = fftData[static_cast<size_t> (bin * 2 + 1)];
                const float scale = (bin == 0 || bin == numFrequencyBins - 1) ? 1.0f : 2.0f;
                const float magnitude = std::sqrt (real * real + imaginary * imaginary)
                                        * scale
                                        / static_cast<float> (fftSize);

                sidechainLinear[static_cast<size_t> (bin)] += magnitude;
            }
        }

        for (int bin = 0; bin < numFrequencyBins; ++bin)
        {
            const float averagedMagnitude = sidechainLinear[static_cast<size_t> (bin)]
                                            / static_cast<float> (safeSidechainChannels);
            sidechainMagnitudeDb[static_cast<size_t> (bin)] = safeDecibels (averagedMagnitude);
        }
    }
    else
    {
        sidechainMagnitudeDb.fill (minimumDecibels);
    }
}

void JuiceEQAudioProcessor::calculateReductionCurve (bool useSidechain,
                                                     const FrameParameters& parameters) noexcept
{
    const float depth = juce::jlimit (0.0f, 1.0f, parameters.depth);
    const float sharpness = juce::jlimit (0.0f, 1.0f, parameters.sharpness);
    const float selectivity = juce::jlimit (0.0f, 1.0f, parameters.selectivity);
    const float hardness = juce::jlimit (0.0f, 1.0f, parameters.softHard);

    // 주변 평균을 계산할 때 보는 bin 범위입니다.
    // selectivity가 낮으면 더 넓은 문맥에서 "튀어나옴"을 찾고,
    // selectivity가 높으면 더 까다롭게 좁은 문제 대역만 봅니다.
    const int averageRadius = juce::jlimit (4, 96,
        static_cast<int> (std::round (juce::jmap (selectivity, 72.0f, 18.0f))));

    smoothAcrossFrequency (mainMagnitudeDb, mainLocalAverageDb, averageRadius);

    if (useSidechain)
        smoothAcrossFrequency (sidechainMagnitudeDb, sidechainLocalAverageDb, averageRadius);
    else
        sidechainLocalAverageDb.fill (minimumDecibels);

    const float resonanceThresholdDb = juce::jmap (selectivity, 2.0f, 11.5f);
    const float resonanceRangeDb = juce::jmap (selectivity, 24.0f, 14.0f);

    float sidechainPeakDb = minimumDecibels;

    if (useSidechain)
    {
        for (float value : sidechainMagnitudeDb)
            sidechainPeakDb = juce::jmax (sidechainPeakDb, value);
    }

    const bool sidechainHasSignal = useSidechain && sidechainPeakDb > -95.0f;

    for (int bin = 0; bin < numFrequencyBins; ++bin)
    {
        const float frequency = binToFrequency (bin, sampleRateHz);

        if (frequency < minimumUsefulFrequency || frequency > static_cast<float> (sampleRateHz * 0.485))
        {
            rawReduction[static_cast<size_t> (bin)] = 0.0f;
            continue;
        }

        const float mainDb = mainMagnitudeDb[static_cast<size_t> (bin)];
        const float localDb = mainLocalAverageDb[static_cast<size_t> (bin)];

        // 주변 대역보다 얼마나 튀어나왔는지입니다.
        // 예: mainDb가 -20 dB이고 주변 평균이 -32 dB면 protrusion은 12 dB입니다.
        const float protrusionDb = mainDb - localDb;
        const float audibleMain = smoothStep ((mainDb + 92.0f) / 52.0f);

        float resonanceAmount = (protrusionDb - resonanceThresholdDb) / resonanceRangeDb;
        resonanceAmount = smoothStep (juce::jlimit (0.0f, 1.0f, resonanceAmount));

        // Soft에서는 강한 공진에만 천천히 반응하고, Hard에서는 작은 돌출도 빠르게 잡습니다.
        const float kneeExponent = juce::jmap (hardness, 1.75f, 0.56f);
        resonanceAmount = std::pow (resonanceAmount, kneeExponent) * audibleMain;

        float targetAmount = resonanceAmount;

        if (sidechainHasSignal)
        {
            const float sidechainDb = sidechainMagnitudeDb[static_cast<size_t> (bin)];
            const float sidechainLocalDb = sidechainLocalAverageDb[static_cast<size_t> (bin)];

            // 사이드체인은 "공진"만 찾는 것이 아니라, 외부 신호가 실제로 차지하는
            // 주파수 윤곽을 메인에서 비우는 용도입니다. 그래서 절대 에너지와
            // 주변 대비 돌출을 함께 봅니다.
            const float sidechainRelativeToPeak = (sidechainDb - (sidechainPeakDb - 42.0f)) / 42.0f;
            const float sidechainProminence = (sidechainDb - sidechainLocalDb + 4.0f) / 26.0f;
            const float sidechainAudible = smoothStep ((sidechainDb + 96.0f) / 54.0f);

            const float sidechainMask = juce::jmax (
                smoothStep (juce::jlimit (0.0f, 1.0f, sidechainRelativeToPeak)) * 0.78f,
                smoothStep (juce::jlimit (0.0f, 1.0f, sidechainProminence)) * 0.52f);

            targetAmount = juce::jmax (targetAmount, sidechainMask * sidechainAudible * audibleMain);
        }

        rawReduction[static_cast<size_t> (bin)] = juce::jlimit (0.0f, 1.0f, targetAmount);
    }

    // sharpness가 낮으면 넓게, 높으면 좁게 퍼뜨립니다.
    // max 기반으로 퍼뜨리는 이유는 강한 공진 하나가 주변 bin까지 자연스럽게 영향을 줘야 하고,
    // 단순 평균 blur는 peak를 너무 희석하기 때문입니다.
    const int spreadRadius = juce::jlimit (1, 34,
        static_cast<int> (std::round (juce::jmap (sharpness, 28.0f, 2.0f))));
    const float sigma = juce::jmax (1.0f, static_cast<float> (spreadRadius) * 0.44f);

    for (int bin = 0; bin < numFrequencyBins; ++bin)
    {
        float maximumWeightedReduction = 0.0f;

        const int firstBin = juce::jmax (0, bin - spreadRadius);
        const int lastBin = juce::jmin (numFrequencyBins - 1, bin + spreadRadius);

        for (int neighbour = firstBin; neighbour <= lastBin; ++neighbour)
        {
            const float distance = static_cast<float> (neighbour - bin);
            const float weight = std::exp (-0.5f * (distance * distance) / (sigma * sigma));
            maximumWeightedReduction = juce::jmax (
                maximumWeightedReduction,
                rawReduction[static_cast<size_t> (neighbour)] * weight);
        }

        spreadReduction[static_cast<size_t> (bin)] = maximumWeightedReduction;
    }

    const float attack = juce::jmap (hardness, 0.48f, 0.78f);
    const float release = juce::jmap (hardness, 0.10f, 0.22f);
    const float maximumReductionDb = juce::jmap (depth, 0.0f, 42.0f);

    float sumReduction = 0.0f;
    float peakReduction = 0.0f;

    for (int bin = 0; bin < numFrequencyBins; ++bin)
    {
        const float target = spreadReduction[static_cast<size_t> (bin)];
        float current = smoothedReduction[static_cast<size_t> (bin)];

        current += (target - current) * (target > current ? attack : release);
        current = juce::jlimit (0.0f, 1.0f, current);

        smoothedReduction[static_cast<size_t> (bin)] = current;

        const float gain = juce::Decibels::decibelsToGain (-maximumReductionDb * current);
        binGain[static_cast<size_t> (bin)] = juce::jlimit (0.02f, 1.0f, gain);

        const float removed = 1.0f - binGain[static_cast<size_t> (bin)];
        sumReduction += removed;
        peakReduction = juce::jmax (peakReduction, removed);
    }

    averageSuppression.store (juce::jlimit (0.0f, 1.0f, sumReduction / static_cast<float> (numFrequencyBins)));
    visualLevel.store (juce::jlimit (0.0f, 1.0f, peakReduction));
}

void JuiceEQAudioProcessor::renderChannelFrame (int channel) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    fillFftDataFromRing (state.inputRing, state.inputWriteIndex);
    fft.performRealOnlyForwardTransform (fftData.data(), true);

    deltaFftData.fill (0.0f);

    for (int bin = 0; bin < numFrequencyBins; ++bin)
    {
        const size_t realIndex = static_cast<size_t> (bin * 2);
        const size_t imaginaryIndex = static_cast<size_t> (bin * 2 + 1);

        const float real = fftData[realIndex];
        const float imaginary = fftData[imaginaryIndex];
        const float gain = binGain[static_cast<size_t> (bin)];
        const float removedGain = 1.0f - gain;

        fftData[realIndex] = real * gain;
        fftData[imaginaryIndex] = imaginary * gain;

        deltaFftData[realIndex] = real * removedGain;
        deltaFftData[imaginaryIndex] = imaginary * removedGain;
    }

    fft.performRealOnlyInverseTransform (fftData.data());
    fft.performRealOnlyInverseTransform (deltaFftData.data());

    addFrameToOverlapBuffers (channel);
}

void JuiceEQAudioProcessor::addFrameToOverlapBuffers (int channel) noexcept
{
    auto& state = mainChannelState[static_cast<size_t> (channel)];

    if (state.processedOverlap.empty() || state.deltaOverlap.empty())
        return;

    // 현재 sample의 output slot은 이미 읽고 지웠으므로, 새 프레임은 반드시 다음
    // sample 위치부터 더합니다. 이 스케줄링 때문에 전체 지연은 정확히 fftSize입니다.
    int writeIndex = (state.overlapReadIndex + 1) % static_cast<int> (state.processedOverlap.size());

    for (int sample = 0; sample < fftSize; ++sample)
    {
        const float synthesis = sqrtHannWindow[static_cast<size_t> (sample)] * overlapAddGain;

        state.processedOverlap[static_cast<size_t> (writeIndex)] += fftData[static_cast<size_t> (sample)] * synthesis;
        state.deltaOverlap[static_cast<size_t> (writeIndex)] += deltaFftData[static_cast<size_t> (sample)] * synthesis;

        if (++writeIndex >= static_cast<int> (state.processedOverlap.size()))
            writeIndex = 0;
    }
}

void JuiceEQAudioProcessor::updateVisualizerData() noexcept
{
    const float nyquist = static_cast<float> (sampleRateHz * 0.5);
    const float maximumFrequency = juce::jmax (visualMinimumFrequency * 2.0f, nyquist * 0.98f);

    for (int visualBin = 0; visualBin < visualizerBinCount; ++visualBin)
    {
        const float startNormalised = static_cast<float> (visualBin)
                                      / static_cast<float> (visualizerBinCount);
        const float endNormalised = static_cast<float> (visualBin + 1)
                                    / static_cast<float> (visualizerBinCount);

        const float startFrequency = visualMinimumFrequency
                                     * std::pow (maximumFrequency / visualMinimumFrequency, startNormalised);
        const float endFrequency = visualMinimumFrequency
                                   * std::pow (maximumFrequency / visualMinimumFrequency, endNormalised);

        const int firstBin = frequencyToBin (startFrequency, sampleRateHz);
        const int lastBin = juce::jmax (firstBin, frequencyToBin (endFrequency, sampleRateHz));

        float peak = 0.0f;

        for (int bin = firstBin; bin <= lastBin && bin < numFrequencyBins; ++bin)
            peak = juce::jmax (peak, 1.0f - binGain[static_cast<size_t> (bin)]);

        const float previous = visualReduction[static_cast<size_t> (visualBin)].load();
        const float smoothed = peak > previous
            ? peak
            : previous * 0.82f + peak * 0.18f;

        visualReduction[static_cast<size_t> (visualBin)].store (juce::jlimit (0.0f, 1.0f, smoothed));
    }
}

void JuiceEQAudioProcessor::fillFftDataFromRing (const std::array<float, fftSize>& ring,
                                                 int writeIndex) noexcept
{
    // writeIndex는 다음에 쓸 위치, 즉 현재 프레임에서 가장 오래된 샘플 위치입니다.
    // 여기서부터 끝까지 읽고, 다시 0번부터 writeIndex 직전까지 읽으면 시간 순서가 됩니다.
    for (int sample = 0; sample < fftSize; ++sample)
    {
        const int ringIndex = (writeIndex + sample) % fftSize;
        fftData[static_cast<size_t> (sample)] =
            ring[static_cast<size_t> (ringIndex)] * sqrtHannWindow[static_cast<size_t> (sample)];
    }

    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
}

void JuiceEQAudioProcessor::smoothAcrossFrequency (
    const std::array<float, numFrequencyBins>& source,
    std::array<float, numFrequencyBins>& destination,
    int radius) const noexcept
{
    const int safeRadius = juce::jlimit (1, 128, radius);

    for (int bin = 0; bin < numFrequencyBins; ++bin)
    {
        const int firstBin = juce::jmax (0, bin - safeRadius);
        const int lastBin = juce::jmin (numFrequencyBins - 1, bin + safeRadius);

        float weightedSum = 0.0f;
        float weightSum = 0.0f;

        for (int neighbour = firstBin; neighbour <= lastBin; ++neighbour)
        {
            const float distance = std::abs (static_cast<float> (neighbour - bin))
                                   / static_cast<float> (safeRadius);
            const float weight = 1.0f - distance * 0.65f;

            weightedSum += source[static_cast<size_t> (neighbour)] * weight;
            weightSum += weight;
        }

        destination[static_cast<size_t> (bin)] =
            weightSum > 0.0f ? weightedSum / weightSum : source[static_cast<size_t> (bin)];
    }
}

//==============================================================================
float JuiceEQAudioProcessor::softLimitSample (float input) noexcept
{
    if (! std::isfinite (input))
        return 0.0f;

    constexpr float ceiling = 0.995f;
    return ceiling * std::tanh (input / ceiling);
}

float JuiceEQAudioProcessor::smoothStep (float value) noexcept
{
    const float x = juce::jlimit (0.0f, 1.0f, value);
    return x * x * (3.0f - 2.0f * x);
}

float JuiceEQAudioProcessor::safeDecibels (float linearGain) noexcept
{
    if (! std::isfinite (linearGain) || linearGain <= 0.0f)
        return minimumDecibels;

    return juce::Decibels::gainToDecibels (linearGain, minimumDecibels);
}

float JuiceEQAudioProcessor::normalisedParameter (float percentValue) noexcept
{
    return juce::jlimit (0.0f, 1.0f, percentValue * 0.01f);
}

float JuiceEQAudioProcessor::binToFrequency (int bin, double sampleRate) noexcept
{
    return static_cast<float> (bin) * static_cast<float> (sampleRate) / static_cast<float> (fftSize);
}

int JuiceEQAudioProcessor::frequencyToBin (float frequency, double sampleRate) noexcept
{
    if (! isFiniteAndPositive (sampleRate))
        return 0;

    const float bin = frequency * static_cast<float> (fftSize) / static_cast<float> (sampleRate);
    return juce::jlimit (0, numFrequencyBins - 1, static_cast<int> (std::floor (bin)));
}

bool JuiceEQAudioProcessor::isFiniteAndPositive (double value) noexcept
{
    return std::isfinite (value) && value > 0.0;
}

//==============================================================================
void JuiceEQAudioProcessor::MainChannelState::prepare (int newOverlapRingSize,
                                                       int latencySamples)
{
    const int safeOverlapSize = juce::jmax (fftSize * 2, newOverlapRingSize);
    const int safeLatency = juce::jmax (1, latencySamples);

    dryDelay.assign (static_cast<size_t> (safeLatency), 0.0f);
    processedOverlap.assign (static_cast<size_t> (safeOverlapSize), 0.0f);
    deltaOverlap.assign (static_cast<size_t> (safeOverlapSize), 0.0f);

    clear();
}

void JuiceEQAudioProcessor::MainChannelState::clear() noexcept
{
    inputRing.fill (0.0f);
    std::fill (dryDelay.begin(), dryDelay.end(), 0.0f);
    std::fill (processedOverlap.begin(), processedOverlap.end(), 0.0f);
    std::fill (deltaOverlap.begin(), deltaOverlap.end(), 0.0f);

    inputWriteIndex = 0;
    dryDelayIndex = 0;
    overlapReadIndex = 0;
}

void JuiceEQAudioProcessor::SidechainChannelState::clear() noexcept
{
    inputRing.fill (0.0f);
    inputWriteIndex = 0;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuiceEQAudioProcessor();
}
