// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "SamplerEngine.h"

namespace minigun
{

SamplerEngine::SamplerEngine()
{
    for (auto& c : rrCounters) c.store (0);
    for (auto& h : padHits) h.store (0);
}

void SamplerEngine::prepare (double sampleRate, int /*maximumBlockSize*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void SamplerEngine::setKitSnapshot (std::shared_ptr<const EngineKit> newSnapshot)
{
    const juce::SpinLock::ScopedLockType lock (kitLock);
    pendingKit = std::move (newSnapshot);
}

void SamplerEngine::requestPreview (std::shared_ptr<const LoadedSample> sampleToPreview)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        pendingPreviewSample = std::move (sampleToPreview);
    }
    previewRequestPending.store (true, std::memory_order_release);
}

void SamplerEngine::requestStopPreview()
{
    stopPreviewRequested.store (true, std::memory_order_release);
}

void SamplerEngine::triggerPad (int padIndex, int velocity)
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;

    int start1, size1, start2, size2;
    triggerFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
        triggerBuffer[(size_t) start1] = { padIndex, juce::jlimit (1, 127, velocity) };
    triggerFifo.finishedWrite (size1 + size2);
}

void SamplerEngine::armMidiLearn (int padIndex) noexcept
{
    learnArmedPad.store (padIndex, std::memory_order_release);
}

bool SamplerEngine::isLearnArmed() const noexcept
{
    return learnArmedPad.load (std::memory_order_acquire) != -1;
}

int SamplerEngine::consumeLearnedNote() noexcept
{
    return learnedNote.exchange (-1, std::memory_order_acq_rel);
}

int SamplerEngine::getAndClearPadHit (int padIndex) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return 0;
    return padHits[(size_t) padIndex].exchange (0, std::memory_order_acq_rel);
}

Voice* SamplerEngine::stealVoice() noexcept
{
    for (auto& v : voices)
        if (! v.isActive())
            return &v;

    Voice* oldest = &voices[0];
    for (auto& v : voices)
        if (v.getStartOrder() < oldest->getStartOrder())
            oldest = &v;
    return oldest;
}

void SamplerEngine::triggerPadInternal (int padIndex, int velocity) noexcept
{
    if (activeKit == nullptr || padIndex < 0 || padIndex >= kNumPads)
        return;

    auto& pad = activeKit->pads[(size_t) padIndex];
    if (pad.layers.empty())
        return;

    // Pick layer containing velocity, else nearest.
    int layerIdx = -1;
    for (int i = 0; i < (int) pad.layers.size(); ++i)
    {
        auto& l = pad.layers[(size_t) i];
        if (velocity >= l.lo && velocity <= l.hi) { layerIdx = i; break; }
    }
    if (layerIdx < 0)
    {
        int bestDist = 1000;
        for (int i = 0; i < (int) pad.layers.size(); ++i)
        {
            auto& l = pad.layers[(size_t) i];
            int d = velocity < l.lo ? l.lo - velocity : velocity - l.hi;
            if (d < bestDist) { bestDist = d; layerIdx = i; }
        }
    }
    if (layerIdx < 0)
        return;

    auto& layer = pad.layers[(size_t) layerIdx];
    if (layer.samples.empty())
        return;

    int sampleIdx;
    if (pad.mode == PlayMode::RoundRobin)
    {
        int n = (int) layer.samples.size();
        sampleIdx = rrCounters[(size_t) padIndex].fetch_add (1, std::memory_order_relaxed) % n;
        if (sampleIdx < 0) sampleIdx += n;
    }
    else
    {
        sampleIdx = random.nextInt ((int) layer.samples.size());
    }

    auto sampleToPlay = layer.samples[(size_t) sampleIdx];
    if (sampleToPlay == nullptr)
        return;

    if (pad.chokeGroup != 0)
        for (auto& v : voices)
            if (v.isActive() && v.getChokeGroup() == pad.chokeGroup)
                v.triggerRelease();

    Voice* voice = stealVoice();
    voice->start (sampleToPlay, velocity, pad.volumeDb, pad.pan, pad.pitchSemitones,
                  pad.attackMs, pad.decayMs, pad.releaseMs, currentSampleRate,
                  padIndex, pad.chokeGroup, pad.output, pad.outputMode, pad.monoSum, ++voiceStartCounter);

    // Packed: low byte = velocity, next byte = layer index + 1 (so the UI can colour the pad like the layer).
    padHits[(size_t) padIndex].store (velocity | ((layerIdx + 1) << 8), std::memory_order_release);
}

void SamplerEngine::handleNoteOn (int note, int velocity) noexcept
{
    int armedPad = learnArmedPad.load (std::memory_order_acquire);
    if (armedPad != -1)
    {
        learnedNote.store (note, std::memory_order_release);
        learnArmedPad.store (-1, std::memory_order_release);
    }

    if (activeKit == nullptr)
        return;

    for (int i = 0; i < kNumPads; ++i)
        if (activeKit->pads[(size_t) i].note == note)
            triggerPadInternal (i, velocity);
}

void SamplerEngine::processBlock (juce::AudioProcessor& proc, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midiMessages) noexcept
{
    // Pick up a new kit snapshot if one is available (never blocks).
    {
        const juce::SpinLock::ScopedTryLockType tryLock (kitLock);
        if (tryLock.isLocked())
            activeKit = pendingKit;
    }

    // Preview sample hand-off.
    if (previewRequestPending.exchange (false, std::memory_order_acq_rel))
    {
        const juce::SpinLock::ScopedTryLockType tryLock (previewLock);
        if (tryLock.isLocked())
        {
            auto s = pendingPreviewSample;
            previewVoice.start (s, 127, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 30.0f,
                                 currentSampleRate, -1, 0, 0, OutputMode::Stereo, true, ++voiceStartCounter);
        }
        else
        {
            previewRequestPending.store (true, std::memory_order_release); // retry next block
        }
    }

    if (stopPreviewRequested.exchange (false, std::memory_order_acq_rel))
        previewVoice.triggerRelease();

    // Drain UI-triggered pad hits.
    int start1, size1, start2, size2;
    triggerFifo.prepareToRead (triggerFifo.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i)
        triggerPadInternal (triggerBuffer[(size_t) (start1 + i)].padIndex, triggerBuffer[(size_t) (start1 + i)].velocity);
    for (int i = 0; i < size2; ++i)
        triggerPadInternal (triggerBuffer[(size_t) (start2 + i)].padIndex, triggerBuffer[(size_t) (start2 + i)].velocity);
    triggerFifo.finishedRead (size1 + size2);

    // Incoming MIDI note-ons (note-off ignored: one-shot playback).
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            handleNoteOn (msg.getNoteNumber(), msg.getVelocity());
    }

    const int numSamples = buffer.getNumSamples();

    // Resolve each output bus's sub-buffer + whether it is currently usable (enabled and
    // stereo). getBusBuffer() wraps existing channel pointers (preallocated storage for
    // up to 32 channels) — it does not allocate. Bus 0 (Main) is always enabled.
    std::array<juce::AudioBuffer<float>, kNumOutputBuses> busBuffers;
    std::array<bool, kNumOutputBuses> busUsable {};
    const int numBuses = juce::jmin (kNumOutputBuses, proc.getBusCount (false));
    for (int i = 0; i < numBuses; ++i)
    {
        busBuffers[(size_t) i] = proc.getBusBuffer (buffer, false, i);
        auto* bus = proc.getBus (false, i);
        busUsable[(size_t) i] = bus != nullptr && bus->isEnabled()
                                 && busBuffers[(size_t) i].getNumChannels() == 2;
    }
    {
        int usable = 0;
        for (int i = 0; i < numBuses; ++i) if (busUsable[(size_t) i]) ++usable;
        usableBusCount.store (usable, std::memory_order_relaxed);
    }

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        int bus = v.getTargetBus();
        if (bus < 0 || bus >= numBuses || ! busUsable[(size_t) bus])
            bus = 0; // fall back to Main
        v.renderNextBlock (busBuffers[(size_t) bus], 0, numSamples);
    }

    if (previewVoice.isActive() && numBuses > 0)
        previewVoice.renderNextBlock (busBuffers[0], 0, numSamples);

    int active = 0;
    for (auto& v : voices)
        if (v.isActive())
            ++active;
    activeVoiceCount.store (active, std::memory_order_relaxed);
}

} // namespace minigun
