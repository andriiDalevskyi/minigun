// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "UI/MinigunLookAndFeel.h"
#include "UI/HeaderBar.h"
#include "UI/PadGrid.h"
#include "UI/PadEditorPanel.h"
#include "UI/LayerEditor.h"
#include "UI/BrowserPanel.h"

class MinigunAudioProcessor;

/** Top-level editor: fixed 1200x720 layout per ARCHITECTURE.md. Polls the processor at
    30 Hz for pad-hit glow / meters / MIDI LED / voice count, and refreshes every panel
    whenever the processor broadcasts a change (kit edited, loaded, or selection moved). */
class MinigunAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer,
                                    private juce::ChangeListener,
                                    public juce::DragAndDropContainer
{
public:
    explicit MinigunAudioProcessorEditor (MinigunAudioProcessor&);
    ~MinigunAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override; // Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z

private:
    MinigunAudioProcessor& audioProcessor;

    minigun::MinigunLookAndFeel lookAndFeel;
    minigun::HeaderBar header;
    minigun::PadGrid padGrid;
    minigun::PadEditorPanel padEditor;
    minigun::LayerEditor layerEditor;
    minigun::BrowserPanel browser;

    std::array<float, 16> padGlow {};
    std::array<int, 16> padGlowHold {};   // timer ticks to hold full glow before it decays
    juce::int64 lastMidiActivityMs = -100000;
    int lastVoiceCount = 0;

    juce::Rectangle<int> footerBounds;

    void refreshAll();
    /** Triggers a pad from the UI and lights it immediately in the colour of the velocity layer
        that will play (the audio thread's own hit report refreshes it a few ms later). */
    void auditionPad (int padIndex, int velocity);
    void handlePadDrop (int padIndex, juce::Array<juce::File> files);

    /** Adds files to a layer of the selected pad. layerIndex == -1 means "use
        layerEditor.getSelectedLayer()" (the default, used by BrowserPanel's "Add to Lx");
        LayerEditor's drag & drop passes an explicit resolved layer index instead. */
    void handleAddToLayer (juce::Array<juce::File> files, int layerIndex = -1);

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MinigunAudioProcessorEditor)
};
