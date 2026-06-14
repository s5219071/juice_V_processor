#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

namespace
{
    constexpr float lowCutFrequency = 110.0f;
    constexpr float tubeFrequency = 18000.0f;

    constexpr std::array<float, JuiceEQAudioProcessor::dynamicBandCount> dynamicFrequencies {
        200.0f, 3600.0f, 10000.0f
    };

    float clampFrequency (float frequency, double sampleRate) noexcept
    {
        return juce::jlimit (10.0f, static_cast<float> (sampleRate * 0.45), frequency);
    }

    float coefficientFromMilliseconds (double sampleRate, float milliseconds) noexcept
    {
        return std::exp (-1.0f / static_cast<float> (sampleRate * milliseconds * 0.001));
    }

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    float biquadMagnitude (const Biquad& c, float frequency, double sampleRate) noexcept
    {
        const double w = juce::MathConstants<double>::twoPi
                         * static_cast<double> (frequency) / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto numerator = c.b0 + c.b1 * z1 + c.b2 * z2;
        const auto denominator = 1.0 + c.a1 * z1 + c.a2 * z2;
        return static_cast<float> (std::abs (numerator / denominator));
    }

    Biquad makeHighPassDisplay (double sampleRate, float cutoff, float q) noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * cutoff / sampleRate;
        const double cosine = std::cos (w);
        const double alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        return {
            ((1.0 + cosine) * 0.5) / a0,
            (-(1.0 + cosine)) / a0,
            ((1.0 + cosine) * 0.5) / a0,
            (-2.0 * cosine) / a0,
            (1.0 - alpha) / a0
        };
    }

    Biquad makePeakDisplay (double sampleRate, float cutoff, float q, float gainDb) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * cutoff / sampleRate;
        const double cosine = std::cos (w);
        const double alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha / a;

        return {
            (1.0 + alpha * a) / a0,
            (-2.0 * cosine) / a0,
            (1.0 - alpha * a) / a0,
            (-2.0 * cosine) / a0,
            (1.0 - alpha / a) / a0
        };
    }

    Biquad makeHighShelfDisplay (double sampleRate, float cutoff, float q, float gainDb) noexcept
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * cutoff / sampleRate;
        const double cosine = std::cos (w);
        const double alpha = std::sin (w) / (2.0 * q);
        const double beta = 2.0 * std::sqrt (a) * alpha;
        const double a0 = (a + 1.0) - (a - 1.0) * cosine + beta;

        return {
            a * ((a + 1.0) + (a - 1.0) * cosine + beta) / a0,
            -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine) / a0,
            a * ((a + 1.0) + (a - 1.0) * cosine - beta) / a0,
            2.0 * ((a - 1.0) - (a + 1.0) * cosine) / a0,
            ((a + 1.0) - (a - 1.0) * cosine - beta) / a0
        };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout JuiceEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::clean, 1 },
        "Clean",
        juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::brightness, 1 },
        "Brightness",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        25.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    parameters.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::oversampling, 1 },
        "Oversampling",
        juce::StringArray { "1x", "2x", "4x", "8x" },
        2));

    return { parameters.begin(), parameters.end() };
}

JuiceEQAudioProcessor::JuiceEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cleanParameter = apvts.getRawParameterValue (ParameterIDs::clean);
    brightnessParameter = apvts.getRawParameterValue (ParameterIDs::brightness);
    oversamplingParameter = apvts.getRawParameterValue (ParameterIDs::oversampling);

    for (auto& meter : dynamicReductionMeters)
        meter.store (0.0f);
}

JuiceEQAudioProcessor::~JuiceEQAudioProcessor() = default;

void JuiceEQAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    newSampleRate = juce::jmax (1.0, newSampleRate);
    sampleRateHz.store (newSampleRate);
    preparedChannels = juce::jlimit (1, maximumChannels, getTotalNumOutputChannels());
    preparedBlockSize = juce::jmax (1, samplesPerBlock);

    rebuildOversamplers (preparedChannels, preparedBlockSize);

    cleanSmoother.reset (newSampleRate, 0.04);
    brightnessSmoother.reset (newSampleRate, 0.04);
    cleanSmoother.setCurrentAndTargetValue (getParameter (cleanParameter, 100.0f) * 0.01f);
    brightnessSmoother.setCurrentAndTargetValue (getParameter (brightnessParameter, 25.0f) * 0.01f);

    activeOversamplingChoice = -1;
    updateOversamplingChoice (static_cast<int> (getParameter (oversamplingParameter, 2.0f)));
    analyzerFifo.reset();
}

void JuiceEQAudioProcessor::releaseResources()
{
    resetDspState();
}

bool JuiceEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output
           && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void JuiceEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0 || outputChannels == 0)
        return;

    const int requestedChoice = juce::jlimit (
        0, 3, static_cast<int> (getParameter (oversamplingParameter, 2.0f)));
    updateOversamplingChoice (requestedChoice);

    cleanSmoother.setTargetValue (getParameter (cleanParameter, 100.0f) * 0.01f);
    brightnessSmoother.setTargetValue (getParameter (brightnessParameter, 25.0f) * 0.01f);

    const float cleanScale = cleanSmoother.skip (buffer.getNumSamples());
    const float brightnessAmount = brightnessSmoother.skip (buffer.getNumSamples());
    const double processingRate = sampleRateHz.load() * static_cast<double> (1 << activeOversamplingChoice);

    juce::dsp::AudioBlock<float> block (buffer);

    if (activeOversamplingChoice == 0)
    {
        processDspBlock (block, processingRate, cleanScale, brightnessAmount);
    }
    else
    {
        auto& oversampler = *oversamplers[static_cast<size_t> (activeOversamplingChoice - 1)];
        auto oversampledBlock = oversampler.processSamplesUp (block);
        processDspBlock (oversampledBlock, processingRate, cleanScale, brightnessAmount);
        oversampler.processSamplesDown (block);
    }

    updateDynamicMeters();
    pushAnalyzerSamples (buffer);
}

void JuiceEQAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void JuiceEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void JuiceEQAudioProcessor::rebuildOversamplers (int channelCount, int maximumBlockSize)
{
    for (int stage = 1; stage <= 3; ++stage)
    {
        auto oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            static_cast<size_t> (channelCount),
            static_cast<size_t> (stage),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,
            true);
        oversampler->initProcessing (static_cast<size_t> (maximumBlockSize));
        oversampler->reset();
        oversamplers[static_cast<size_t> (stage - 1)] = std::move (oversampler);
    }
}

void JuiceEQAudioProcessor::updateOversamplingChoice (int choice)
{
    choice = juce::jlimit (0, 3, choice);

    if (choice == activeOversamplingChoice)
        return;

    activeOversamplingChoice = choice;
    resetDspState();

    if (choice == 0)
    {
        setLatencySamples (0);
    }
    else
    {
        auto& oversampler = *oversamplers[static_cast<size_t> (choice - 1)];
        oversampler.reset();
        setLatencySamples (static_cast<int> (std::round (oversampler.getLatencyInSamples())));
    }

    const double processingRate = sampleRateHz.load() * static_cast<double> (1 << choice);

    for (auto& channel : channels)
        channel.prepare (processingRate);

    updateFilterCoefficients (processingRate,
                              getParameter (cleanParameter, 100.0f) * 0.01f,
                              getParameter (brightnessParameter, 25.0f) * 0.01f);
}

void JuiceEQAudioProcessor::updateFilterCoefficients (double processingRate,
                                                       float cleanScale,
                                                       float brightnessAmount)
{
    for (auto& channel : channels)
        channel.updateCoefficients (processingRate, cleanScale, brightnessAmount);
}

void JuiceEQAudioProcessor::processDspBlock (juce::dsp::AudioBlock<float> block,
                                             double processingRate,
                                             float cleanScale,
                                             float brightnessAmount) noexcept
{
    updateFilterCoefficients (processingRate, cleanScale, brightnessAmount);

    const int channelCount = juce::jmin (maximumChannels, static_cast<int> (block.getNumChannels()));
    const int sampleCount = static_cast<int> (block.getNumSamples());

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto* samples = block.getChannelPointer (static_cast<size_t> (channel));
        auto& dsp = channels[static_cast<size_t> (channel)];

        for (int sample = 0; sample < sampleCount; ++sample)
            samples[sample] = dsp.process (samples[sample], cleanScale, brightnessAmount);
    }
}

void JuiceEQAudioProcessor::pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer) noexcept
{
    if (buffer.getNumChannels() == 0)
        return;

    const int requested = juce::jmin (buffer.getNumSamples(), analyzerFifo.getFreeSpace());
    if (requested <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    analyzerFifo.prepareToWrite (requested, start1, size1, start2, size2);
    const float* source = buffer.getReadPointer (0);

    if (size1 > 0)
        std::memcpy (analyzerStorage.data() + start1, source, static_cast<size_t> (size1) * sizeof (float));
    if (size2 > 0)
        std::memcpy (analyzerStorage.data() + start2, source + size1, static_cast<size_t> (size2) * sizeof (float));

    analyzerFifo.finishedWrite (size1 + size2);
}

int JuiceEQAudioProcessor::pullAnalyzerSamples (float* destination, int maximumSamples) noexcept
{
    if (destination == nullptr || maximumSamples <= 0)
        return 0;

    const int requested = juce::jmin (maximumSamples, analyzerFifo.getNumReady());
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    analyzerFifo.prepareToRead (requested, start1, size1, start2, size2);

    if (size1 > 0)
        std::memcpy (destination, analyzerStorage.data() + start1, static_cast<size_t> (size1) * sizeof (float));
    if (size2 > 0)
        std::memcpy (destination + size1, analyzerStorage.data() + start2, static_cast<size_t> (size2) * sizeof (float));

    analyzerFifo.finishedRead (size1 + size2);
    return size1 + size2;
}

void JuiceEQAudioProcessor::updateDynamicMeters() noexcept
{
    for (int band = 0; band < dynamicBandCount; ++band)
    {
        float maximum = 0.0f;
        for (int channel = 0; channel < preparedChannels; ++channel)
            maximum = juce::jmax (maximum, channels[static_cast<size_t> (channel)]
                                               .dynamicBands[static_cast<size_t> (band)]
                                               .reductionDb);

        const float previous = dynamicReductionMeters[static_cast<size_t> (band)].load();
        dynamicReductionMeters[static_cast<size_t> (band)].store (
            maximum > previous ? maximum : previous * 0.90f + maximum * 0.10f);
    }
}

void JuiceEQAudioProcessor::resetDspState() noexcept
{
    for (auto& channel : channels)
        channel.reset();

    for (auto& oversampler : oversamplers)
        if (oversampler != nullptr)
            oversampler->reset();

    for (auto& meter : dynamicReductionMeters)
        meter.store (0.0f);
}

float JuiceEQAudioProcessor::getMagnitudeResponseDb (float frequencyHz) const noexcept
{
    const double rate = sampleRateHz.load();
    const float clean = getParameter (cleanParameter, 100.0f) * 0.01f;
    const float brightness = getParameter (brightnessParameter, 25.0f) * 0.01f;
    const float frequency = juce::jlimit (20.0f, static_cast<float> (rate * 0.49), frequencyHz);

    float magnitude = magnitudeForHighPass (frequency, rate, lowCutFrequency, 0.7071f);
    magnitude *= magnitudeForPeak (frequency, rate, 450.0f, 0.90f, 1.0f * clean);
    magnitude *= magnitudeForPeak (frequency, rate, 2000.0f, 1.00f, 2.5f * clean);
    magnitude *= magnitudeForPeak (frequency, rate, 8000.0f, 0.82f, 3.0f * clean);
    magnitude *= magnitudeForHighShelf (frequency, rate, clampFrequency (20000.0f, rate), 1.15f, 10.0f * clean);
    magnitude *= magnitudeForHighShelf (frequency, rate, clampFrequency (18000.0f, rate), 0.66f, 10.0f * brightness);

    float responseDb = juce::Decibels::gainToDecibels (magnitude, -60.0f);
    constexpr std::array<float, dynamicBandCount> widthsInOctaves { 0.82f, 0.52f, 0.42f };

    for (int band = 0; band < dynamicBandCount; ++band)
    {
        const float octaves = std::log2 (frequency / dynamicFrequencies[static_cast<size_t> (band)]);
        const float width = widthsInOctaves[static_cast<size_t> (band)];
        const float shape = std::exp (-0.5f * (octaves * octaves) / (width * width));
        responseDb -= getDynamicReductionDb (band) * shape;
    }

    return juce::jlimit (-60.0f, 30.0f, responseDb);
}

float JuiceEQAudioProcessor::getDynamicReductionDb (int band) const noexcept
{
    if (band < 0 || band >= dynamicBandCount)
        return 0.0f;
    return dynamicReductionMeters[static_cast<size_t> (band)].load();
}

float JuiceEQAudioProcessor::getParameter (const std::atomic<float>* parameter, float fallback) noexcept
{
    return parameter != nullptr ? parameter->load() : fallback;
}

float JuiceEQAudioProcessor::magnitudeForHighPass (float frequency,
                                                   double sampleRate,
                                                   float cutoff,
                                                   float q) noexcept
{
    return biquadMagnitude (makeHighPassDisplay (sampleRate, clampFrequency (cutoff, sampleRate), q),
                            frequency,
                            sampleRate);
}

float JuiceEQAudioProcessor::magnitudeForPeak (float frequency,
                                               double sampleRate,
                                               float cutoff,
                                               float q,
                                               float gainDb) noexcept
{
    return biquadMagnitude (makePeakDisplay (sampleRate, clampFrequency (cutoff, sampleRate), q, gainDb),
                            frequency,
                            sampleRate);
}

float JuiceEQAudioProcessor::magnitudeForHighShelf (float frequency,
                                                    double sampleRate,
                                                    float cutoff,
                                                    float q,
                                                    float gainDb) noexcept
{
    return biquadMagnitude (makeHighShelfDisplay (sampleRate, clampFrequency (cutoff, sampleRate), q, gainDb),
                            frequency,
                            sampleRate);
}

JuiceEQAudioProcessor::DynamicBand::DynamicBand()
{
    detector.coefficients = new Coefficients (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
}

void JuiceEQAudioProcessor::DynamicBand::prepare (double processingRate)
{
    *detector.coefficients = ArrayCoefficients::makeBandPass (
        processingRate, clampFrequency (frequency, processingRate), q);
    attackCoefficient = coefficientFromMilliseconds (processingRate, 3.0f);
    releaseCoefficient = coefficientFromMilliseconds (processingRate, 90.0f);
    gainAttackCoefficient = coefficientFromMilliseconds (processingRate, 7.0f);
    gainReleaseCoefficient = coefficientFromMilliseconds (processingRate, 130.0f);
    reset();
}

void JuiceEQAudioProcessor::DynamicBand::reset() noexcept
{
    detector.reset();
    envelope = 0.0f;
    reductionDb = 0.0f;
}

float JuiceEQAudioProcessor::DynamicBand::process (float input, float cleanScale) noexcept
{
    const float bandSignal = detector.processSample (input);
    const float detectorValue = std::abs (bandSignal);
    const float envelopeCoefficient = detectorValue > envelope ? attackCoefficient : releaseCoefficient;
    envelope = detectorValue + envelopeCoefficient * (envelope - detectorValue);

    const float levelDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
    const float overThresholdDb = juce::jmax (0.0f, levelDb - thresholdDb);
    const float compressedDb = overThresholdDb * (1.0f - 1.0f / ratio);
    const float targetReductionDb = juce::jmin (maximumDepthDb * cleanScale, compressedDb * cleanScale);
    const float gainCoefficient = targetReductionDb > reductionDb
                                      ? gainAttackCoefficient
                                      : gainReleaseCoefficient;
    reductionDb = targetReductionDb + gainCoefficient * (reductionDb - targetReductionDb);

    const float removal = 1.0f - juce::Decibels::decibelsToGain (-reductionDb);
    return input - bandSignal * removal;
}

JuiceEQAudioProcessor::ChannelDsp::ChannelDsp()
{
    auto initialise = [] (Filter& filter)
    {
        filter.coefficients = new Coefficients (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    };

    initialise (lowCut);
    initialise (bell450);
    initialise (bell2k);
    initialise (bell8k);
    initialise (shelf20k);
    initialise (tubeHighPass);
    initialise (tubeWetHighPass);
    initialise (brightnessShelf);
}

void JuiceEQAudioProcessor::ChannelDsp::prepare (double processingRate)
{
    dynamicBands[0].frequency = 200.0f;
    dynamicBands[0].q = 0.85f;
    dynamicBands[0].thresholdDb = -36.0f;
    dynamicBands[0].ratio = 2.4f;
    dynamicBands[0].maximumDepthDb = 3.0f;

    dynamicBands[1].frequency = 3600.0f;
    dynamicBands[1].q = 2.0f;
    dynamicBands[1].thresholdDb = -31.0f;
    dynamicBands[1].ratio = 3.2f;
    dynamicBands[1].maximumDepthDb = 5.0f;

    dynamicBands[2].frequency = 10000.0f;
    dynamicBands[2].q = 2.6f;
    dynamicBands[2].thresholdDb = -34.0f;
    dynamicBands[2].ratio = 3.5f;
    dynamicBands[2].maximumDepthDb = 6.0f;

    for (auto& band : dynamicBands)
        band.prepare (processingRate);

    lastProcessingRate = 0.0;
    lastCleanScale = -1.0f;
    lastBrightnessAmount = -1.0f;
}

void JuiceEQAudioProcessor::ChannelDsp::updateCoefficients (double processingRate,
                                                            float cleanScale,
                                                            float brightnessAmount)
{
    if (std::abs (processingRate - lastProcessingRate) < 0.5
        && std::abs (cleanScale - lastCleanScale) < 0.0005f
        && std::abs (brightnessAmount - lastBrightnessAmount) < 0.0005f)
        return;

    lastProcessingRate = processingRate;
    lastCleanScale = cleanScale;
    lastBrightnessAmount = brightnessAmount;

    *lowCut.coefficients = ArrayCoefficients::makeHighPass (
        processingRate, clampFrequency (lowCutFrequency, processingRate), 0.7071f);
    *bell450.coefficients = ArrayCoefficients::makePeakFilter (
        processingRate, clampFrequency (450.0f, processingRate), 0.90f,
        juce::Decibels::decibelsToGain (1.0f * cleanScale));
    *bell2k.coefficients = ArrayCoefficients::makePeakFilter (
        processingRate, clampFrequency (2000.0f, processingRate), 1.00f,
        juce::Decibels::decibelsToGain (2.5f * cleanScale));
    *bell8k.coefficients = ArrayCoefficients::makePeakFilter (
        processingRate, clampFrequency (8000.0f, processingRate), 0.82f,
        juce::Decibels::decibelsToGain (3.0f * cleanScale));
    *shelf20k.coefficients = ArrayCoefficients::makeHighShelf (
        processingRate, clampFrequency (20000.0f, processingRate), 1.15f,
        juce::Decibels::decibelsToGain (10.0f * cleanScale));
    *tubeHighPass.coefficients = ArrayCoefficients::makeHighPass (
        processingRate, clampFrequency (tubeFrequency, processingRate), 0.7071f);
    *tubeWetHighPass.coefficients = ArrayCoefficients::makeHighPass (
        processingRate, clampFrequency (tubeFrequency, processingRate), 0.7071f);
    *brightnessShelf.coefficients = ArrayCoefficients::makeHighShelf (
        processingRate, clampFrequency (tubeFrequency, processingRate), 0.66f,
        juce::Decibels::decibelsToGain (10.0f * brightnessAmount));
}

void JuiceEQAudioProcessor::ChannelDsp::reset() noexcept
{
    lowCut.reset();
    bell450.reset();
    bell2k.reset();
    bell8k.reset();
    shelf20k.reset();
    tubeHighPass.reset();
    tubeWetHighPass.reset();
    brightnessShelf.reset();

    for (auto& band : dynamicBands)
        band.reset();
}

float JuiceEQAudioProcessor::ChannelDsp::process (float input,
                                                  float cleanScale,
                                                  float brightnessAmount) noexcept
{
    float sample = lowCut.processSample (input);
    sample = dynamicBands[0].process (sample, cleanScale);
    sample = bell450.processSample (sample);
    sample = bell2k.processSample (sample);
    sample = dynamicBands[1].process (sample, cleanScale);
    sample = bell8k.processSample (sample);
    sample = dynamicBands[2].process (sample, cleanScale);
    sample = shelf20k.processSample (sample);

    const float high = tubeHighPass.processSample (sample);
    const float drive = 1.0f + brightnessAmount * 3.0f;
    constexpr float bias = 0.22f;
    const float biasTanh = std::tanh (bias);
    const float normalisation = drive * (1.0f - biasTanh * biasTanh);
    const float saturated = (std::tanh (drive * high + bias) - biasTanh)
                            / juce::jmax (0.001f, normalisation);
    const float tubeWet = tubeWetHighPass.processSample (saturated - high);
    sample += tubeWet * brightnessAmount * 0.70f;

    return brightnessShelf.processSample (sample);
}

juce::AudioProcessorEditor* JuiceEQAudioProcessor::createEditor()
{
    return new JuiceEQAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuiceEQAudioProcessor();
}
