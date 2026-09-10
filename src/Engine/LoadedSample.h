#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace minigun
{

/** Decoded audio sample, fully resident in memory. Immutable once constructed —
    safe to share across threads via std::shared_ptr<const LoadedSample>. */
struct LoadedSample
{
    juce::AudioBuffer<float> buffer; // 1 or 2 channels
    double sampleRate = 44100.0;
    juce::File file;
};

} // namespace minigun
