// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "Voice.h"
#include <cmath>

namespace minigun
{

namespace
{
    constexpr float kHalfPi = juce::MathConstants<float>::halfPi;
}

void Voice::start (std::shared_ptr<const LoadedSample> sampleToPlay,
                    int velocity,
                    bool velocityToVolume,
                    float volumeDb,
                    float pan,
                    float pitchSemitones,
                    float attackMsIn,
                    float decayMsIn,
                    float releaseMsIn,
                    double hostSampleRateToUse,
                    int padIndexToUse,
                    int chokeGroupToUse,
                    int busIndexToUse,
                    OutputMode outputModeToUse,
                    bool monoSumToUse,
                    juce::int64 voiceStartOrder)
{
    sample = std::move (sampleToPlay);
    hostSampleRate = hostSampleRateToUse > 0.0 ? hostSampleRateToUse : 44100.0;

    ratio = (sample != nullptr && sample->sampleRate > 0.0)
                ? (sample->sampleRate / hostSampleRate) * std::pow (2.0, (double) pitchSemitones / 12.0)
                : 1.0;

    readPos = 0.0;

    baseGain = juce::Decibels::decibelsToGain (volumeDb);
    // Velocity tracking off: the hit still picks its velocity layer, it just plays at full level.
    velocityGain = velocityToVolume ? juce::jlimit (0.0f, 1.0f, (float) velocity / 127.0f) : 1.0f;

    float t = juce::jlimit (0.0f, 1.0f, (pan + 1.0f) * 0.5f);
    panLeftGain = std::cos (t * kHalfPi);
    panRightGain = std::sin (t * kHalfPi);

    attackMs = juce::jmax (0.0f, attackMsIn);
    decayMs = decayMsIn;
    releaseMs = juce::jmax (1.0f, releaseMsIn);

    padIndex = padIndexToUse;
    chokeGroup = chokeGroupToUse;
    targetBus = busIndexToUse;
    outputMode = outputModeToUse;
    monoSum = monoSumToUse;
    startOrder = voiceStartOrder;

    stageSamplesElapsed = 0.0;
    releaseStartLevel = 0.0f;

    if (sample == nullptr || sample->buffer.getNumSamples() <= 0)
    {
        stage = Stage::Idle;
        return;
    }

    if (attackMs <= 0.0f)
    {
        stage = Stage::Hold;
        envelopeLevel = 1.0f;
    }
    else
    {
        stage = Stage::Attack;
        envelopeLevel = 0.0f;
    }
}

void Voice::triggerRelease()
{
    if (stage == Stage::Idle || stage == Stage::Release)
        return;

    stage = Stage::Release;
    releaseStartLevel = envelopeLevel;
    stageSamplesElapsed = 0.0;
}

float Voice::computeSample (int channel, double position) const noexcept
{
    const auto& buf = sample->buffer;
    const int numSamples = buf.getNumSamples();
    const int numChannels = buf.getNumChannels();
    const int srcChannel = channel < numChannels ? channel : 0;

    const int i0 = (int) position;
    if (i0 >= numSamples)
        return 0.0f;

    const int i1 = juce::jmin (i0 + 1, numSamples - 1);
    const float frac = (float) (position - (double) i0);

    const float s0 = buf.getSample (srcChannel, i0);
    const float s1 = buf.getSample (srcChannel, i1);
    return s0 + frac * (s1 - s0);
}

void Voice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) noexcept
{
    if (stage == Stage::Idle || sample == nullptr)
        return;

    const int numOutChannels = juce::jmin (2, outputBuffer.getNumChannels());
    const int sampleLength = sample->buffer.getNumSamples();
    const int numSrcChannels = sample->buffer.getNumChannels();

    const double attackSamples = (double) attackMs * 0.001 * hostSampleRate;
    const double decaySamples = decayMs >= 0.0f ? (double) decayMs * 0.001 * hostSampleRate : -1.0;
    const double releaseSamples = juce::jmax (1.0, (double) releaseMs * 0.001 * hostSampleRate);

    auto* left = numOutChannels > 0 ? outputBuffer.getWritePointer (0) : nullptr;
    auto* right = numOutChannels > 1 ? outputBuffer.getWritePointer (1) : nullptr;

    // Mono routing: write only the target channel of the pad's bus buffer, no pan applied.
    const bool isMono = outputMode != OutputMode::Stereo;
    const int monoSrcChannel = outputMode == OutputMode::MonoLeft ? 0 : 1; // side matching the output when not summing
    float* monoTarget = nullptr;
    if (isMono)
        monoTarget = (outputMode == OutputMode::MonoLeft) ? left : (right != nullptr ? right : left);

    for (int i = 0; i < numSamples; ++i)
    {
        if (stage == Stage::Idle)
            break;

        if ((int) readPos >= sampleLength)
        {
            stage = Stage::Idle;
            break;
        }

        // Envelope state machine.
        switch (stage)
        {
            case Stage::Attack:
                envelopeLevel = attackSamples > 0.0
                                     ? (float) juce::jlimit (0.0, 1.0, stageSamplesElapsed / attackSamples)
                                     : 1.0f;
                if (stageSamplesElapsed >= attackSamples)
                {
                    stage = Stage::Hold;
                    stageSamplesElapsed = 0.0;
                    envelopeLevel = 1.0f;
                }
                break;

            case Stage::Hold:
                envelopeLevel = 1.0f;
                if (decaySamples >= 0.0 && stageSamplesElapsed >= decaySamples)
                {
                    stage = Stage::Release;
                    releaseStartLevel = 1.0f;
                    stageSamplesElapsed = 0.0;
                }
                break;

            case Stage::Release:
            {
                double progress = juce::jlimit (0.0, 1.0, stageSamplesElapsed / releaseSamples);
                envelopeLevel = releaseStartLevel * (float) (1.0 - progress);
                if (progress >= 1.0)
                {
                    stage = Stage::Idle;
                    envelopeLevel = 0.0f;
                }
                break;
            }

            case Stage::Idle:
                break;
        }

        if (stage == Stage::Idle && envelopeLevel <= 0.0f)
            break;

        const float gain = envelopeLevel * baseGain * velocityGain;

        if (isMono)
        {
            float monoValue;
            if (numSrcChannels <= 1)
                monoValue = computeSample (0, readPos); // mono sample used as-is
            else if (monoSum)
                monoValue = 0.5f * (computeSample (0, readPos) + computeSample (1, readPos));
            else
                monoValue = computeSample (monoSrcChannel, readPos);

            if (monoTarget != nullptr)
                monoTarget[startSample + i] += monoValue * gain;
        }
        else
        {
            const float l = computeSample (0, readPos) * gain * panLeftGain;
            const float r = computeSample (1, readPos) * gain * panRightGain;

            if (left != nullptr)
                left[startSample + i] += l;
            if (right != nullptr)
                right[startSample + i] += r;
        }

        readPos += ratio;
        stageSamplesElapsed += 1.0;
    }
}

} // namespace minigun
