// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "PadEditorPanel.h"
#include "MinigunLookAndFeel.h"
#include "PadComponent.h"
#include "../Model/KitModel.h"
#include "../PluginProcessor.h"

namespace minigun
{

namespace
{
    juce::String formatDb (float db)
    {
        return juce::String (db, 1) + " dB";
    }

    juce::String formatPan (float pan)
    {
        if (std::abs (pan) < 0.005f) return "C";
        int pct = (int) std::round (std::abs (pan) * 100.0f);
        return (pan < 0.0f ? "L" : "R") + juce::String (pct);
    }

    juce::String formatSemitones (float st)
    {
        juce::String sign = st > 0.0f ? "+" : "";
        if (std::abs (st - std::round (st)) < 0.01f)
            return sign + juce::String ((int) std::round (st)) + " st";
        return sign + juce::String (st, 1) + " st";
    }

    juce::String formatMs (float ms)
    {
        return juce::String ((int) std::round (ms)) + " ms";
    }

    // Typed-value parsers for the knob fields: forgiving about the unit suffix the field shows.
    double parsePlainNumber (const juce::String& t) { return t.getDoubleValue(); }

    double parsePan (const juce::String& t)
    {
        auto s = t.trim().toUpperCase();
        if (s.isEmpty() || s == "C" || s == "0") return 0.0;
        if (s.startsWithChar ('L') || s.startsWithChar ('R'))
        {
            const double pct = s.substring (1).getDoubleValue();
            return (s.startsWithChar ('L') ? -1.0 : 1.0) * pct / 100.0;
        }
        const double v = s.getDoubleValue();
        return std::abs (v) > 1.0 ? v / 100.0 : v; // "-50" reads as 50% left, "-0.5" as the raw value
    }

    double parseDecay (const juce::String& t)
    {
        auto s = t.trim().toUpperCase();
        if (s.startsWithChar ('F')) return 5001.0; // "Full"
        return s.getDoubleValue();
    }

    juce::String noteComboText (int note)
    {
        return noteToShortName (note) + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (note);
    }
}

PadEditorPanel::PadEditorPanel (MinigunAudioProcessor& processorIn) : processor (processorIn)
{
    setPaintingIsUnclipped (true);

    nameEditor.setFont (MinigunLookAndFeel::monoFont (14.0f));
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setSelectAllWhenFocused (true);
    auto commitName = [this]
    {
        writeAndNotify ([this] { processor.getKit().pads[(size_t) currentPadIndex].name = nameEditor.getText(); });
    };
    nameEditor.onTextChange = commitName; // live: the name is saved as it is typed
    nameEditor.onFocusLost = commitName;
    nameEditor.onReturnKey = [this, commitName] { commitName(); giveAwayKeyboardFocus(); };
    nameEditor.onEscapeKey = [this] { giveAwayKeyboardFocus(); };
    addAndMakeVisible (nameEditor);

    rrButton.setClickingTogglesState (true);
    rndButton.setClickingTogglesState (true);
    rrButton.setRadioGroupId (0xbeef1);
    rndButton.setRadioGroupId (0xbeef1);
    rrButton.onClick = [this]
    {
        if (rrButton.getToggleState())
            writeAndNotify ([this] { processor.getKit().pads[(size_t) currentPadIndex].mode = PlayMode::RoundRobin; });
    };
    rndButton.onClick = [this]
    {
        if (rndButton.getToggleState())
            writeAndNotify ([this] { processor.getKit().pads[(size_t) currentPadIndex].mode = PlayMode::Random; });
    };
    addAndMakeVisible (rrButton);
    addAndMakeVisible (rndButton);

    learnButton.onClick = [this] { processor.armMidiLearn (currentPadIndex); };
    addAndMakeVisible (learnButton);

    // NOTE: 128-item combo + a small digit-only editor, kept in sync with each other.
    noteCombo.setScrollWheelEnabled (true); // items are ordered 0..127, so wheel = +/-1 note
    for (int n = 0; n < 128; ++n)
        noteCombo.addItem (noteComboText (n), n + 1);
    noteCombo.onChange = [this]
    {
        int note = noteCombo.getSelectedId() - 1;
        if (note >= 0)
            setNoteFromUI (note);
    };
    addAndMakeVisible (noteCombo);

    noteNumberEditor.setFont (MinigunLookAndFeel::monoFont (13.0f));
    noteNumberEditor.setJustification (juce::Justification::centred);
    noteNumberEditor.setInputRestrictions (3, "0123456789");
    noteNumberEditor.setSelectAllWhenFocused (true);
    auto commitNoteNumber = [this]
    {
        int note = juce::jlimit (0, 127, noteNumberEditor.getText().getIntValue());
        setNoteFromUI (note);
    };
    noteNumberEditor.onFocusLost = commitNoteNumber;
    noteNumberEditor.onReturnKey = [this, commitNoteNumber] { commitNoteNumber(); giveAwayKeyboardFocus(); };
    noteNumberEditor.onEscapeKey = [this] { giveAwayKeyboardFocus(); };
    addAndMakeVisible (noteNumberEditor);

    // CHOKE: "-" (id 1, group 0) then 1..8 (id 2..9, group = id-1).
    chokeCombo.addItem (juce::String::fromUTF8 ("\xE2\x80\x94"), 1);
    for (int i = 1; i <= 8; ++i)
        chokeCombo.addItem (juce::String (i), i + 1);
    chokeCombo.onChange = [this]
    {
        int choke = chokeCombo.getSelectedId() - 1;
        writeAndNotify ([this, choke] { processor.getKit().pads[(size_t) currentPadIndex].chokeGroup = choke; });
    };
    addAndMakeVisible (chokeCombo);

    // OUT: 17 items, ids 1..17 = bus index (0 = Main) + 1. Label text depends on outputMode.
    rebuildOutCombo (OutputMode::Stereo);
    outCombo.onChange = [this]
    {
        int output = outCombo.getSelectedId() - 1;
        if (output >= 0)
            writeAndNotify ([this, output] { processor.getKit().pads[(size_t) currentPadIndex].output = output; });
    };
    addAndMakeVisible (outCombo);

    modeStButton.setClickingTogglesState (true);
    modeLButton.setClickingTogglesState (true);
    modeRButton.setClickingTogglesState (true);
    modeStButton.setRadioGroupId (0xbeef2);
    modeLButton.setRadioGroupId (0xbeef2);
    modeRButton.setRadioGroupId (0xbeef2);
    modeStButton.onClick = [this] { if (modeStButton.getToggleState()) setOutputModeFromUI (OutputMode::Stereo); };
    modeLButton.onClick  = [this] { if (modeLButton.getToggleState())  setOutputModeFromUI (OutputMode::MonoLeft); };
    modeRButton.onClick  = [this] { if (modeRButton.getToggleState())  setOutputModeFromUI (OutputMode::MonoRight); };
    addAndMakeVisible (modeStButton);
    addAndMakeVisible (modeLButton);
    addAndMakeVisible (modeRButton);

    sumButton.setClickingTogglesState (true);
    sumButton.onClick = [this]
    {
        bool sum = sumButton.getToggleState();
        writeAndNotify ([this, sum] { processor.getKit().pads[(size_t) currentPadIndex].monoSum = sum; });
    };
    addAndMakeVisible (sumButton);

    // VEL: lit = MIDI velocity scales the pad's volume (classic behaviour). Off = every hit plays at
    // full level; the velocity still chooses the layer. Ctrl+click sets every pad at once.
    velButton.setClickingTogglesState (true);
    velButton.setTooltip ("Velocity controls volume (off: every hit at full level, layers still follow velocity). Ctrl+click = all pads");
    velButton.onClick = [this]
    {
        setVelocityToVolumeFromUI (velButton.getToggleState(),
                                   juce::ModifierKeys::getCurrentModifiers().isCommandDown());
    };
    addAndMakeVisible (velButton);

    // Six knobs: Ctrl+click resets to the default, the field under each one takes a typed value.
    struct KnobSpec
    {
        const char* caption;
        double lo, hi, interval, defaultValue;
        juce::String (*format) (float);
        double (*parse) (const juce::String&);
        bool teal;
        const char* tooltip;
    };

    static const KnobSpec specs[6] =
    {
        { "Vol",   -60.0,  12.0, 0.1,    0.0, formatDb,        parsePlainNumber, false, "Pad level" },
        { "Pan",    -1.0,   1.0, 0.01,   0.0, formatPan,       parsePan,         false, "Pan (type L50 / C / R50)" },
        { "Pitch", -12.0,  12.0, 0.1,    0.0, formatSemitones, parsePlainNumber, false, "Transpose in semitones" },
        { "Atk",     0.0, 500.0, 1.0,    0.0, formatMs,        parsePlainNumber, true,  "Attack" },
        { "Dec",     1.0, 5001.0, 1.0, 5001.0, nullptr,        parseDecay,       true,  "Decay (type Full for the whole sample)" },
        { "Rel",     1.0, 2000.0, 1.0,  120.0, formatMs,       parsePlainNumber, true,  "Release" }
    };

    for (int i = 0; i < 6; ++i)
    {
        auto& spec = specs[i];
        auto k = std::make_unique<KnobControl> (spec.caption);
        k->setRange (spec.lo, spec.hi, spec.interval);
        k->setDefaultValue (spec.defaultValue);
        k->setTooltipText (spec.tooltip);

        if (auto* fmt = spec.format)
            k->format = [fmt] (double v) { return fmt ((float) v); };
        else
            k->format = [] (double v) { return v >= 5001.0 ? juce::String ("Full") : formatMs ((float) v); };

        auto* parseFn = spec.parse;
        k->parse = [parseFn] (const juce::String& t) { return parseFn (t); };

        if (spec.teal)
            k->setThumbColour (MinigunLookAndFeel::teal);

        k->onGestureStart = [this] { processor.beginUndoGesture(); }; // a whole knob sweep = one undo step
        k->onGestureEnd   = [this] { processor.endUndoGesture(); };

        addAndMakeVisible (*k);
        knobs[(size_t) i] = std::move (k);
    }

    knobs[0]->onValueChange = [this] (double v) { writeAndNotify ([this, v] { processor.getKit().pads[(size_t) currentPadIndex].volumeDb = (float) v; }); };
    knobs[1]->onValueChange = [this] (double v) { writeAndNotify ([this, v] { processor.getKit().pads[(size_t) currentPadIndex].pan = (float) v; }); };
    knobs[2]->onValueChange = [this] (double v) { writeAndNotify ([this, v] { processor.getKit().pads[(size_t) currentPadIndex].pitchSemitones = (float) v; }); };
    knobs[3]->onValueChange = [this] (double v) { writeAndNotify ([this, v] { processor.getKit().pads[(size_t) currentPadIndex].attackMs = (float) v; }); };
    knobs[4]->onValueChange = [this] (double v) { writeAndNotify ([this, v] { processor.getKit().pads[(size_t) currentPadIndex].decayMs = v >= 5001.0 ? -1.0f : (float) v; }); };
    knobs[5]->onValueChange = [this] (double v) { writeAndNotify ([this, v] { processor.getKit().pads[(size_t) currentPadIndex].releaseMs = (float) v; }); };

    startTimerHz (15);
}

PadEditorPanel::~PadEditorPanel() { stopTimer(); }

void PadEditorPanel::writeAndNotify (std::function<void()> mutator)
{
    mutator();
    processor.kitEdited();
}

void PadEditorPanel::rebuildOutCombo (OutputMode mode)
{
    int prevId = outCombo.getSelectedId();
    outCombo.clear (juce::dontSendNotification);

    for (int busIdx = 0; busIdx <= 16; ++busIdx)
    {
        juce::String label;
        if (mode == OutputMode::Stereo)
        {
            label = busIdx == 0 ? juce::String ("Main")
                                 : "Out " + juce::String (2 * busIdx - 1) + "-" + juce::String (2 * busIdx);
        }
        else
        {
            const bool left = mode == OutputMode::MonoLeft;
            if (busIdx == 0)
                label = left ? "Main L" : "Main R";
            else
                label = "Out " + juce::String (left ? (2 * busIdx - 1) : (2 * busIdx));
        }
        outCombo.addItem (label, busIdx + 1);
    }

    if (prevId > 0)
        outCombo.setSelectedId (prevId, juce::dontSendNotification);
}

void PadEditorPanel::setNoteFromUI (int note)
{
    note = juce::jlimit (0, 127, note);
    writeAndNotify ([this, note] { processor.getKit().pads[(size_t) currentPadIndex].note = note; });
    noteCombo.setSelectedId (note + 1, juce::dontSendNotification);
    noteNumberEditor.setText (juce::String (note), juce::dontSendNotification);
    repaint();
}

void PadEditorPanel::setOutputModeFromUI (OutputMode mode)
{
    writeAndNotify ([this, mode] { processor.getKit().pads[(size_t) currentPadIndex].outputMode = mode; });
    rebuildOutCombo (mode);
    sumButton.setEnabled (mode != OutputMode::Stereo);
    sumButton.setAlpha ((mode != OutputMode::Stereo) ? 1.0f : 0.35f); // dim when stereo: SUM only matters for mono outputs
}

void PadEditorPanel::setVelocityToVolumeFromUI (bool on, bool applyToAllPads)
{
    writeAndNotify ([this, on, applyToAllPads]
    {
        auto& pads = processor.getKit().pads;
        if (applyToAllPads)
            for (auto& p : pads) p.velocityToVolume = on;
        else
            pads[(size_t) currentPadIndex].velocityToVolume = on;
    });
}

void PadEditorPanel::refresh()
{
    const bool padChanged = (processor.getSelectedPad() != currentPadIndex);
    currentPadIndex = processor.getSelectedPad();
    auto& pad = processor.getKit().pads[(size_t) currentPadIndex];

    // Keep the user's in-progress typing, but never show a stale name after switching pads.
    if (padChanged && nameEditor.hasKeyboardFocus (false))
        giveAwayKeyboardFocus();
    if (padChanged || ! nameEditor.hasKeyboardFocus (false))
        nameEditor.setText (pad.name, juce::dontSendNotification);

    rrButton.setToggleState (pad.mode == PlayMode::RoundRobin, juce::dontSendNotification);
    rndButton.setToggleState (pad.mode == PlayMode::Random, juce::dontSendNotification);

    noteCombo.setSelectedId (pad.note + 1, juce::dontSendNotification);
    if (padChanged || ! noteNumberEditor.hasKeyboardFocus (false))
        noteNumberEditor.setText (juce::String (pad.note), juce::dontSendNotification);

    chokeCombo.setSelectedId (pad.chokeGroup + 1, juce::dontSendNotification);

    rebuildOutCombo (pad.outputMode);
    outCombo.setSelectedId (pad.output + 1, juce::dontSendNotification);
    modeStButton.setToggleState (pad.outputMode == OutputMode::Stereo, juce::dontSendNotification);
    modeLButton.setToggleState (pad.outputMode == OutputMode::MonoLeft, juce::dontSendNotification);
    modeRButton.setToggleState (pad.outputMode == OutputMode::MonoRight, juce::dontSendNotification);
    sumButton.setToggleState (pad.monoSum, juce::dontSendNotification);
    sumButton.setEnabled (pad.outputMode != OutputMode::Stereo);
    sumButton.setAlpha ((pad.outputMode != OutputMode::Stereo) ? 1.0f : 0.35f); // dim when stereo: SUM only matters for mono outputs

    velButton.setToggleState (pad.velocityToVolume, juce::dontSendNotification);

    knobs[0]->setValue (pad.volumeDb);
    knobs[1]->setValue (pad.pan);
    knobs[2]->setValue (pad.pitchSemitones);
    knobs[3]->setValue (pad.attackMs);
    knobs[4]->setValue (pad.decayMs < 0.0f ? 5001.0 : pad.decayMs);
    knobs[5]->setValue (pad.releaseMs);

    repaint();
}

void PadEditorPanel::timerCallback()
{
    bool armed = processor.isLearnArmed();
    if (armed != lastLearnArmed)
    {
        lastLearnArmed = armed;
        learnButton.setToggleState (armed, juce::dontSendNotification);
    }
}

void PadEditorPanel::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto row1 = area.removeFromTop (28);
    badgeBounds = row1.removeFromLeft (28);
    row1.removeFromLeft (10);
    auto segArea = row1.removeFromRight (110);
    nameEditor.setBounds (row1.removeFromLeft (140));

    rrButton.setBounds (segArea.removeFromLeft (52));
    rndButton.setBounds (segArea.removeFromLeft (58));

    area.removeFromTop (10);

    // Row 2: NOTE combo + number, CHOKE combo, MIDI learn.
    auto row2 = area.removeFromTop (44);
    auto learnCol = row2.removeFromRight (54);
    auto noteCol  = row2.removeFromLeft (178); // combo 130 + gap 8 + number 40
    row2.removeFromLeft (8);
    auto chokeCol = row2.removeFromLeft (54);

    noteCol.removeFromTop (14);
    auto noteComboArea = noteCol.removeFromLeft (130);
    noteCol.removeFromLeft (8);
    noteCombo.setBounds (noteComboArea.withHeight (30));
    noteNumberEditor.setBounds (noteCol.withHeight (30));

    chokeCol.removeFromTop (14);
    chokeCombo.setBounds (chokeCol.withHeight (30));

    learnCol.removeFromTop (14);
    learnButton.setBounds (learnCol.withHeight (30));

    area.removeFromTop (6);

    // Row 3: OUT combo, ST|L|R mode toggle, SUM.
    auto row3 = area.removeFromTop (44);
    row3.removeFromTop (14);
    auto outCol = row3.removeFromLeft (112);
    row3.removeFromLeft (8);
    auto modeCol = row3.removeFromLeft (84);
    row3.removeFromLeft (8);
    auto sumCol = row3.removeFromLeft (48);
    row3.removeFromLeft (8);
    auto velCol = row3.removeFromLeft (48);

    outCombo.setBounds (outCol.withHeight (30));

    int segW = modeCol.getWidth() / 3;
    modeStButton.setBounds (modeCol.removeFromLeft (segW));
    modeLButton.setBounds (modeCol.removeFromLeft (segW));
    modeRButton.setBounds (modeCol);

    sumButton.setBounds (sumCol.withHeight (30));
    velButton.setBounds (velCol.withHeight (30));

    area.removeFromTop (6);

    auto knobRow = area.removeFromTop (area.getHeight());
    int knobW = knobRow.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
        knobs[(size_t) i]->setBounds (knobRow.removeFromLeft (i == 5 ? knobRow.getWidth() : knobW));
}

void PadEditorPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (MinigunLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (MinigunLookAndFeel::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Pad number badge.
    auto badge = badgeBounds.toFloat();
    juce::ColourGradient grad (MinigunLookAndFeel::padRubberTop, badge.getX(), badge.getY(),
                               MinigunLookAndFeel::padRubberBottom, badge.getX(), badge.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (badge, 6.0f);
    g.setColour (MinigunLookAndFeel::teal);
    g.drawRoundedRectangle (badge.expanded (1.5f), 7.0f, 2.0f);
    g.setColour (juce::Colour (0xff2a2c30));
    g.setFont (MinigunLookAndFeel::sansFont (13.0f, true));
    g.drawText (juce::String (currentPadIndex + 1), badgeBounds, juce::Justification::centred);

    auto drawLabelAbove = [&g] (const juce::Component& c, const juce::String& labelText)
    {
        auto b = c.getBounds();
        juce::Rectangle<int> labelArea (b.getX(), b.getY() - 19, juce::jmax (60, b.getWidth()), 14);
        g.setColour (MinigunLookAndFeel::label);
        g.setFont (MinigunLookAndFeel::labelFont (11.0f));
        g.drawText (labelText, labelArea, juce::Justification::centredLeft);
    };

    drawLabelAbove (noteCombo, "NOTE");
    drawLabelAbove (chokeCombo, "CHOKE");
    drawLabelAbove (outCombo, "OUT");
    drawLabelAbove (learnButton, "LEARN");
}

} // namespace minigun

namespace minigun
{
void PadEditorPanel::mouseDown (const juce::MouseEvent&)
{
    // JUCE keeps focus inside a parent that already contains the focused child; hand it back to the
    // editor root so the name / note fields commit and lose their frame on any click elsewhere.
    giveAwayKeyboardFocus();
}
} // namespace minigun
