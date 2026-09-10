// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "PadGrid.h"
#include "MinigunLookAndFeel.h"
#include "../Model/KitModel.h"

namespace minigun
{

PadGrid::PadGrid()
{
    for (int i = 0; i < 16; ++i)
    {
        auto pad = std::make_unique<PadComponent> (i);
        pad->onTrigger = [this] (int idx, int vel) { if (onTrigger) onTrigger (idx, vel); };
        pad->onSelect  = [this] (int idx) { if (onSelect) onSelect (idx); };
        pad->onFilesDropped = [this] (int idx, juce::Array<juce::File> files)
        {
            if (onFilesDropped) onFilesDropped (idx, files);
        };
        pad->onPadDroppedOnPad = [this] (int from, int to)
        {
            if (onPadMoveRequested) onPadMoveRequested (from, to);
        };
        addAndMakeVisible (*pad);
        pads[(size_t) i] = std::move (pad);
    }
}

void PadGrid::refresh (const Kit& kit, int selectedPad)
{
    for (int i = 0; i < 16; ++i)
    {
        auto& p = kit.pads[(size_t) i];
        pads[(size_t) i]->setPadData (p.name, p.note, (int) p.layers.size(), p.isEmpty());
        pads[(size_t) i]->setSelected (i == selectedPad);
    }
}

void PadGrid::clearPadDragState()
{
    for (auto& p : pads)
        p->setDragSourceHighlight (false);
}

void PadGrid::setPadGlow (int padIndex, float glow)
{
    if (padIndex >= 0 && padIndex < 16)
        pads[(size_t) padIndex]->setGlow (glow);
}

void PadGrid::setPadGlowLayer (int padIndex, int layerIndex, int layerCount)
{
    if (padIndex < 0 || padIndex >= 16) return;

    juce::Colour top = MinigunLookAndFeel::padHitTop, bottom = MinigunLookAndFeel::padHitBottom;
    if (layerIndex >= 0 && layerCount > 0)
    {
        MinigunLookAndFeel::layerColours (layerIndex, layerCount, top, bottom);
        top = top.brighter (0.25f); // the rubber pad face reads lighter than the flat bar segment
    }
    pads[(size_t) padIndex]->setGlowColours (top, bottom);
}

void PadGrid::resized()
{
    auto area = getLocalBounds();
    auto gridArea = area.removeFromTop (kCellSize * 4 + kGap * 3);

    for (int padIndex = 0; padIndex < 16; ++padIndex)
    {
        int displayRow = 3 - (padIndex / 4); // 0 = top row
        int displayCol = padIndex % 4;
        int x = displayCol * (kCellSize + kGap);
        int y = displayRow * (kCellSize + kGap);
        pads[(size_t) padIndex]->setBounds (gridArea.getX() + x, gridArea.getY() + y, kCellSize, kCellSize);
    }
}

void PadGrid::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    area.removeFromTop (kCellSize * 4 + kGap * 3);
    auto captionRow = area.reduced (4, 0);
    captionRow.removeFromTop (juce::jmax (0, (area.getHeight() - 14) / 2));

    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::labelFont (11.0f));
    g.drawText (juce::String::fromUTF8 ("LIT = PLAYING \xC2\xB7 OUTLINE = SELECTED \xC2\xB7 LEDS = LAYERS"),
               captionRow, juce::Justification::centredLeft);

    g.setColour (MinigunLookAndFeel::teal);
    g.setFont (MinigunLookAndFeel::monoFont (11.0f, true));
    g.drawText ("DROP AUDIO ON A PAD TO ASSIGN", captionRow, juce::Justification::centredRight);
}

} // namespace minigun
