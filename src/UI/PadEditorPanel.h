// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Model/KitModel.h"
#include "ClickFocusTextEditor.h"
#include <array>
#include <functional>

class MinigunAudioProcessor;

namespace minigun
{

/** Per-pad editor: name, note, RR/RND, choke, output routing, MIDI learn and the 6
    envelope/mix knobs. Every edit writes straight into processor.getKit().pads[selected]
    and then calls processor.kitEdited(). */
class PadEditorPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit PadEditorPanel (MinigunAudioProcessor& processor);
    ~PadEditorPanel() override;

    static constexpr int kPreferredHeight = 234;

    /** Pulls the current selected pad from the processor and updates every widget. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override; // click on the panel background drops focus from the text fields
    void resized() override;

private:
    MinigunAudioProcessor& processor;

    ClickFocusTextEditor nameEditor;
    juce::TextButton rrButton   { "RR" };
    juce::TextButton rndButton  { "RND" };
    juce::TextButton learnButton { "MIDI" };

    juce::ComboBox noteCombo;
    ClickFocusTextEditor noteNumberEditor;
    juce::ComboBox chokeCombo;

    juce::ComboBox outCombo;
    juce::TextButton modeStButton { "ST" };
    juce::TextButton modeLButton  { "L" };
    juce::TextButton modeRButton  { "R" };
    juce::TextButton sumButton    { "SUM" };

    struct Knob
    {
        juce::Slider slider;
        juce::Label nameLabel;
        juce::Label valueLabel;
    };
    std::array<std::unique_ptr<Knob>, 6> knobs; // Vol, Pan, Pitch, Atk, Dec, Rel

    juce::Rectangle<int> badgeBounds;

    bool lastLearnArmed = false;
    int currentPadIndex = 0;

    void timerCallback() override;
    void updateKnobLabel (int index);
    void writeAndNotify (std::function<void()> mutator);

    /** Rebuilds the OUT combo's item text for the given output mode (item ids stay 1..17 = bus index + 1). */
    void rebuildOutCombo (OutputMode mode);
    /** Writes note to the model and keeps the combo/number editor in sync. */
    void setNoteFromUI (int note);
    /** Writes outputMode to the model, refreshes OUT combo labels and SUM enablement. */
    void setOutputModeFromUI (OutputMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadEditorPanel)
};

} // namespace minigun
