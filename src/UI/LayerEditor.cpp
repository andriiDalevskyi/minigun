// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "LayerEditor.h"
#include "MinigunLookAndFeel.h"
#include "PadComponent.h"
#include "../Model/KitModel.h"
#include "../PluginProcessor.h"

namespace minigun
{

namespace
{
    const juce::Colour kDarkTop   { 0xff5a4a2c };
    const juce::Colour kDarkBottom{ 0xff3c3220 };
    const juce::Colour kBrightTop { 0xfff2a33a };
    const juce::Colour kBrightBottom { 0xffc77d1f };

    void layerRampColours (int index, int total, juce::Colour& top, juce::Colour& bottom)
    {
        MinigunLookAndFeel::layerColours (index, total, top, bottom); // shared with the pad glow
    }

    juce::Colour badgeTextFor (juce::Colour bg)
    {
        return bg.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff2a1a04) : juce::Colour (0xffe6c68f);
    }
}

//==============================================================================
class SampleChip : public juce::Component
{
public:
    explicit SampleChip (juce::File f) : file (std::move (f)) {}

    int padIndex = 0, layerIndex = 0, sampleIndex = 0; // identity carried by a chip drag

    std::function<void()> onPreviewClick;
    std::function<void()> onRemoveClick;

    /** Dims the chip while it is the source of a drag. */
    void setBeingDragged (bool shouldBeDragged)
    {
        if (beingDragged != shouldBeDragged) { beingDragged = shouldBeDragged; repaint(); }
    }

    int preferredWidth() const
    {
        auto f = MinigunLookAndFeel::monoFont (11.0f);
        return juce::GlyphArrangement::getStringWidthInt (f, file.getFileName()) + 16 + 18;
    }

    void paint (juce::Graphics& g) override
    {
        const float a = beingDragged ? 0.3f : 1.0f; // the chip stays in place, dimmed, while it is dragged

        auto b = getLocalBounds().toFloat();
        g.setColour (MinigunLookAndFeel::segBg.withMultipliedAlpha (a));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0xff33373e).withMultipliedAlpha (a));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        auto textArea = getLocalBounds().reduced (8, 0);
        textArea.removeFromRight (16);
        g.setColour (MinigunLookAndFeel::text.withMultipliedAlpha (a));
        g.setFont (MinigunLookAndFeel::monoFont (11.0f));
        g.drawText (file.getFileName(), textArea, juce::Justification::centredLeft, true);

        auto xArea = getLocalBounds().removeFromRight (18).toFloat().reduced (6.0f);
        g.setColour (MinigunLookAndFeel::label.withMultipliedAlpha (a));
        g.drawLine (xArea.getX(), xArea.getY(), xArea.getRight(), xArea.getBottom(), 1.6f);
        g.drawLine (xArea.getRight(), xArea.getY(), xArea.getX(), xArea.getBottom(), 1.6f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragStarted = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Same guard as the pad grid: the mouse has to really travel, so a click on the chip
        // (preview / remove) never turns into a drag by accident.
        if (dragStarted || e.getDistanceFromDragStart() < 8) return;

        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
        if (container == nullptr) return;

        auto snapshot = createComponentSnapshot (getLocalBounds(), true); // taken before dimming
        juce::Image faded (juce::Image::ARGB, snapshot.getWidth(), snapshot.getHeight(), true);
        {
            juce::Graphics ig (faded);
            ig.setOpacity (0.8f);
            ig.drawImageAt (snapshot, 0, 0);
        }

        dragStarted = true;
        setBeingDragged (true);

        auto offset = -e.getPosition();
        container->startDragging (LayerEditor::makeSampleDragDescription (padIndex, layerIndex, sampleIndex),
                                  this, juce::ScaledImage (faded), false, &offset, &e.source);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragStarted) { dragStarted = false; return; } // the drag consumed this gesture

        auto xHit = getLocalBounds().removeFromRight (18);
        if (xHit.contains (e.getPosition())) { if (onRemoveClick) onRemoveClick(); }
        else                                  { if (onPreviewClick) onPreviewClick(); }
    }

private:
    juce::File file;
    bool dragStarted = false;
    bool beingDragged = false;
};

//==============================================================================
class AddChip : public juce::Component
{
public:
    std::function<void()> onClick;

    static constexpr int preferredWidth = 56;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::Path dashed;
        dashed.addRoundedRectangle (b.reduced (0.5f), 4.0f);
        float dashLengths[] = { 3.0f, 2.5f };
        juce::Path stroked;
        juce::PathStrokeType (1.2f).createDashedStroke (stroked, dashed, dashLengths, 2);
        g.setColour (MinigunLookAndFeel::teal);
        g.fillPath (stroked);

        g.setFont (MinigunLookAndFeel::monoFont (11.0f));
        g.drawText ("+ add", getLocalBounds(), juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
};

//==============================================================================
class LayerRow : public juce::Component
{
public:
    int layerIndex = 0;
    int padIndex = 0;
    juce::Colour badgeTop, badgeBottom;
    bool selected = false;

    std::function<void()> onSelect;
    std::function<void()> onDeleteRequested;
    std::function<void (juce::File)> onPreviewFile;
    std::function<void (int)> removeSampleAt; // sampleIdx
    std::function<void()> onAddFiles;

    void setSamples (const std::vector<minigun::SampleRef>& samples)
    {
        chips.clear();
        for (size_t i = 0; i < samples.size(); ++i)
        {
            auto chip = std::make_unique<SampleChip> (samples[i].file);
            auto idx = (int) i;
            chip->padIndex = padIndex;
            chip->layerIndex = layerIndex;
            chip->sampleIndex = idx;
            chip->onPreviewClick = [this, file = samples[i].file] { if (onPreviewFile) onPreviewFile (file); };
            chip->onRemoveClick = [this, idx] { if (removeSampleAt) removeSampleAt (idx); };
            addAndMakeVisible (*chip);
            chips.push_back (std::move (chip));
        }
        addChip.onClick = [this] { if (onAddFiles) onAddFiles(); };
        addAndMakeVisible (addChip);
    }

    int computeHeightForWidth (int width) const
    {
        return layoutChips (width, false);
    }

    /** Insertion slot for a chip dropped at this row-local position: the number of chips that
        come before the point in reading order (0 = before the first chip, size = after the last). */
    int insertIndexForLocalPos (juce::Point<int> p) const
    {
        int index = 0;
        for (auto& c : chips)
        {
            auto b = c->getBounds();
            const bool pointIsPastChip = p.y >= b.getBottom()
                                      || (p.y >= b.getY() && p.x >= b.getCentreX());
            if (! pointIsPastChip) break;
            ++index;
        }
        return index;
    }

    /** Row-local bounds of the insertion caret drawn for `index`. */
    juce::Rectangle<int> caretBoundsForIndex (int index) const
    {
        const int rowH = 26;
        if (chips.empty())
            return { addChip.getX() - 4, addChip.getY(), 3, rowH };

        if (index >= (int) chips.size())
        {
            auto b = chips.back()->getBounds();
            return { b.getRight() + 2, b.getY(), 3, rowH };
        }

        auto b = chips[(size_t) juce::jmax (0, index)]->getBounds();
        return { b.getX() - 4, b.getY(), 3, rowH };
    }

    /** Dims the chip that is being dragged out of this row; -1 undims every chip. */
    void setDraggedChip (int sampleIndex)
    {
        for (size_t i = 0; i < chips.size(); ++i)
            chips[i]->setBeingDragged ((int) i == sampleIndex);
    }

    void resized() override
    {
        layoutChips (getWidth(), true);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff181b1f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (selected ? MinigunLookAndFeel::teal : juce::Colour (0xff262a30));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, selected ? 1.5f : 1.0f);

        auto badge = badgeBounds.toFloat();
        juce::ColourGradient grad (badgeTop, badge.getX(), badge.getY(), badgeBottom, badge.getX(), badge.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (badgeTextFor (badgeTop));
        g.setFont (MinigunLookAndFeel::sansFont (12.0f, true));
        g.drawText ("L" + juce::String (layerIndex + 1), badgeBounds, juce::Justification::centred);

        // Delete button (x) in the top-right corner, same style as the chip remove button.
        auto xBox = deleteButtonBounds().toFloat();
        g.setColour (deleteHover ? MinigunLookAndFeel::amber : MinigunLookAndFeel::label);
        auto xArea = xBox.reduced (6.0f);
        g.drawLine (xArea.getX(), xArea.getY(), xArea.getRight(), xArea.getBottom(), 1.6f);
        g.drawLine (xArea.getRight(), xArea.getY(), xArea.getX(), xArea.getBottom(), 1.6f);
    }

    juce::Rectangle<int> deleteButtonBounds() const
    {
        return { getWidth() - 10 - 20, 13, 20, 20 };
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool over = deleteButtonBounds().contains (e.getPosition());
        if (over != deleteHover) { deleteHover = over; repaint (deleteButtonBounds()); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (deleteHover) { deleteHover = false; repaint (deleteButtonBounds()); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Delete layer");
            m.showMenuAsync (juce::PopupMenu::Options(), [this] (int result)
            {
                if (result == 1 && onDeleteRequested) onDeleteRequested();
            });
        }
        else if (deleteButtonBounds().contains (e.getPosition()))
        {
            if (onDeleteRequested) onDeleteRequested();
        }
        else
        {
            if (onSelect) onSelect();
        }
    }

private:
    juce::Rectangle<int> badgeBounds { 10, 10, 26, 26 };
    bool deleteHover = false;
    std::vector<std::unique_ptr<SampleChip>> chips;
    AddChip addChip;

    int layoutChips (int width, bool apply) const
    {
        const int pad = 10;
        const int gap = 6;
        const int badgeW = 26;
        const int rowH = 26;
        int x = pad + badgeW + 10;
        int y = pad;
        int startX = x;

        const int rightLimit = width - pad - 24; // keep the top-right delete button clear of chips

        auto place = [&] (int w)
        {
            if (x + w > rightLimit && x > startX)
            {
                x = startX;
                y += rowH + gap;
            }
            juce::Rectangle<int> r (x, y, w, rowH);
            x += w + gap;
            return r;
        };

        for (auto& c : chips)
        {
            auto r = place (juce::jmin (c->preferredWidth(), rightLimit - startX)); // long names are ellipsised, never overlap the x button
            if (apply) const_cast<SampleChip*> (c.get())->setBounds (r);
        }
        auto r = place (AddChip::preferredWidth);
        if (apply) const_cast<AddChip*> (&addChip)->setBounds (r);

        return y + rowH + pad;
    }
};

//==============================================================================
class LayerEditor::RowsContainer : public juce::Component
{
public:
    std::vector<std::unique_ptr<LayerRow>> rows;

    void rebuild (Pad& pad, int padIndex, int selectedLayer, int width,
                  std::function<void (int)> onSelect,
                  std::function<void (int)> onDelete,
                  std::function<void (int, int)> onRemoveSample,
                  std::function<void (int)> onAddFiles,
                  std::function<void (juce::File)> onPreview)
    {
        rows.clear();
        int n = (int) pad.layers.size();
        int y = 0;
        for (int i = n - 1; i >= 0; --i) // top layer first
        {
            auto row = std::make_unique<LayerRow>();
            row->layerIndex = i;
            row->padIndex = padIndex;
            layerRampColours (i, n, row->badgeTop, row->badgeBottom);
            row->selected = (i == selectedLayer);
            row->onSelect = [onSelect, i] { onSelect (i); };
            row->onDeleteRequested = [onDelete, i] { onDelete (i); };
            row->onPreviewFile = [onPreview] (juce::File f) { onPreview (f); };
            row->onAddFiles = [onAddFiles, i] { onAddFiles (i); };
            row->removeSampleAt = [onRemoveSample, i] (int sampleIdx) { onRemoveSample (i, sampleIdx); };
            row->setSamples (pad.layers[(size_t) i].samples);

            int h = row->computeHeightForWidth (width);
            addAndMakeVisible (*row);
            row->setBounds (0, y, width, h);
            y += h + 8;
            rows.push_back (std::move (row));
        }
        setSize (width, juce::jmax (y, 1));
    }
};

//==============================================================================
LayerEditor::LayerEditor (MinigunAudioProcessor& processorIn) : processor (processorIn)
{
    setPaintingIsUnclipped (true);

    addLayerButton.onClick = [this] { addNewLayer(); };
    addAndMakeVisible (addLayerButton);

    removeLayerButton.setTooltip ("Delete the selected layer (its velocity range goes to the neighbour)");
    removeLayerButton.onClick = [this]
    {
        if (! currentPad().layers.empty())
            deleteLayer (selectedLayer);
    };
    addAndMakeVisible (removeLayerButton);

    rowsContainer = std::make_unique<RowsContainer>();
    rowsViewport.setViewedComponent (rowsContainer.get(), false);
    rowsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (rowsViewport);
}

LayerEditor::~LayerEditor() = default;

Pad& LayerEditor::currentPad()
{
    return processor.getKit().pads[(size_t) processor.getSelectedPad()];
}

void LayerEditor::writeAndNotify (std::function<void()> mutator)
{
    mutator();
    processor.kitEdited();
}

void LayerEditor::addNewLayer()
{
    writeAndNotify ([this]
    {
        auto& pad = currentPad();
        int idx = minigun::layers::addLayerOnTop (pad);
        if (idx >= 0) selectedLayer = idx;
    });
    refresh();
}

void LayerEditor::deleteLayer (int layerIndex)
{
    writeAndNotify ([this, layerIndex]
    {
        minigun::layers::removeLayer (currentPad(), layerIndex);
    });
    refresh();
}

void LayerEditor::selectLayer (int layerIndex)
{
    selectedLayer = layerIndex;
    refresh();
}

void LayerEditor::refresh()
{
    auto& pad = currentPad();
    int n = (int) pad.layers.size();
    if (n == 0) selectedLayer = 0;
    else selectedLayer = juce::jlimit (0, n - 1, selectedLayer < 0 ? n - 1 : selectedLayer);

    removeLayerButton.setEnabled (n > 0);
    removeLayerButton.setAlpha (n > 0 ? 1.0f : 0.35f);

    int width = rowsViewport.getWidth() > 0 ? rowsViewport.getMaximumVisibleWidth() : getWidth() - 8;
    width = juce::jmax (width, 100);

    rowsContainer->rebuild (pad, processor.getSelectedPad(), selectedLayer, width,
        [this] (int i) { selectLayer (i); },
        [this] (int i) { deleteLayer (i); },
        [this] (int layerIdx, int sampleIdx)
        {
            writeAndNotify ([this, layerIdx, sampleIdx]
            {
                auto& p = currentPad();
                if (layerIdx >= 0 && layerIdx < (int) p.layers.size())
                {
                    auto& samples = p.layers[(size_t) layerIdx].samples;
                    if (sampleIdx >= 0 && sampleIdx < (int) samples.size())
                        samples.erase (samples.begin() + sampleIdx);
                }
            });
            refresh();
        },
        [this] (int i)
        {
            selectedLayer = i;
            auto chooser = std::make_shared<juce::FileChooser> ("Add samples", juce::File(),
                "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.wma");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems,
                [this, i, chooser] (const juce::FileChooser& fc)
                {
                    auto files = fc.getResults();
                    if (files.isEmpty()) return;
                    writeAndNotify ([this, i, &files]
                    {
                        auto& pad2 = currentPad();
                        if (i >= 0 && i < (int) pad2.layers.size())
                            for (auto& f : files)
                                pad2.layers[(size_t) i].samples.push_back ({ f, false });
                    });
                    refresh();
                });
        },
        [this] (juce::File f) { processor.previewFile (f); });

    repaint();
}

void LayerEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (MinigunLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (MinigunLookAndFeel::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto content = getLocalBounds().reduced (14);
    auto headerRow = content.removeFromTop (24);
    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::labelFont (11.0f));
    g.drawText ("VELOCITY LAYERS", headerRow, juce::Justification::centredLeft);

    auto& pad = processor.getKit().pads[(size_t) processor.getSelectedPad()];
    g.setColour (MinigunLookAndFeel::teal);
    g.setFont (MinigunLookAndFeel::monoFont (11.0f, true));
    g.drawText (juce::String ((int) pad.layers.size()) + " / 8", headerRow, juce::Justification::centredRight);

    // Range bar.
    g.setColour (MinigunLookAndFeel::lcdBg);
    g.fillRoundedRectangle (rangeBarBounds.toFloat(), 5.0f);
    g.setColour (MinigunLookAndFeel::lcdBorder);
    g.drawRoundedRectangle (rangeBarBounds.toFloat().reduced (0.5f), 5.0f, 1.0f);

    int n = (int) pad.layers.size();
    juce::Path clip;
    clip.addRoundedRectangle (rangeBarBounds.toFloat(), 5.0f);
    g.saveState();
    g.reduceClipRegion (rangeBarBounds);

    for (int i = 0; i < n; ++i)
    {
        auto& l = pad.layers[(size_t) i];
        auto seg = segmentBoundsForLayer (i, n);

        juce::Colour top, bottom;
        layerRampColours (i, n, top, bottom);
        juce::ColourGradient grad (top, 0.0f, (float) seg.getY(), bottom, 0.0f, (float) seg.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRect (seg);

        g.setColour (badgeTextFor (top));
        g.setFont (MinigunLookAndFeel::monoFont (11.0f, true));
        g.drawText ("L" + juce::String (i + 1) + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (l.lo) + juce::String::fromUTF8 ("\xE2\x80\x93") + juce::String (l.hi),
                   seg, juce::Justification::centred);
    }

    g.restoreState();

    // Dividers.
    for (int i = 0; i < n - 1; ++i)
    {
        int x = (int) velocityToX (pad.layers[(size_t) i].hi) + 1;
        bool hot = (i == draggingDivider || i == hoverDivider);
        g.setColour (hot ? MinigunLookAndFeel::teal.brighter (0.3f) : MinigunLookAndFeel::teal);
        g.fillRect (x - 1, rangeBarBounds.getY(), 3, rangeBarBounds.getHeight());
    }

    auto scaleRow = juce::Rectangle<int> (rangeBarBounds.getX(), rangeBarBounds.getBottom() + 2, rangeBarBounds.getWidth(), 14);
    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::monoFont (10.0f));
    g.drawText ("1", scaleRow, juce::Justification::centredLeft);
    g.drawText ("127", scaleRow, juce::Justification::centredRight);
    g.setFont (MinigunLookAndFeel::labelFont (10.0f));
    g.drawText ("DRAG DIVIDERS TO SET VELOCITY RANGES", scaleRow, juce::Justification::centred);
}

void LayerEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragActive) return;

    if (dragHighlightSpecific && ! dragHighlightBounds.isEmpty())
    {
        g.saveState();
        if (dragHighlightInRows)
            g.reduceClipRegion (rowsViewport.getBounds());

        juce::Path dashed;
        dashed.addRoundedRectangle (dragHighlightBounds.toFloat().reduced (1.0f), 6.0f);
        float dashLengths[] = { 4.0f, 3.0f };
        juce::Path stroked;
        juce::PathStrokeType (2.0f).createDashedStroke (stroked, dashed, dashLengths, 2);
        g.setColour (MinigunLookAndFeel::teal);
        g.fillPath (stroked);
        g.restoreState();
    }
    else
    {
        g.setColour (MinigunLookAndFeel::teal.withAlpha (0.4f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    // Chip drag: a caret showing exactly which slot the sample will land in.
    if (dragIsSample && dragInsertIndex >= 0 && ! dragCaretBounds.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion (rowsViewport.getBounds());
        g.setColour (MinigunLookAndFeel::teal);
        g.fillRoundedRectangle (dragCaretBounds.toFloat(), 1.5f);
        g.restoreState();
    }
}

void LayerEditor::resized()
{
    auto content = getLocalBounds().reduced (14);
    auto headerRow = content.removeFromTop (24);
    addLayerButton.setBounds (headerRow.removeFromRight (70).withHeight (24).withY (headerRow.getY()));
    headerRow.removeFromRight (6);
    removeLayerButton.setBounds (headerRow.removeFromRight (70).withHeight (24).withY (headerRow.getY()));

    content.removeFromTop (8);
    rangeBarBounds = content.removeFromTop (34);
    content.removeFromTop (16); // scale row

    content.removeFromTop (8);
    rowsViewport.setBounds (content);

    refresh();
}

float LayerEditor::velocityToX (int velocity) const
{
    float t = (float) (velocity - 1) / 126.0f;
    return (float) rangeBarBounds.getX() + t * (float) rangeBarBounds.getWidth();
}

int LayerEditor::xToVelocity (int x) const
{
    float t = (float) (x - rangeBarBounds.getX()) / (float) juce::jmax (1, rangeBarBounds.getWidth());
    return juce::jlimit (1, 127, 1 + (int) std::round (t * 126.0f));
}

int LayerEditor::findDividerNear (int x) const
{
    auto& pad = processor.getKit().pads[(size_t) processor.getSelectedPad()];
    int n = (int) pad.layers.size();
    for (int i = 0; i < n - 1; ++i)
    {
        int dx = (int) velocityToX (pad.layers[(size_t) i].hi) + 1;
        if (std::abs (dx - x) <= 6) return i;
    }
    return -1;
}

void LayerEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! rangeBarBounds.contains (e.getPosition()))
        return;

    draggingDivider = findDividerNear (e.x);

    // Click on a segment (not on a divider): audition that layer. The velocity comes from the
    // click position on the 1..127 scale, so the engine picks this layer and applies the pad's
    // round-robin / random rule exactly as a MIDI note would.
    if (draggingDivider < 0 && ! currentPad().layers.empty())
    {
        const int velocity = xToVelocity (e.x);
        if (onAudition) onAudition (velocity);
        else processor.triggerPad (processor.getSelectedPad(), velocity);
    }
}

void LayerEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingDivider >= 0)
    {
        int newHi = xToVelocity (e.x);
        writeAndNotify ([this, newHi] { minigun::layers::setDivider (currentPad(), draggingDivider, newHi); });
        repaint();
    }
}

void LayerEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingDivider >= 0)
    {
        draggingDivider = -1;
        refresh();
    }
}

void LayerEditor::mouseMove (const juce::MouseEvent& e)
{
    int newHover = rangeBarBounds.contains (e.getPosition()) ? findDividerNear (e.x) : -1;
    if (newHover != hoverDivider)
    {
        hoverDivider = newHover;
        repaint();
    }
}

juce::Rectangle<int> LayerEditor::segmentBoundsForLayer (int index, int total) const
{
    auto& pad = processor.getKit().pads[(size_t) processor.getSelectedPad()];
    auto& l = pad.layers[(size_t) index];
    int x0 = (int) velocityToX (l.lo);
    int x1 = (int) velocityToX (l.hi + 1);
    if (index == 0) x0 = rangeBarBounds.getX();
    if (index == total - 1) x1 = rangeBarBounds.getRight();
    return { x0, rangeBarBounds.getY(), juce::jmax (1, x1 - x0), rangeBarBounds.getHeight() };
}

//==============================================================================
// Drag & drop.

void LayerEditor::updateDragTarget (juce::Point<int> localPos, bool sampleDrag)
{
    dragActive = true;

    bool specific = false;
    bool inRows = false;
    int target = -1;
    int insertIndex = -1;
    juce::Rectangle<int> highlight;
    juce::Rectangle<int> caret;

    // 1) Over a layer row (rows live inside rowsViewport / rowsContainer).
    if (rowsViewport.getBounds().contains (localPos))
    {
        auto posInRows = rowsContainer->getLocalPoint (this, localPos);
        for (auto& row : rowsContainer->rows)
        {
            if (row->getBounds().contains (posInRows))
            {
                target = row->layerIndex;
                specific = true;
                inRows = true;
                highlight = getLocalArea (rowsContainer.get(), row->getBounds());

                // A chip drag also picks the slot it would land in, so a sample can be reordered
                // inside one layer, not just moved between layers.
                if (sampleDrag)
                {
                    insertIndex = row->insertIndexForLocalPos (row->getLocalPoint (this, localPos));
                    caret = getLocalArea (row.get(), row->caretBoundsForIndex (insertIndex));
                }
                break;
            }
        }
    }

    // 2) Over a range-bar segment.
    if (! specific && rangeBarBounds.contains (localPos))
    {
        auto& pad = processor.getKit().pads[(size_t) processor.getSelectedPad()];
        int n = (int) pad.layers.size();
        for (int i = 0; i < n; ++i)
        {
            auto seg = segmentBoundsForLayer (i, n);
            if (seg.contains (localPos))
            {
                target = i;
                specific = true;
                inRows = false;
                highlight = seg;
                break;
            }
        }
    }

    // 3) Elsewhere: the currently selected layer (-1 if the pad has no layers).
    if (! specific)
    {
        auto& pad = processor.getKit().pads[(size_t) processor.getSelectedPad()];
        target = pad.layers.empty() ? -1 : selectedLayer;
    }

    bool changed = (dragTargetLayer != target) || (dragHighlightSpecific != specific)
                 || (dragHighlightInRows != inRows) || (dragHighlightBounds != highlight)
                 || (dragIsSample != sampleDrag) || (dragInsertIndex != insertIndex)
                 || (dragCaretBounds != caret);

    dragTargetLayer = target;
    dragHighlightSpecific = specific;
    dragHighlightInRows = inRows;
    dragHighlightBounds = highlight;
    dragIsSample = sampleDrag;
    dragInsertIndex = insertIndex;
    dragCaretBounds = caret;

    if (changed) repaint();
}

void LayerEditor::clearDragTarget()
{
    if (dragActive)
    {
        dragActive = false;
        dragTargetLayer = -1;
        dragHighlightSpecific = false;
        dragHighlightInRows = false;
        dragHighlightBounds = {};
        dragIsSample = false;
        dragInsertIndex = -1;
        dragCaretBounds = {};
        repaint();
    }
}

void LayerEditor::handleDroppedFiles (const juce::Array<juce::File>& files, juce::Point<int> localPos)
{
    updateDragTarget (localPos, false);
    int target = dragTargetLayer;
    clearDragTarget();

    if (files.isEmpty()) return;
    if (onFilesDroppedOnLayer) onFilesDroppedOnLayer (target, files);
}

bool LayerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (PadComponent::hasAudioExtension (f)) return true;
    return false;
}

void LayerEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    updateDragTarget ({ x, y }, false);
}

void LayerEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    updateDragTarget ({ x, y }, false);
}

void LayerEditor::fileDragExit (const juce::StringArray&)
{
    clearDragTarget();
}

void LayerEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::Array<juce::File> audioFiles;
    for (auto& f : files)
        if (PadComponent::hasAudioExtension (f)) audioFiles.add (juce::File (f));
    handleDroppedFiles (audioFiles, { x, y });
}

bool LayerEditor::isInterestedInDragSource (const SourceDetails& details)
{
    int padIdx = 0, layerIdx = 0, sampleIdx = 0;
    if (sampleDragFromDescription (details.description, padIdx, layerIdx, sampleIdx))
        return padIdx == processor.getSelectedPad(); // the panel only ever shows one pad's layers

    return PadComponent::filesFromDragDescription (details.description).size() > 0;
}

static bool isSampleDrag (const juce::var& description)
{
    int a = 0, b = 0, c = 0;
    return LayerEditor::sampleDragFromDescription (description, a, b, c);
}

void LayerEditor::itemDragEnter (const SourceDetails& details)
{
    updateDragTarget (details.localPosition, isSampleDrag (details.description));
}

void LayerEditor::itemDragMove (const SourceDetails& details)
{
    updateDragTarget (details.localPosition, isSampleDrag (details.description));
}

void LayerEditor::itemDragExit (const SourceDetails&)
{
    clearDragTarget();
}

void LayerEditor::itemDropped (const SourceDetails& details)
{
    int padIdx = 0, srcLayer = 0, srcIndex = 0;
    if (sampleDragFromDescription (details.description, padIdx, srcLayer, srcIndex))
    {
        updateDragTarget (details.localPosition, true);
        const int dstLayer = dragTargetLayer;
        const int dstIndex = dragInsertIndex;
        clearDragTarget();

        if (padIdx == processor.getSelectedPad())
            moveSample (srcLayer, srcIndex, dstLayer, dstIndex,
                        juce::ModifierKeys::currentModifiers.isCtrlDown()); // Ctrl held = copy
        else
            clearSampleDragState();
        return;
    }

    auto files = PadComponent::filesFromDragDescription (details.description);
    handleDroppedFiles (files, details.localPosition);
}

//==============================================================================
juce::var LayerEditor::makeSampleDragDescription (int padIndex, int layerIndex, int sampleIndex)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("minigunSamplePad", padIndex);
    obj->setProperty ("minigunSampleLayer", layerIndex);
    obj->setProperty ("minigunSampleIndex", sampleIndex);
    return juce::var (obj);
}

bool LayerEditor::sampleDragFromDescription (const juce::var& description,
                                             int& padIndex, int& layerIndex, int& sampleIndex)
{
    auto* obj = description.getDynamicObject();
    if (obj == nullptr || ! obj->hasProperty ("minigunSamplePad")) return false;

    padIndex = (int) obj->getProperty ("minigunSamplePad");
    layerIndex = (int) obj->getProperty ("minigunSampleLayer");
    sampleIndex = (int) obj->getProperty ("minigunSampleIndex");
    return true;
}

void LayerEditor::clearSampleDragState()
{
    clearDragTarget();
    for (auto& row : rowsContainer->rows)
        row->setDraggedChip (-1);
}

void LayerEditor::moveSample (int srcLayer, int srcIndex, int dstLayer, int dstIndex, bool copy)
{
    auto& pad = currentPad();
    const int numLayers = (int) pad.layers.size();
    if (srcLayer < 0 || srcLayer >= numLayers || dstLayer < 0 || dstLayer >= numLayers)
    {
        clearSampleDragState();
        return;
    }

    auto& src = pad.layers[(size_t) srcLayer].samples;
    if (srcIndex < 0 || srcIndex >= (int) src.size())
    {
        clearSampleDragState();
        return;
    }

    int insertAt = dstIndex >= 0 ? dstIndex : (int) pad.layers[(size_t) dstLayer].samples.size();

    // Dropping a chip back onto the slot it already occupies changes nothing: don't spend an
    // undo step (or a kit reload) on it.
    if (! copy && dstLayer == srcLayer && (insertAt == srcIndex || insertAt == srcIndex + 1))
    {
        selectLayer (dstLayer);
        return;
    }

    const auto ref = src[(size_t) srcIndex];

    writeAndNotify ([&]
    {
        if (! copy)
        {
            src.erase (src.begin() + srcIndex);
            if (dstLayer == srcLayer && insertAt > srcIndex) --insertAt;
        }

        auto& dst = pad.layers[(size_t) dstLayer].samples;
        insertAt = juce::jlimit (0, (int) dst.size(), insertAt);
        dst.insert (dst.begin() + insertAt, ref);
    });

    selectedLayer = dstLayer;
    refresh(); // rebuilds the rows, so the dimmed source chip goes with them
}

} // namespace minigun
