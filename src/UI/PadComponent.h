// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace minigun
{

/** MIDI note number -> short name, e.g. 36 -> "C1", 49 -> "C#2" (Ableton/Logic convention
    where note 60 = C3). Shared by PadComponent and PadEditorPanel. */
inline juce::String noteToShortName (int note)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int n = juce::jlimit (0, 127, note);
    int octave = n / 12 - 2;
    return juce::String (names[n % 12]) + juce::String (octave);
}

/** One 118x118 drum pad. Shows number, note, name, an LED strip for layer count, and
    reacts to click (velocity from Y) and file drag/drop (both OS files and internal
    drags originating from BrowserPanel). */
class PadComponent : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::DragAndDropTarget
{
public:
    /** Margin inside the component so the selection outline and hit glow are never clipped. */
    static constexpr int kInset = 5;

public:
    explicit PadComponent (int padIndex);
    ~PadComponent() override = default;

    void setPadData (const juce::String& name, int note, int layerCount, bool empty);
    void setSelected (bool shouldBeSelected);
    void setGlow (float amount); // 0..1, 1 = full hit brightness
    /** Colour the glow takes at full brightness (defaults to the amber hit colour). Set before
        setGlow() when a hit arrives so the pad lights up in its velocity layer's colour. */
    void setGlowColours (juce::Colour top, juce::Colour bottom);

    int getPadIndex() const noexcept { return padIndex; }
    float getGlow() const noexcept { return glow; }

    std::function<void (int padIndex, int velocity)> onTrigger;
    std::function<void (int padIndex)> onSelect;
    std::function<void (int padIndex, juce::Array<juce::File>)> onFilesDropped;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    // juce::DragAndDropTarget (internal drags from BrowserPanel)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Description format used both here and in BrowserPanel's startDragging(): a juce::var
        holding an Array<var> of absolute file path strings. */
    static juce::var makeDragDescription (const juce::Array<juce::File>& files);
    static juce::Array<juce::File> filesFromDragDescription (const juce::var& description);

    /** True if the path has a recognised audio file extension (wav aif aiff flac ogg mp3 wma). */
    static bool hasAudioExtension (const juce::String& path);

private:
    int padIndex;
    juce::String padName { "Empty" };
    int note = 36;
    int layerCount = 0;
    bool isEmpty = true;
    bool selected = false;
    bool dragOver = false;
    float glow = 0.0f;
    juce::Colour glowTop { 0xffffd08a }, glowBottom { 0xfff2a33a }; // MinigunLookAndFeel::padHitTop/Bottom

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadComponent)
};

} // namespace minigun
