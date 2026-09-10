#include "HeaderBar.h"
#include "MinigunLookAndFeel.h"
#include "../Model/KitModel.h"
#include "../Model/KitStore.h"
#include "../PluginProcessor.h"

namespace minigun
{

namespace
{
    juce::Path logoPath (juce::Rectangle<float> r)
    {
        juce::Path p;
        float g = r.getWidth() * 0.09f;
        float s = (r.getWidth() - g) * 0.5f;
        p.addRoundedRectangle (r.getX(), r.getY(), s, s, s * 0.2f);
        p.addRoundedRectangle (r.getX() + s + g, r.getY(), s, s, s * 0.2f);
        p.addRoundedRectangle (r.getX(), r.getY() + s + g, s, s, s * 0.2f);
        p.addRoundedRectangle (r.getX() + s + g, r.getY() + s + g, s, s, s * 0.2f);
        return p;
    }

    juce::Path saveIconPath (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addRoundedRectangle (r, 2.0f);
        p.startNewSubPath (r.getX() + r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.55f);
        p.lineTo (r.getRight() - r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.55f);
        p.lineTo (r.getRight() - r.getWidth() * 0.2f, r.getBottom());
        p.lineTo (r.getX() + r.getWidth() * 0.2f, r.getBottom());
        p.closeSubPath();
        return p;
    }

    juce::Path loadIconPath (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addRoundedRectangle (r, 2.0f);
        return p;
    }
}

HeaderBar::HeaderBar (MinigunAudioProcessor& processorIn) : processor (processorIn)
{
    setPaintingIsUnclipped (true);

    saveButton.onClick = [this] { saveKit(); };
    loadButton.onClick = [this] { loadKit(); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);

    masterKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible (masterKnob);
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "master", masterKnob);

    refreshKitInfo();
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::refreshKitInfo()
{
    auto& kit = processor.getKit();
    kitName = kit.name;
    padCount = kit.loadedPadCount();
    sampleCount = kit.totalSamples();
    repaint();
}

void HeaderBar::setMidiLedOn (bool on)
{
    if (midiLit != on) { midiLit = on; repaint(); }
}

void HeaderBar::setMeterLevels (float l, float r)
{
    peakL = l; peakR = r;
    repaint();
}

void HeaderBar::saveKit()
{
    auto& kit = processor.getKit();
    if (kit.folder.isDirectory())
    {
        processor.saveKitToFolder (kit.folder);
        refreshKitInfo();
        if (onKitChanged) onKitChanged();
        return;
    }

    auto startDir = KitStore::defaultKitsRoot();
    startDir.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser> ("Save kit to folder", startDir);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            auto dir = fc.getResult();
            if (dir == juce::File()) return;
            if (processor.saveKitToFolder (dir))
            {
                refreshKitInfo();
                if (onKitChanged) onKitChanged();
            }
        });
}

void HeaderBar::loadKit()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load kit from folder", KitStore::defaultKitsRoot());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            auto dir = fc.getResult();
            if (dir == juce::File()) return;
            if (processor.loadKitFromFolder (dir))
            {
                refreshKitInfo();
                if (onKitChanged) onKitChanged();
            }
        });
}

void HeaderBar::paint (juce::Graphics& g)
{
    g.setColour (MinigunLookAndFeel::amber);
    g.strokePath (logoPath (logoBounds.toFloat()), juce::PathStrokeType (2.0f));

    g.setColour (juce::Colour (0xffe9e6df));
    g.setFont (MinigunLookAndFeel::sansFont (22.0f, true));
    g.drawText ("MINIGUN", wordmarkBounds, juce::Justification::centredLeft);

    // Kit LCD.
    g.setColour (MinigunLookAndFeel::lcdBg);
    g.fillRoundedRectangle (kitLcdBounds.toFloat(), 4.0f);
    g.setColour (MinigunLookAndFeel::lcdBorder);
    g.drawRoundedRectangle (kitLcdBounds.toFloat().reduced (0.5f), 4.0f, 1.0f);

    auto lcdArea = kitLcdBounds.reduced (10, 0);
    g.setColour (juce::Colour (0xff6b6f77));
    g.setFont (MinigunLookAndFeel::monoFont (11.0f));
    auto kitLabelArea = lcdArea.removeFromLeft (30);
    g.drawText ("KIT", kitLabelArea, juce::Justification::centredLeft);

    g.setColour (MinigunLookAndFeel::amber);
    g.setFont (MinigunLookAndFeel::monoFont (13.0f));
    auto nameArea = lcdArea.removeFromLeft (juce::jmax (60, lcdArea.getWidth() - 130));
    g.drawText (kitName, nameArea, juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (0xff6b6f77));
    g.setFont (MinigunLookAndFeel::monoFont (11.0f));
    g.drawText (juce::String (padCount) + juce::String::fromUTF8 (" pads \xC2\xB7 ") + juce::String (sampleCount) + " smp",
               lcdArea, juce::Justification::centredRight);

    // Save/Load icons.
    auto drawBtnIcon = [&g] (juce::Rectangle<int> btnBounds, bool isSave)
    {
        auto iconArea = juce::Rectangle<float> ((float) btnBounds.getX() + 12.0f, btnBounds.getCentreY() - 6.0f, 12.0f, 12.0f);
        g.setColour (MinigunLookAndFeel::text);
        g.strokePath (isSave ? saveIconPath (iconArea) : loadIconPath (iconArea), juce::PathStrokeType (1.6f));
    };
    drawBtnIcon (saveButton.getBounds(), true);
    drawBtnIcon (loadButton.getBounds(), false);

    // MIDI LED.
    auto midiArea = juce::Rectangle<int> (meterBounds.getX() - 130, 0, 60, getHeight());
    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::labelFont (11.0f));
    g.drawText ("MIDI", midiArea.removeFromLeft (36), juce::Justification::centredLeft);
    auto ledR = 4.0f;
    auto ledCentre = juce::Point<float> ((float) midiArea.getX() + 8.0f, getHeight() * 0.5f);
    g.setColour (midiLit ? MinigunLookAndFeel::amber : MinigunLookAndFeel::amber.withAlpha (0.15f));
    if (midiLit)
    {
        juce::DropShadow ds (MinigunLookAndFeel::amber.withAlpha (0.8f), 8, {});
        juce::Path dot; dot.addEllipse (ledCentre.x - ledR, ledCentre.y - ledR, ledR * 2.0f, ledR * 2.0f);
        ds.drawForPath (g, dot);
    }
    g.fillEllipse (ledCentre.x - ledR, ledCentre.y - ledR, ledR * 2.0f, ledR * 2.0f);

    // Peak meter (8 segments, max(L,R)).
    static const float thresholds[8] = { -40.0f, -30.0f, -24.0f, -18.0f, -12.0f, -8.0f, -4.0f, 0.0f };
    static const bool isDarkSeg[8]   = { false, false, false, false, false, false, true, true };
    float level = juce::Decibels::gainToDecibels (juce::jmax (peakL, peakR), -80.0f);

    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::labelFont (11.0f));
    g.drawText ("OUT", meterBounds.withX (meterBounds.getX() - 34).withWidth (30), juce::Justification::centredLeft);

    int segW = 5, segGap = 2;
    for (int i = 0; i < 8; ++i)
    {
        bool lit = level >= thresholds[i];
        juce::Colour c = isDarkSeg[i] ? juce::Colour (0xff2a2e33)
                        : (i < 4 ? MinigunLookAndFeel::teal : MinigunLookAndFeel::amber);
        if (! lit) c = juce::Colour (0xff2a2e33);
        g.setColour (c);
        g.fillRect (meterBounds.getX() + i * (segW + segGap), meterBounds.getY(), segW, meterBounds.getHeight());
    }
}

void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced (24, 0);
    int midY = getHeight() / 2;

    logoBounds = juce::Rectangle<int> (area.getX(), midY - 11, 22, 22);
    area.removeFromLeft (22 + 10);
    wordmarkBounds = area.removeFromLeft (150).withY (0).withHeight (getHeight());
    area.removeFromLeft (16);

    kitLcdBounds = area.removeFromLeft (280).withHeight (30).withY (midY - 15);
    area.removeFromLeft (16);

    saveButton.setBounds (area.removeFromLeft (110).withHeight (30).withY (midY - 15));
    area.removeFromLeft (10);
    loadButton.setBounds (area.removeFromLeft (110).withHeight (30).withY (midY - 15));

    auto right = getLocalBounds().reduced (24, 0);
    masterKnob.setBounds (right.removeFromRight (34).withHeight (34).withY (midY - 17));
    right.removeFromRight (18);
    meterBounds = right.removeFromRight (8 * 5 + 7 * 2).withHeight (18).withY (midY - 9);
}

} // namespace minigun
