// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "AdvancedPanel.h"
#include "MinigunLookAndFeel.h"
#include "../Model/KitModel.h"
#include "../PluginProcessor.h"

namespace minigun
{

namespace
{
    juce::String formatCents (double c)
    {
        if (c < 0.5) return "OFF";
        return juce::String::fromUTF8 ("\xC2\xB1") + juce::String ((int) std::round (c)) + " c";
    }

    juce::String formatDbSpread (double db)
    {
        if (db < 0.05) return "OFF";
        return juce::String::fromUTF8 ("\xC2\xB1") + juce::String (db, 1) + " dB";
    }

    double parseAmount (const juce::String& t)
    {
        auto s = t.trim().toUpperCase();
        if (s.startsWith ("OFF")) return 0.0;
        return std::abs (s.retainCharacters ("0123456789.-").getDoubleValue());
    }

    // Plain-English help, drawn on the right of the strip (keep every line <= ~40 chars).
    const char* const kInfoLines[] =
    {
        "Repeated identical hits comb into a",
        "machine-gun: blasts, double kick, rolls.",
        "RND adds a hair of detune / level per",
        "hit. Metal: 10 c + 1.0 dB. Timing kept.",
        "TRIG: forces that on all 16 pads."
    };
}

AdvancedPanel::AdvancedPanel (MinigunAudioProcessor& processorIn) : processor (processorIn)
{
    setPaintingIsUnclipped (true);

    trigButton.setClickingTogglesState (true);
    trigButton.setTooltip ("Trigger mode: the kit is played from a programmed or drum-replaced MIDI track. "
                           "Every pad humanizes by " + juce::String ((int) kTriggerModeRndPitchCents) + " cents / "
                           + juce::String (kTriggerModeRndVolDb, 1) + " dB and the knobs are locked.");
    trigButton.onClick = [this] { setTriggerModeFromUI (trigButton.getToggleState()); };
    addAndMakeVisible (trigButton);

    pitchKnob = std::make_unique<KnobControl> ("RND PITCH", KnobControl::Layout::horizontal);
    pitchKnob->setRange (0.0, 50.0, 1.0);
    pitchKnob->setDefaultValue (0.0);
    pitchKnob->format = [] (double v) { return formatCents (v); };
    pitchKnob->parse = [] (const juce::String& t) { return parseAmount (t); };
    pitchKnob->setTooltipText ("Random detune per hit, +/- cents. 8-12 c kills the machine-gun; above ~25 c it reads as out of tune.");
    pitchKnob->setThumbColour (MinigunLookAndFeel::teal);
    pitchKnob->onGestureStart = [this] { processor.beginUndoGesture(); };
    pitchKnob->onGestureEnd   = [this] { processor.endUndoGesture(); };
    pitchKnob->onValueChange = [this] (double v)
    {
        processor.getKit().pads[(size_t) currentPadIndex].rndPitchCents = (float) v;
        processor.kitEdited();
    };
    addAndMakeVisible (*pitchKnob);

    volKnob = std::make_unique<KnobControl> ("RND VOL", KnobControl::Layout::horizontal);
    volKnob->setRange (0.0, 6.0, 0.1);
    volKnob->setDefaultValue (0.0);
    volKnob->format = [] (double v) { return formatDbSpread (v); };
    volKnob->parse = [] (const juce::String& t) { return parseAmount (t); };
    volKnob->setTooltipText ("Random level per hit, +/- dB. 0.5-1.5 dB breathes; past 2 dB the groove starts to wobble.");
    volKnob->setThumbColour (MinigunLookAndFeel::teal);
    volKnob->onGestureStart = [this] { processor.beginUndoGesture(); };
    volKnob->onGestureEnd   = [this] { processor.endUndoGesture(); };
    volKnob->onValueChange = [this] (double v)
    {
        processor.getKit().pads[(size_t) currentPadIndex].rndVolDb = (float) v;
        processor.kitEdited();
    };
    addAndMakeVisible (*volKnob);
}

void AdvancedPanel::setTriggerModeFromUI (bool on)
{
    processor.getKit().triggerMode = on;
    processor.kitEdited();
    refresh();
}

void AdvancedPanel::updateEnablement()
{
    const bool triggerMode = processor.getKit().triggerMode;
    pitchKnob->setControlEnabled (! triggerMode);
    volKnob->setControlEnabled (! triggerMode);
}

void AdvancedPanel::refresh()
{
    currentPadIndex = processor.getSelectedPad();
    const auto& kit = processor.getKit();
    const auto& pad = kit.pads[(size_t) currentPadIndex];

    trigButton.setToggleState (kit.triggerMode, juce::dontSendNotification);
    MinigunLookAndFeel::setAccentButton (trigButton, kit.triggerMode);

    // While trigger mode is on the knobs show what is actually being applied, not the stored pad values.
    pitchKnob->setValue (kit.triggerMode ? kTriggerModeRndPitchCents : pad.rndPitchCents);
    volKnob->setValue (kit.triggerMode ? kTriggerModeRndVolDb : pad.rndVolDb);

    updateEnablement();
    repaint();
}

void AdvancedPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 6);

    auto controls = area.removeFromLeft (246);
    area.removeFromLeft (10);
    infoBounds = area;

    titleBounds = controls.removeFromTop (13);

    auto row = controls;
    trigButton.setBounds (row.removeFromLeft (50).withSizeKeepingCentre (50, 26));
    row.removeFromLeft (8);
    pitchKnob->setBounds (row.removeFromLeft (92));
    row.removeFromLeft (4);
    volKnob->setBounds (row.removeFromLeft (92));
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (MinigunLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (MinigunLookAndFeel::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::labelFont (10.0f));
    g.drawText (juce::String::fromUTF8 ("ADVANCED \xC2\xB7 HUMANIZE"), titleBounds, juce::Justification::centredLeft);

    // Help text: as many lines as fit, 11 px apart.
    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::sansFont (10.0f));
    const int lineHeight = 11;
    const int numLines = juce::jmin ((int) (sizeof (kInfoLines) / sizeof (kInfoLines[0])),
                                     juce::jmax (1, infoBounds.getHeight() / lineHeight));
    auto line = infoBounds.withHeight (lineHeight)
                          .withY (infoBounds.getY() + juce::jmax (0, (infoBounds.getHeight() - numLines * lineHeight) / 2));
    for (int i = 0; i < numLines; ++i)
    {
        g.drawText (kInfoLines[i], line, juce::Justification::centredLeft);
        line.translate (0, lineHeight);
    }
}

void AdvancedPanel::mouseDown (const juce::MouseEvent&)
{
    giveAwayKeyboardFocus();
}

} // namespace minigun
