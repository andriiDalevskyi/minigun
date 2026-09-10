// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Model/KitModel.h"

using namespace minigun;

MinigunAudioProcessorEditor::MinigunAudioProcessorEditor (MinigunAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      audioProcessor (p),
      header (p),
      padEditor (p),
      layerEditor (p),
      browser (p)
{
    setLookAndFeel (&lookAndFeel);

    // The editor root accepts keyboard focus itself. Without this, JUCE hands focus to the first
    // focusable child (the pad-name field) every time the plug-in window is activated, e.g. when
    // the host focuses the FX window on mouse-over; clicking on empty space also lands here, which
    // makes text fields commit on any click elsewhere.
    setWantsKeyboardFocus (true);
    addMouseListener (this, true); // see mouseDown(): clicks anywhere release text-field focus

    addAndMakeVisible (header);
    addAndMakeVisible (padGrid);
    addAndMakeVisible (padEditor);
    addAndMakeVisible (layerEditor);
    addAndMakeVisible (browser);

    padGrid.onSelect = [this] (int idx)
    {
        audioProcessor.setSelectedPad (idx);
        layerEditor.notifyPadChanged();
        refreshAll();
    };
    padGrid.onTrigger = [this] (int idx, int vel) { auditionPad (idx, vel); };
    layerEditor.onAudition = [this] (int vel) { auditionPad (audioProcessor.getSelectedPad(), vel); };
    padGrid.onFilesDropped = [this] (int idx, juce::Array<juce::File> files) { handlePadDrop (idx, files); };

    browser.onAddToLayer = [this] (juce::Array<juce::File> files) { handleAddToLayer (files); };
    layerEditor.onFilesDroppedOnLayer = [this] (int idx, juce::Array<juce::File> files) { handleAddToLayer (files, idx); };

    header.onKitChanged = [this] { refreshAll(); };

    audioProcessor.addChangeListener (this);

    setSize (1200, 720);
    setResizable (false, false);

    refreshAll();
    startTimerHz (30);
}

MinigunAudioProcessorEditor::~MinigunAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

static void autoNamePad (minigun::Pad& pad, const juce::File& firstFile)
{
    if (pad.name.isEmpty())
        pad.name = firstFile.getFileNameWithoutExtension().substring (0, 24).trim();
}

// The layer colour must stay readable even for soft hits, so the glow never drops below 55%.
static float glowForVelocity (int velocity)
{
    return juce::jlimit (0.0f, 1.0f, 0.55f + 0.45f * (float) velocity / 127.0f);
}

static constexpr int kGlowHoldTicks = 4; // ~130 ms at 30 Hz

void MinigunAudioProcessorEditor::auditionPad (int padIndex, int velocity)
{
    if (padIndex < 0 || padIndex >= 16) return;
    velocity = juce::jlimit (1, 127, velocity);

    audioProcessor.triggerPad (padIndex, velocity);

    // Instant visual feedback, independent of the audio device: same colour rule as a MIDI hit.
    const auto& pad = audioProcessor.getKit().pads[(size_t) padIndex];
    padGrid.setPadGlowLayer (padIndex, pad.layerForVelocity (velocity), (int) pad.layers.size());
    padGlow[(size_t) padIndex] = glowForVelocity (velocity);
    padGlowHold[(size_t) padIndex] = kGlowHoldTicks;
    padGrid.setPadGlow (padIndex, padGlow[(size_t) padIndex]);
}

void MinigunAudioProcessorEditor::handlePadDrop (int padIndex, juce::Array<juce::File> files)
{
    if (files.isEmpty()) return;

    auto& kit = audioProcessor.getKit();
    auto& pad = kit.pads[(size_t) padIndex];

    std::vector<minigun::SampleRef> refs;
    for (auto& f : files) refs.push_back ({ f, false });

    // Dropped files join the pad's ACTIVE layer. New layers are only ever created by the user
    // (+ LAYER); the single exception is a pad with no layers at all, which needs its first one.
    const bool samePad = (padIndex == audioProcessor.getSelectedPad());

    if (pad.layers.empty())
    {
        pad.layers.push_back ({ 1, 127, {} });
    }

    int target = samePad ? layerEditor.getSelectedLayer() : (int) pad.layers.size() - 1; // other pad: its top layer
    target = juce::jlimit (0, (int) pad.layers.size() - 1, target);
    for (auto& r : refs)
        pad.layers[(size_t) target].samples.push_back (r);

    autoNamePad (pad, files.getFirst());
    audioProcessor.setSelectedPad (padIndex);
    audioProcessor.kitEdited();
    if (! samePad) layerEditor.notifyPadChanged();
    refreshAll();
}

void MinigunAudioProcessorEditor::handleAddToLayer (juce::Array<juce::File> files, int layerIndex)
{
    if (files.isEmpty()) return;

    auto& pad = audioProcessor.getKit().pads[(size_t) audioProcessor.getSelectedPad()];
    int layerIdx = layerIndex >= 0 ? layerIndex : layerEditor.getSelectedLayer();

    if (pad.layers.empty())
    {
        pad.layers.push_back ({ 1, 127, {} });
        layerIdx = 0;
    }
    layerIdx = juce::jlimit (0, (int) pad.layers.size() - 1, layerIdx);

    for (auto& f : files)
        pad.layers[(size_t) layerIdx].samples.push_back ({ f, false });
    autoNamePad (pad, files.getFirst());

    audioProcessor.kitEdited();
    refreshAll();
}

void MinigunAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // If a text field has focus and the click landed anywhere else (a knob, a pad, empty space),
    // take the focus back so stray keystrokes cannot edit the field.
    if (auto* focused = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()))
    {
        auto* clicked = e.eventComponent;
        if (clicked != focused && ! focused->isParentOf (clicked))
            grabKeyboardFocus();
    }
}

bool MinigunAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        // With Ctrl held, JUCE on Windows reports the control character (Ctrl+Z = 26, Ctrl+Y = 25);
        // other platforms report the letter. Accept both.
        const auto code = key.getKeyCode();
        if (code == 'Z' || code == 'z' || code == 26)
        {
            if (mods.isShiftDown()) audioProcessor.redo(); else audioProcessor.undo();
            return true;
        }
        if ((code == 'Y' || code == 'y' || code == 25) && ! mods.isShiftDown())
        {
            audioProcessor.redo();
            return true;
        }
    }
    return false;
}

void MinigunAudioProcessorEditor::refreshAll()
{
    auto& kit = audioProcessor.getKit();
    int sel = audioProcessor.getSelectedPad();

    padGrid.refresh (kit, sel);
    padEditor.refresh();
    layerEditor.refresh();
    browser.setCurrentLayerNumber (layerEditor.getSelectedLayer() + 1);
    header.refreshKitInfo();
    repaint();
}

void MinigunAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();
}

void MinigunAudioProcessorEditor::timerCallback()
{
    const float decayPerTick = (1000.0f / 30.0f) / 150.0f; // ~150ms full decay

    for (int i = 0; i < 16; ++i)
    {
        int layerIndex = -1;
        int vel = audioProcessor.getAndClearPadHit (i, &layerIndex);
        if (vel > 0)
        {
            padGlow[(size_t) i] = glowForVelocity (vel);
            padGlowHold[(size_t) i] = kGlowHoldTicks;
            // Light the pad in the colour of the velocity layer that just played.
            const int layerCount = (int) audioProcessor.getKit().pads[(size_t) i].layers.size();
            padGrid.setPadGlowLayer (i, layerIndex, layerCount);
        }
        else if (padGlowHold[(size_t) i] > 0)
            --padGlowHold[(size_t) i]; // hold the layer colour for a moment before fading
        else
            padGlow[(size_t) i] = juce::jmax (0.0f, padGlow[(size_t) i] - decayPerTick);

        padGrid.setPadGlow (i, padGlow[(size_t) i]);
    }

    header.setMeterLevels (audioProcessor.outputPeakL.load(), audioProcessor.outputPeakR.load());

    if (audioProcessor.midiActivity.exchange (false))
        lastMidiActivityMs = juce::Time::getMillisecondCounter();
    header.setMidiLedOn ((juce::Time::getMillisecondCounter() - lastMidiActivityMs) < 100);

    int voices = audioProcessor.getActiveVoiceCount();
    if (voices != lastVoiceCount)
    {
        lastVoiceCount = voices;
        repaint (footerBounds);
    }
}

void MinigunAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient bg (MinigunLookAndFeel::body, 0.0f, 0.0f,
                             MinigunLookAndFeel::bodyDark, 0.0f, bounds.getHeight(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (MinigunLookAndFeel::outerBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (juce::Colour (0xff0f1113));
    g.drawLine (0.0f, 56.0f, bounds.getWidth(), 56.0f, 1.0f);
    g.drawLine (0.0f, bounds.getHeight() - 28.0f, bounds.getWidth(), bounds.getHeight() - 28.0f, 1.0f);

    g.setColour (juce::Colour (0xff6b6f77));
    g.setFont (MinigunLookAndFeel::monoFont (10.0f));

    auto& kit = audioProcessor.getKit();
    juce::String kitFolderText = kit.folder.isDirectory()
        ? "Kit folder: " + kit.folder.getFullPathName().replaceCharacter ('\\', '/') + juce::String::fromUTF8 ("/ \xC2\xB7 samples copied on save")
        : juce::String::fromUTF8 ("Kit folder: (unsaved) \xC2\xB7 samples copied on save");
    g.drawText (kitFolderText, footerBounds.reduced (0, 0), juce::Justification::centredLeft);

    // JucePlugin_VersionString comes from project(Minigun VERSION x.y.z) in CMakeLists.txt.
    juce::String rightText = juce::String ("v") + JucePlugin_VersionString + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (audioProcessor.getActiveVoiceCount()) + juce::String::fromUTF8 (" voices \xC2\xB7 outs ") + juce::String (audioProcessor.getUsableBusCount()) + "/17";
    g.drawText (rightText, footerBounds, juce::Justification::centredRight);
}

void MinigunAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (56);
    header.setBounds (headerArea);

    footerBounds = area.removeFromBottom (28).reduced (24, 0);

    auto body = area.reduced (24, 20);

    auto padCol = body.removeFromLeft (518); // 4 x 128 + 3 x 2 (pads carry a 5px outline margin)
    body.removeFromLeft (15);
    auto midCol = body.removeFromLeft (360);
    body.removeFromLeft (20);
    auto browserCol = body; // absorbs any remainder so the fixed window never overflows

    padGrid.setBounds (padCol);

    auto padEditorArea = midCol.removeFromTop (PadEditorPanel::kPreferredHeight);
    midCol.removeFromTop (14);
    padEditor.setBounds (padEditorArea);
    layerEditor.setBounds (midCol);

    browser.setBounds (browserCol);
}
