#pragma once
#include "LoadedSample.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>

namespace minigun
{

/** Loads audio files into memory and caches them by absolute path.
    Message-thread only (uses juce::File / juce::AudioFormatManager). */
class SampleLoader
{
public:
    SampleLoader();

    /** Loads (or returns a cached) sample. Returns nullptr if the file cannot be read
        (cache is left untouched in that case). */
    std::shared_ptr<const LoadedSample> load (const juce::File& file);

    /** Drops all cached samples. */
    void clearCache();

    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

private:
    juce::AudioFormatManager formatManager;
    std::map<juce::String, std::shared_ptr<const LoadedSample>> cache;
};

} // namespace minigun
