#pragma once
#include "EngineKit.h"
#include "Voice.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace minigun
{

/** Real-time audio engine: 32 voices, note->pad dispatch, RR/Random layer sample
    selection, choke groups, and a dedicated preview voice.

    Thread-safety: setKitSnapshot()/requestPreview()/requestStopPreview()/triggerPad()/
    armMidiLearn() may be called from the message thread. processBlock() (and everything
    it touches) must only be called from the audio thread. Cross-thread hand-off uses a
    juce::SpinLock-guarded pointer swap (message thread blocks briefly; audio thread uses
    ScopedTryLock and keeps the previous snapshot on contention) plus lock-free atomics. */
class SamplerEngine
{
public:
    static constexpr int kNumVoices = 32;
    static constexpr int kNumOutputBuses = 17; // bus 0 = Main, 1..16 = aux stereo

    SamplerEngine();

    //==============================================================================
    // Message-thread API.

    void prepare (double sampleRate, int maximumBlockSize);

    /** Publishes a new immutable kit snapshot for the audio thread to pick up. */
    void setKitSnapshot (std::shared_ptr<const EngineKit> newSnapshot);

    /** Publishes a new preview sample; the audio thread starts a dedicated preview voice
        for it (0 dB, no pad involved). */
    void requestPreview (std::shared_ptr<const LoadedSample> sampleToPreview);

    /** Requests the preview voice to release (short ramp) on the next block. */
    void requestStopPreview();

    /** Enqueues a UI-triggered pad hit (lock-free), consumed at the start of the next
        processBlock(). velocity is 1..127. */
    void triggerPad (int padIndex, int velocity);

    /** Arms MIDI learn for a pad; the next MIDI note-on captured by processBlock() is
        reported via consumeLearnedNote(). Pass -1 to disarm. */
    void armMidiLearn (int padIndex) noexcept;
    bool isLearnArmed() const noexcept;

    /** Returns (and clears) the most recently learned note, or -1 if none is pending. */
    int consumeLearnedNote() noexcept;

    /** Returns (and clears) the velocity of the last hit on padIndex since the previous
        call, or 0 if it has not been hit since. */
    int getAndClearPadHit (int padIndex) noexcept;

    int getActiveVoiceCount() const noexcept { return activeVoiceCount.load (std::memory_order_relaxed); }
    /** Number of output buses that were enabled and stereo during the last block (diagnostic for the footer). */
    int getUsableBusCount() const noexcept { return usableBusCount.load (std::memory_order_relaxed); }

    //==============================================================================
    // Audio-thread API.

    /** Renders into buffer (adds; caller should clear it first), consuming incoming MIDI
        note-ons and any pending UI triggers / preview requests. `proc` is used only to read
        the current bus layout (getBus/getBusBuffer) — no allocation, locking, or state
        mutation happens on it. Each voice renders into its pad's output bus if that bus is
        currently enabled and stereo, else falls back to Main; the preview voice always
        renders to Main. */
    void processBlock (juce::AudioProcessor& proc, juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midiMessages) noexcept;

private:
    struct PadTrigger { int padIndex; int velocity; };

    double currentSampleRate = 44100.0;

    juce::SpinLock kitLock;
    std::shared_ptr<const EngineKit> pendingKit;
    std::shared_ptr<const EngineKit> activeKit; // audio-thread-owned cache

    juce::SpinLock previewLock;
    std::shared_ptr<const LoadedSample> pendingPreviewSample;
    std::atomic<bool> previewRequestPending { false };
    std::atomic<bool> stopPreviewRequested { false };
    Voice previewVoice;

    std::array<Voice, kNumVoices> voices;
    juce::int64 voiceStartCounter = 0;
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<int> usableBusCount { 0 };

    std::array<std::atomic<int>, kNumPads> rrCounters;
    std::array<std::atomic<int>, kNumPads> padHits; // packed last hit: velocity | (layerIndex + 1) << 8; 0 = none pending
    juce::Random random;

    std::atomic<int> learnArmedPad { -1 };
    std::atomic<int> learnedNote { -1 };

    static constexpr int kTriggerFifoSize = 64;
    juce::AbstractFifo triggerFifo { kTriggerFifoSize };
    std::array<PadTrigger, kTriggerFifoSize> triggerBuffer;

    void handleNoteOn (int note, int velocity) noexcept;
    void triggerPadInternal (int padIndex, int velocity) noexcept;
    Voice* stealVoice() noexcept;
};

} // namespace minigun
