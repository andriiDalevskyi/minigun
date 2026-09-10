#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/EngineKit.h"
#include "Model/KitStore.h"

namespace
{
    constexpr const char* kMasterParamId = "master";
}

MinigunAudioProcessor::BusesProperties MinigunAudioProcessor::makeBusesProperties()
{
    auto props = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    // Aux buses are enabled by default: hosts such as REAPER do not activate optional
    // VST3 output buses on their own, so a disabled-by-default bus would never receive audio.
    for (int i = 1; i <= 16; ++i)
        props = props.withOutput ("Out " + juce::String (i), juce::AudioChannelSet::stereo(), true);
    return props;
}

//==============================================================================
MinigunAudioProcessor::MinigunAudioProcessor()
    : AudioProcessor (makeBusesProperties()),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    kitEdited(); // publish the initial (empty) kit snapshot
    startTimerHz (30);
}

MinigunAudioProcessor::~MinigunAudioProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout MinigunAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMasterParamId, 1 },
        "Master",
        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String MinigunAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

void MinigunAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    samplerEngine.prepare (sampleRate, samplesPerBlock);
}

void MinigunAudioProcessor::releaseResources()
{
}

bool MinigunAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    // Every aux bus must be either disabled or stereo.
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        auto set = layouts.getChannelSet (false, i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void MinigunAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // the host may hand us garbage in disabled/unused channels

    for (const auto metadata : midiMessages)
    {
        if (metadata.getMessage().isNoteOn())
        {
            midiActivity.store (true, std::memory_order_release);
            break;
        }
    }

    samplerEngine.processBlock (*this, buffer, midiMessages);

    const float masterDb = apvts.getRawParameterValue (kMasterParamId)->load();
    const float masterGain = juce::Decibels::decibelsToGain (masterDb, -60.0f);
    buffer.applyGain (masterGain); // applies to every bus (all channels in the buffer)

    // Peak meter = max over every currently enabled stereo bus.
    const int numSamples = buffer.getNumSamples();
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < getBusCount (false); ++i)
    {
        auto* bus = getBus (false, i);
        if (bus == nullptr || ! bus->isEnabled())
            continue;
        auto busBuf = getBusBuffer (buffer, false, i);
        if (busBuf.getNumChannels() < 2)
            continue;
        peakL = juce::jmax (peakL, busBuf.getMagnitude (0, 0, numSamples));
        peakR = juce::jmax (peakR, busBuf.getMagnitude (1, 0, numSamples));
    }
    outputPeakL.store (peakL, std::memory_order_relaxed);
    outputPeakR.store (peakR, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* MinigunAudioProcessor::createEditor()
{
    return new MinigunAudioProcessorEditor (*this);
}

//==============================================================================
void MinigunAudioProcessor::kitEdited()
{
    // (a) Synchronously (re)load samples on the message thread; flag missing files.
    for (auto& pad : kit.pads)
        for (auto& layer : pad.layers)
            for (auto& ref : layer.samples)
                ref.missing = (sampleLoader.load (ref.file) == nullptr);

    // (b) Build and publish a new immutable snapshot for the audio thread.
    samplerEngine.setKitSnapshot (minigun::EngineKit::build (kit, sampleLoader));

    // (c) Notify all UI panels.
    sendChangeMessage();
}

void MinigunAudioProcessor::triggerPad (int padIndex, int velocity)
{
    samplerEngine.triggerPad (padIndex, velocity);
}

void MinigunAudioProcessor::previewFile (const juce::File& file)
{
    samplerEngine.requestPreview (sampleLoader.load (file));
}

void MinigunAudioProcessor::stopPreview()
{
    samplerEngine.requestStopPreview();
}

int MinigunAudioProcessor::getAndClearPadHit (int padIndex, int* layerIndexOut)
{
    const int packed = samplerEngine.getAndClearPadHit (padIndex);
    if (layerIndexOut != nullptr)
        *layerIndexOut = ((packed >> 8) & 0xff) - 1;
    return packed & 0xff;
}

void MinigunAudioProcessor::armMidiLearn (int padIndex)
{
    armedLearnPad = padIndex;
    samplerEngine.armMidiLearn (padIndex);
}

bool MinigunAudioProcessor::isLearnArmed() const
{
    return samplerEngine.isLearnArmed();
}

void MinigunAudioProcessor::timerCallback()
{
    const int note = samplerEngine.consumeLearnedNote();
    if (note >= 0 && armedLearnPad >= 0 && armedLearnPad < minigun::kNumPads)
    {
        kit.pads[(size_t) armedLearnPad].note = juce::jlimit (0, 127, note);
        armedLearnPad = -1;
        kitEdited();
    }
}

bool MinigunAudioProcessor::saveKitToFolder (const juce::File& folder)
{
    // A kit that was never named takes the name of the folder it is saved into.
    if (kit.name.isEmpty() || kit.name == "Untitled Kit")
        kit.name = folder.getFileName();

    if (! minigun::KitStore::saveKit (kit, folder))
        return false;
    kitEdited();
    return true;
}

bool MinigunAudioProcessor::loadKitFromFolder (const juce::File& folder)
{
    minigun::Kit loaded;
    if (! minigun::KitStore::loadKit (folder, loaded))
        return false;
    kit = std::move (loaded);
    selectedPad = 0;
    kitEdited();
    return true;
}

//==============================================================================
void MinigunAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("Minigun");
    root.setAttribute (kMasterParamId, (double) apvts.getRawParameterValue (kMasterParamId)->load());

    auto* kitXml = root.createNewChildElement ("kit");
    kitXml->setAttribute ("folder", kit.folder.getFullPathName());
    kitXml->addTextElement (kit.toJsonString (juce::File())); // invalid File -> always absolute paths

    copyXmlToBinary (root, destData);
}

void MinigunAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> root (getXmlFromBinary (data, sizeInBytes));
    if (root == nullptr || ! root->hasTagName ("Minigun"))
        return;

    const float masterDb = (float) root->getDoubleAttribute (kMasterParamId, 0.0);
    if (auto* param = apvts.getParameter (kMasterParamId))
        param->setValueNotifyingHost (param->convertTo0to1 (masterDb));

    if (auto* kitXml = root->getChildByName ("kit"))
    {
        auto folder = juce::File (kitXml->getStringAttribute ("folder"));
        auto json = kitXml->getAllSubText();

        minigun::Kit loaded;
        if (loaded.fromJsonString (json, folder))
        {
            kit = std::move (loaded);
            selectedPad = 0;
            kitEdited();
        }
    }
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MinigunAudioProcessor();
}
