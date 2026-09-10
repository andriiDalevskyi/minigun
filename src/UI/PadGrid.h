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

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kCellSize = 118 + 2 * PadComponent::kInset; // 128: pad body 118 + outline margin
    static constexpr int kGap = 2; // visual gap stays 12 (2 + 2 * kInset)

private:
    std::array<std::unique_ptr<PadComponent>, 16> pads;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};

} // namespace minigun
