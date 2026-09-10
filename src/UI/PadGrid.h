// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include "PadComponent.h"
#include <array>
#include <memory>

namespace minigun
{
struct Kit;

/** 4x4 pad grid in MPC order (pad 0 bottom-left ... pad 15 top-right) plus the caption
    row shown below it in the mockup. */
class PadGrid : public juce::Component
{
public:
    PadGrid();
    ~PadGrid() override = default;

    /** Refreshes all 16 pads from the model. */
    void refresh (const Kit& kit, int selectedPad);

    void setPadGlow (int padIndex, float glow);
    /** Sets the colour the pad glows in (the velocity layer's colour); pass -1 layer for the default amber. */
    void setPadGlowLayer (int padIndex, int layerIndex, int layerCount);

    std::function<void (int padIndex, int velocity)> onTrigger;
    std::function<void (int padIndex)> onSelect;
    std::function<void (int padIndex, juce::Array<juce::File>)> onFilesDropped;
    /** A pad was dragged onto another pad; PluginEditor asks for confirmation and moves it. */
    std::function<void (int sourcePadIndex, int targetPadIndex)> onPadMoveRequested;

    /** Clears the "picked up" dimming on every pad; call when a pad drag ends, dropped or not. */
    void clearPadDragState();

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kCellSize = 118 + 2 * PadComponent::kInset; // 128: pad body 118 + outline margin
    static constexpr int kGap = 2; // visual gap stays 12 (2 + 2 * kInset)

private:
    std::array<std::unique_ptr<PadComponent>, 16> pads;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};

} // namespace minigun
