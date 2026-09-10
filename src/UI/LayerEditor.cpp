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

    std::function<void()> onPreviewClick;
    std::function<void()> onRemoveClick;

    int preferredWidth() const
    {
        auto f = MinigunLookAndFeel::monoFont (11.0f);
        return juce::GlyphArrangement::getStringWidthInt (f, file.getFileName()) + 16 + 18;
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (MinigunLookAndFeel::segBg);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0xff33373e));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        auto textArea = getLocalBounds().reduced (8, 0);
        textArea.removeFromRight (16);
        g.setColour (MinigunLookAndFeel::text);
        g.setFont (MinigunLookAndFeel::monoFont (11.0f));
        g.drawText (file.getFileName(), textArea, juce::Justification::centredLeft, true);

        auto xArea = getLocalBounds().removeFromRight (18).toFloat().reduced (6.0f);
        g.setColour (MinigunLookAndFeel::label);
        g.drawLine (xArea.getX(), xArea.getY(), xArea.getRight(), xArea.getBottom(), 1.6f);
        g.drawLine (xArea.getRight(), xArea.getY(), xArea.getX(), xArea.getBottom(), 1.6f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto xHit = getLocalBounds().removeFromRight (18);
        if (xHit.contains (e.getPosition())) { if (onRemoveClick) onRemoveClick(); }
        else                                  { if (onPreviewClick) onPreviewClick(); }
    }

private:
    juce::File file;
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
        else
        {
            if (onSelect) onSelect();
        }
    }

private:
    juce::Rectangle<int> badgeBounds { 10, 10, 26, 26 };
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

        auto place = [&] (int w)
        {
            if (x + w > width - pad && x > startX)
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
            auto r = place (c->preferredWidth());
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

    void rebuild (Pad& pad, int selectedLayer, int width,
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

    int width = rowsViewport.getWidth() > 0 ? rowsViewport.getMaximumVisibleWidth() : getWidth() - 8;
    width = juce::jmax (width, 100);

    rowsContainer->rebuild (pad, selectedLayer, width,
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

    // Drag & drop highlight.
    if (dragActive)
    {
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
    }
}

void LayerEditor::resized()
{
    auto content = getLocalBounds().reduced (14);
    auto headerRow = content.removeFromTop (24);
    addLayerButton.setBounds (headerRow.removeFromRight (70).withHeight (24).withY (headerRow.getY()));

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

void LayerEditor::updateDragTarget (juce::Point<int> localPos)
{
    dragActive = true;

    bool specific = false;
    bool inRows = false;
    int target = -1;
    juce::Rectangle<int> highlight;

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
                 || (dragHighlightInRows != inRows) || (dragHighlightBounds != highlight);

    dragTargetLayer = target;
    dragHighlightSpecific = specific;
    dragHighlightInRows = inRows;
    dragHighlightBounds = highlight;

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
        repaint();
    }
}

void LayerEditor::handleDroppedFiles (const juce::Array<juce::File>& files, juce::Point<int> localPos)
{
    updateDragTarget (localPos);
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
    updateDragTarget ({ x, y });
}

void LayerEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    updateDragTarget ({ x, y });
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
    return PadComponent::filesFromDragDescription (details.description).size() > 0;
}

void LayerEditor::itemDragEnter (const SourceDetails& details)
{
    updateDragTarget (details.localPosition);
}

void LayerEditor::itemDragMove (const SourceDetails& details)
{
    updateDragTarget (details.localPosition);
}

void LayerEditor::itemDragExit (const SourceDetails&)
{
    clearDragTarget();
}

void LayerEditor::itemDropped (const SourceDetails& details)
{
    auto files = PadComponent::filesFromDragDescription (details.description);
    handleDroppedFiles (files, details.localPosition);
}

} // namespace minigun
