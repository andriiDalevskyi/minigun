// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include "LoadedSample.h"
#include "../Model/KitModel.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace minigun
{

/** One playing sample instance: resampling, pitch, ADR envelope, gain/pan.
    Real-time safe: start()/renderNextBlock()/release() never allocate, lock, or touch juce::File. */
class Voice
{
public:
    Voice() = default;

    /** Begins playback. padIndex/chokeGroup are used by SamplerEngine for choke handling
        (padIndex == -1 identifies a preview voice, which choke never touches). */
    void start (std::shared_ptr<const LoadedSample> sampleToPlay,
                int velocity,
                bool velocityToVolume,
                float volumeDb,
                float pan,
                float pitchSemitones,
                float attackMs,
                float decayMs,
                float releaseMs,
                double hostSampleRateToUse,
                int padIndexToUse,
                int chokeGroupToUse,
                int busIndexToUse,
                OutputMode outputModeToUse,
                bool monoSumToUse,
                juce::int64 voiceStartOrder);

    /** Immediately begins the release ramp from the current envelope level (used for choke
        and for stopping a preview). No-op if already releasing or idle. */
    void triggerRelease();

    bool isActive() const noexcept { return stage != Stage::Idle; }
    int getPadIndex() const noexcept { return padIndex; }
    int getChokeGroup() const noexcept { return chokeGroup; }
    /** Output bus this voice was started for (0 = Main, 1..16 = aux). The engine falls back
        to Main at render time if this bus is not currently enabled in the host layout. */
    int getTargetBus() const noexcept { return targetBus; }
    juce::int64 getStartOrder() const noexcept { return startOrder; }

    /** Adds this voice's output into outputBuffer[0..1] for [startSample, startSample+numSamples). */
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) noexcept;

private:
    enum class Stage { Idle, Attack, Hold, Release };

    std::shared_ptr<const LoadedSample> sample;
    double readPos = 0.0;
    double ratio = 1.0;
    double hostSampleRate = 44100.0;

    float velocityGain = 1.0f;
    float baseGain = 1.0f;
    float panLeftGain = 0.7071f;
    float panRightGain = 0.7071f;

    float attackMs = 0.0f;
    float decayMs = -1.0f;
    float releaseMs = 120.0f;

    Stage stage = Stage::Idle;
    double stageSamplesElapsed = 0.0;
    float envelopeLevel = 0.0f;
    float releaseStartLevel = 0.0f;

    int padIndex = -1;
    int chokeGroup = 0;
    int targetBus = 0;
    OutputMode outputMode = OutputMode::Stereo;
    bool monoSum = true;
    juce::int64 startOrder = 0;

    float computeSample (int channel, double position) const noexcept;
};

} // namespace minigun
