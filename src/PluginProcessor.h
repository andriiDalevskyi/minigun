// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include "Model/KitModel.h"
#include "Engine/SampleLoader.h"
#include "Engine/SamplerEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

/** Owner of the Kit model and the real-time SamplerEngine; implements the
    Processor <-> UI contract documented in ARCHITECTURE.md. */
class MinigunAudioProcessor : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::Timer
{
public:
    MinigunAudioProcessor();
    ~MinigunAudioProcessor() override;

    //==============================================================================
    // juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Processor <-> UI contract (see ARCHITECTURE.md).

    minigun::Kit& getKit() noexcept { return kit; }

    /** Call after ANY edit to the Kit returned by getKit(). Synchronously (re)loads any
        new/changed sample files, builds+publishes a new EngineKit snapshot to the audio
        thread, and calls sendChangeMessage() so all UI panels refresh. */
    void kitEdited();

    //==============================================================================
    // Undo / redo. Every kitEdited() call records a history step (a serialized Kit snapshot).
    // Edits closer together than ~0.7 s, or inside a begin/endUndoGesture() pair (knob drags),
    // are merged into the previous step so a knob sweep or typing a name is one undo.
    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void beginUndoGesture() noexcept { ++undoGestureDepth; }
    void endUndoGesture() noexcept   { undoGestureDepth = juce::jmax (0, undoGestureDepth - 1); }

    int getSelectedPad() const noexcept { return selectedPad; }
    void setSelectedPad (int index) noexcept { selectedPad = juce::jlimit (0, minigun::kNumPads - 1, index); }

    /** UI-triggered pad hit (e.g. mouse click). velocity is 1..127. */
    void triggerPad (int padIndex, int velocity);

    /** Browser preview: loads and plays file through a dedicated preview voice at 0 dB,
        bypassing pads, through the plugin's normal output. */
    void previewFile (const juce::File& file);
    void stopPreview();

    /** Returns (and clears) the velocity of the last hit on padIndex since the previous
        call, or 0 if it has not been hit since. Poll at ~30 Hz from the UI. */
    /** Velocity of the last hit since the previous call (0 = none). If layerIndexOut is given it
        receives the velocity layer that played (-1 when unknown). */
    int getAndClearPadHit (int padIndex, int* layerIndexOut = nullptr);

    /** Arms MIDI learn for padIndex; the next incoming MIDI note-on assigns its note
        number to that pad. */
    void armMidiLearn (int padIndex);
    bool isLearnArmed() const;

    /** Wraps KitStore::saveKit()/loadKit() and calls kitEdited() on success. */
    bool saveKitToFolder (const juce::File& folder);
    bool loadKitFromFolder (const juce::File& folder);

    minigun::SampleLoader& getSampleLoader() noexcept { return sampleLoader; }
    juce::AudioFormatManager& getFormatManager() noexcept { return sampleLoader.getFormatManager(); }

    int getActiveVoiceCount() const noexcept { return samplerEngine.getActiveVoiceCount(); }
    int getUsableBusCount() const noexcept { return samplerEngine.getUsableBusCount(); }

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> outputPeakL { 0.0f };
    std::atomic<float> outputPeakR { 0.0f };

    /** Set true on the audio thread whenever any MIDI note-on arrives in a block; the UI
        clears it after lighting the MIDI LED. */
    std::atomic<bool> midiActivity { false };

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    // Main stereo (enabled) + 16 aux stereo buses "Out 1".."Out 16" (disabled by default).
    // Declared as a static member (rather than a free function) because BusesProperties is
    // a protected nested type of juce::AudioProcessor, only nameable from a derived class.
    static BusesProperties makeBusesProperties();

    minigun::Kit kit;
    int selectedPad = 0;

    // Undo history: serialized kit snapshots (JSON with absolute paths + kit folder).
    std::vector<juce::String> undoStack, redoStack;
    juce::String committedState;          // snapshot of the kit as last recorded in history
    juce::uint32 lastCommitMs = 0;
    int undoGestureDepth = 0;
    static constexpr size_t kMaxUndoSteps = 200;
    static constexpr juce::uint32 kUndoMergeWindowMs = 700;

    juce::String snapshotKit() const;
    void applySnapshot (const juce::String& snapshot);
    void resetUndoHistory();
    void publishKit();                    // steps (a)-(c) of kitEdited() without touching history

    minigun::SampleLoader sampleLoader;
    minigun::SamplerEngine samplerEngine;

    int armedLearnPad = -1; // message-thread record of which pad armMidiLearn() targeted

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinigunAudioProcessor)
};
