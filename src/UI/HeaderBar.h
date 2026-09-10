// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <functional>

class MinigunAudioProcessor;

namespace minigun
{

/** Top header: wordmark, kit LCD, Save/Load kit, MIDI activity LED, output peak meter
    and the master gain knob (bound to apvts "master"). */
class HeaderBar : public juce::Component
{
public:
    explicit HeaderBar (MinigunAudioProcessor& processor);
    ~HeaderBar() override;

    /** Re-reads the kit name/pad-count/sample-count from the processor. */
    void refreshKitInfo();

    void setMidiLedOn (bool on);
    void setMeterLevels (float peakL, float peakR);

    /** Called after a successful Save/Load so the editor can refresh every panel
        (in case the processor's own change broadcast doesn't cover it). */
    std::function<void()> onKitChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MinigunAudioProcessor& processor;

    juce::TextButton saveButton { "Save kit" };
    juce::TextButton loadButton { "Load kit" };
    juce::Slider masterKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> logoBounds, wordmarkBounds, kitLcdBounds;
    juce::String kitName { "Untitled Kit" };
    int padCount = 0, sampleCount = 0;

    bool midiLit = false;
    float peakL = 0.0f, peakR = 0.0f;
    juce::Rectangle<int> meterBounds;

    void saveKit();
    void loadKit();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace minigun
