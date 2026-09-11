// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "KnobControl.h"
#include <memory>

class MinigunAudioProcessor;

namespace minigun
{

/** Small "advanced" strip under the pad grid: per-hit humanize (random pitch / level) for the
    selected pad, plus the kit-wide TRIG switch that forces sensible humanize on every pad. */
class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (MinigunAudioProcessor& processor);

    static constexpr int kPreferredHeight = 70;

    /** Pulls trigger mode and the selected pad's humanize amounts from the processor. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override; // click on the background drops text-field focus

private:
    MinigunAudioProcessor& processor;

    juce::TextButton trigButton { "TRIG" };
    std::unique_ptr<KnobControl> pitchKnob; // +/- cents
    std::unique_ptr<KnobControl> volKnob;   // +/- dB

    juce::Rectangle<int> titleBounds, infoBounds;
    int currentPadIndex = 0;

    void setTriggerModeFromUI (bool on);
    void updateEnablement();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedPanel)
};

} // namespace minigun
