#include "SampleLoader.h"

namespace minigun
{

SampleLoader::SampleLoader()
{
    formatManager.registerBasicFormats();

   #if JUCE_USE_WINDOWS_MEDIA_FORMAT && JUCE_WINDOWS
    formatManager.registerFormat (new juce::WindowsMediaAudioFormat(), false);
   #endif
}

std::shared_ptr<const LoadedSample> SampleLoader::load (const juce::File& file)
{
    auto key = file.getFullPathName();

    auto existing = cache.find (key);
    if (existing != cache.end())
        return existing->second;

    if (! file.existsAsFile())
        return nullptr;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    auto sample = std::make_shared<LoadedSample>();
    sample->sampleRate = reader->sampleRate;
    sample->file = file;

    int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
    auto numSamples = (int) juce::jmin ((juce::int64) std::numeric_limits<int>::max(), reader->lengthInSamples);

    sample->buffer.setSize (numChannels, numSamples);
    reader->read (&sample->buffer, 0, numSamples, 0, true, numChannels > 1);

    cache[key] = sample;
    return sample;
}

void SampleLoader::clearCache()
{
    cache.clear();
}

} // namespace minigun
