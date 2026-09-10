#include "PadComponent.h"
#include "MinigunLookAndFeel.h"

namespace minigun
{

PadComponent::PadComponent (int padIndexIn) : padIndex (padIndexIn)
{
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (true, false);
}

void PadComponent::setPadData (const juce::String& nameIn, int noteIn, int layerCountIn, bool emptyIn)
{
    padName = nameIn.isEmpty() ? "Empty" : nameIn;
    note = noteIn;
    layerCount = juce::jlimit (0, 8, layerCountIn);
    isEmpty = emptyIn;
    repaint();
}

void PadComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void PadComponent::setGlowColours (juce::Colour top, juce::Colour bottom)
{
    if (glowTop != top || glowBottom != bottom)
    {
        glowTop = top;
        glowBottom = bottom;
        if (glow > 0.01f) repaint();
    }
}

void PadComponent::setGlow (float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (! juce::approximatelyEqual (glow, amount))
    {
        glow = amount;
        repaint();
    }
}

void PadComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (kInset);
    const float cornerRadius = 10.0f;

    juce::Colour top, bottom;
    if (dragOver)
    {
        top = MinigunLookAndFeel::padDropTop;
        bottom = MinigunLookAndFeel::padDropBottom;
    }
    else
    {
        juce::Colour baseTop = isEmpty ? MinigunLookAndFeel::padEmptyTop : MinigunLookAndFeel::padRubberTop;
        juce::Colour baseBottom = isEmpty ? MinigunLookAndFeel::padEmptyBottom : MinigunLookAndFeel::padRubberBottom;
        top = baseTop.interpolatedWith (glowTop, glow);
        bottom = baseBottom.interpolatedWith (glowBottom, glow);
    }

    // Glow shadow when hit.
    if (glow > 0.01f && ! dragOver)
    {
        juce::DropShadow shadow (glowBottom.withAlpha (0.75f * glow), (int) (28.0f * glow), { 0, 0 });
        juce::Path padShape;
        padShape.addRoundedRectangle (bounds, cornerRadius);
        shadow.drawForPath (g, padShape);
    }
    else
    {
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.55f), 6, { 0, 3 });
        juce::Path padShape;
        padShape.addRoundedRectangle (bounds, cornerRadius);
        shadow.drawForPath (g, padShape);
    }

    juce::ColourGradient grad (top, bounds.getX(), bounds.getY(), bottom, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bounds, cornerRadius);

    // Inner highlight/shadow strokes (approximating the CSS inset box-shadows).
    {
        juce::Path inner;
        inner.addRoundedRectangle (bounds.reduced (0.75f), cornerRadius);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (inner, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 0.6f));
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.strokePath (inner, juce::PathStrokeType (1.5f), juce::AffineTransform::translation (0.0f, -1.2f));
    }

    if (dragOver)
    {
        juce::Path dashed;
        auto dropBounds = bounds.reduced (6.0f);
        dashed.addRoundedRectangle (dropBounds, cornerRadius);
        float dashLengths[] = { 4.0f, 3.0f };
        juce::Path strokedDashed;
        juce::PathStrokeType (2.0f).createDashedStroke (strokedDashed, dashed, dashLengths, 2);
        g.setColour (MinigunLookAndFeel::teal);
        g.fillPath (strokedDashed);
    }

    // Text/number colours depend on state.
    bool hot = glow > 0.5f;
    juce::Colour numCol = hot ? juce::Colour (0xff6b4612) : juce::Colour (0xff9a9ea6);
    juce::Colour noteCol = hot ? juce::Colour (0xff6b4612) : juce::Colour (0xff5c6068);
    juce::Colour nameCol = dragOver ? juce::Colour (0xff0f5e5b)
                          : hot     ? juce::Colour (0xff2a1a04)
                          : isEmpty ? juce::Colour (0xff8c9097)
                                    : juce::Colour (0xff2a2c30);

    auto content = bounds.reduced (10.0f);

    // Top row: number (left), note (right).
    g.setColour (numCol);
    g.setFont (MinigunLookAndFeel::sansFont (11.0f, true));
    g.drawText (juce::String (padIndex + 1), content.removeFromTop (14.0f).withTrimmedRight (60.0f),
               juce::Justification::topLeft);

    g.setColour (noteCol);
    g.setFont (MinigunLookAndFeel::monoFont (11.0f));
    auto noteText = noteToShortName (note) + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (note);
    g.drawText (noteText, bounds.reduced (10.0f).removeFromTop (14.0f), juce::Justification::topRight);

    // LED strip (loaded pads only).
    if (! isEmpty && ! dragOver)
    {
        const int maxLeds = 8;
        const float ledW = 10.0f, ledH = 4.0f, gap = 3.0f;
        float totalW = maxLeds * ledW + (maxLeds - 1) * gap;
        float startX = bounds.getX() + 10.0f;
        float ledY = bounds.getBottom() - 10.0f - 15.0f - ledH; // above the name
        for (int i = 0; i < maxLeds; ++i)
        {
            bool on = i < layerCount;
            g.setColour (on ? MinigunLookAndFeel::teal : juce::Colour (0xffa9a59e));
            g.fillRoundedRectangle (startX + i * (ledW + gap), ledY, ledW, ledH, 1.0f);
        }
        juce::ignoreUnused (totalW);
    }

    // Name, bottom-left.
    auto nameArea = bounds.reduced (10.0f);
    nameArea = nameArea.removeFromBottom (18.0f);
    g.setColour (nameCol);
    if (isEmpty)
    {
        g.setFont (MinigunLookAndFeel::sansFont (12.0f, true));
        g.drawText (dragOver ? "Drop here" : padName.toUpperCase(), nameArea, juce::Justification::centredLeft);
    }
    else
    {
        g.setFont (MinigunLookAndFeel::sansFont (15.0f, true));
        g.drawText (padName.toUpperCase(), nameArea, juce::Justification::centredLeft);
    }

    if (selected)
    {
        auto outline = bounds.expanded (3.0f);
        g.setColour (MinigunLookAndFeel::teal);
        g.drawRoundedRectangle (outline, cornerRadius + 2.0f, 2.0f);
    }
}

void PadComponent::mouseDown (const juce::MouseEvent& e)
{
    float t = juce::jlimit (0.0f, 1.0f, (float) e.y / (float) juce::jmax (1, getHeight()));
    int velocity = juce::jlimit (1, 127, (int) juce::jmap (t, 0.0f, 1.0f, 127.0f, 1.0f));

    setGlow ((float) velocity / 127.0f);
    if (onSelect) onSelect (padIndex);
    if (onTrigger) onTrigger (padIndex, velocity);
}

bool PadComponent::hasAudioExtension (const juce::String& path)
{
    static const char* exts[] = { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".wma" };
    auto lower = path.toLowerCase();
    for (auto* e : exts)
        if (lower.endsWith (e)) return true;
    return false;
}

bool PadComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (hasAudioExtension (f)) return true;
    return false;
}

void PadComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void PadComponent::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void PadComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    juce::Array<juce::File> audioFiles;
    for (auto& f : files)
        if (hasAudioExtension (f)) audioFiles.add (juce::File (f));

    if (! audioFiles.isEmpty() && onFilesDropped)
        onFilesDropped (padIndex, audioFiles);

    repaint();
}

bool PadComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return filesFromDragDescription (details.description).size() > 0;
}

void PadComponent::itemDragEnter (const SourceDetails&)
{
    dragOver = true;
    repaint();
}

void PadComponent::itemDragExit (const SourceDetails&)
{
    dragOver = false;
    repaint();
}

void PadComponent::itemDropped (const SourceDetails& details)
{
    dragOver = false;
    auto files = filesFromDragDescription (details.description);
    if (! files.isEmpty() && onFilesDropped)
        onFilesDropped (padIndex, files);
    repaint();
}

juce::var PadComponent::makeDragDescription (const juce::Array<juce::File>& files)
{
    juce::Array<juce::var> arr;
    for (auto& f : files)
        arr.add (f.getFullPathName());
    return juce::var (arr);
}

juce::Array<juce::File> PadComponent::filesFromDragDescription (const juce::var& description)
{
    juce::Array<juce::File> result;
    if (auto* arr = description.getArray())
    {
        for (auto& v : *arr)
        {
            auto s = v.toString();
            if (s.isNotEmpty() && hasAudioExtension (s))
                result.add (juce::File (s));
        }
    }
    return result;
}

} // namespace minigun
