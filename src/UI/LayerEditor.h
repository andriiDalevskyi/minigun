// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class MinigunAudioProcessor;

namespace minigun
{
struct Pad;

/** Velocity-layer editor: range bar with draggable dividers, "+ Layer", and per-layer
    rows (top layer first) with sample chips. Tracks a selected layer for the browser's
    "Add to Lx" workflow. Also accepts drag & drop of audio files, either from the OS
    (Explorer) or from BrowserPanel's internal drag, targeting whichever layer row or
    range-bar segment is under the mouse (or the selected layer elsewhere in the panel). */
class LayerEditor : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    public juce::DragAndDropTarget
{
public:
    explicit LayerEditor (MinigunAudioProcessor& processor);
    ~LayerEditor() override;

    /** Re-reads the selected pad from the processor and rebuilds everything. */
    void refresh();

    /** Call when the SELECTED PAD (not just its layers) changes, so the selection
        resets to that pad's top layer instead of carrying over the old index. */
    void notifyPadChanged() { selectedLayer = -1; refresh(); }

    int getSelectedLayer() const noexcept { return selectedLayer; }

    /** Fired when the user clicks a range-bar segment: audition the selected pad at this velocity
        (the editor triggers the pad and lights it in the layer's colour). */
    std::function<void (int velocity)> onAudition;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // juce::FileDragAndDropTarget (Explorer drags)
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // juce::DragAndDropTarget (internal drags from BrowserPanel)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Called with the resolved target layer and the (already audio-extension-filtered)
        dropped files, whether the drag came from Explorer or from BrowserPanel. */
    std::function<void (int layerIndex, juce::Array<juce::File>)> onFilesDroppedOnLayer;

private:
    MinigunAudioProcessor& processor;

    juce::TextButton addLayerButton { "+ Layer" };
    juce::Viewport rowsViewport;

    class RowsContainer;
    std::unique_ptr<RowsContainer> rowsContainer;

    juce::Rectangle<int> rangeBarBounds;
    int draggingDivider = -1;
    int hoverDivider = -1;
    int selectedLayer = 0;

    bool dragActive = false;
    int dragTargetLayer = -1;
    bool dragHighlightSpecific = false; // true: draw dashed outline around dragHighlightBounds; false: faint panel border
    bool dragHighlightInRows = false;   // true: clip dragHighlightBounds painting to rowsViewport's bounds
    juce::Rectangle<int> dragHighlightBounds;

    void writeAndNotify (std::function<void()> mutator);
    Pad& currentPad();
    int findDividerNear (int x) const;
    float velocityToX (int velocity) const;
    int xToVelocity (int x) const;
    void addNewLayer();
    void deleteLayer (int layerIndex);
    void selectLayer (int layerIndex);

    /** Resolves the layer that a drag/drop at this component's local (x, y) should target:
        a layer row -> that layer; a range-bar segment -> that layer; otherwise -> the
        currently selected layer (-1 if the pad has no layers at all). */
    void updateDragTarget (juce::Point<int> localPos);
    void clearDragTarget();
    void handleDroppedFiles (const juce::Array<juce::File>& files, juce::Point<int> localPos);
    juce::Rectangle<int> segmentBoundsForLayer (int index, int total) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerEditor)
};

} // namespace minigun
