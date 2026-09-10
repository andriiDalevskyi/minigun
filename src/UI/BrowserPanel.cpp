// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#include "BrowserPanel.h"
#include "MinigunLookAndFeel.h"
#include "PadComponent.h"
#include "../PluginProcessor.h"

namespace minigun
{

namespace
{
    juce::Path folderPath (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addRoundedRectangle (r.getX(), r.getY() + r.getHeight() * 0.18f, r.getWidth(), r.getHeight() * 0.7f, 1.5f);
        juce::Path tab;
        tab.addRoundedRectangle (r.getX(), r.getY(), r.getWidth() * 0.45f, r.getHeight() * 0.32f, 1.2f);
        p.addPath (tab);
        return p;
    }

    juce::Path waveformPath (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getCentreY());
        p.lineTo (r.getX() + r.getWidth() * 0.15f, r.getY());
        p.lineTo (r.getX() + r.getWidth() * 0.35f, r.getBottom());
        p.lineTo (r.getX() + r.getWidth() * 0.55f, r.getY() + r.getHeight() * 0.25f);
        p.lineTo (r.getX() + r.getWidth() * 0.72f, r.getBottom() * 0.9f - r.getY() * 0.1f);
        p.lineTo (r.getRight(), r.getCentreY());
        return p;
    }

    juce::Path playTriangle (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
        return p;
    }
}

//==============================================================================
class BrowserPanel::ListModel : public juce::ListBoxModel
{
public:
    explicit ListModel (BrowserPanel& ownerIn) : owner (ownerIn) {}

    int getNumRows() override { return (int) owner.entries.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= (int) owner.entries.size()) return;
        auto& e = owner.entries[(size_t) rowNumber];
        bool playing = ! e.isDir && ! e.isUp && e.file == owner.selectedFile;

        if (playing)
        {
            g.setColour (juce::Colour (0xff23282a));
            g.fillRect (0, 0, width, height);
        }

        // A clicked folder (or "..") gets a teal frame so the user sees what a double-click will open.
        if (rowIsSelected && (e.isDir || e.isUp))
        {
            g.setColour (MinigunLookAndFeel::teal.withAlpha (0.12f));
            g.fillRoundedRectangle (juce::Rectangle<float> (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f), 3.0f);
            g.setColour (MinigunLookAndFeel::teal);
            g.drawRoundedRectangle (juce::Rectangle<float> (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f), 3.0f, 1.5f);
        }

        auto iconArea = juce::Rectangle<float> (8.0f, height * 0.5f - 7.0f, 14.0f, 14.0f);
        if (e.isUp || e.isDir)
        {
            g.setColour (MinigunLookAndFeel::label);
            g.fillPath (folderPath (iconArea));
        }
        else if (playing)
        {
            g.setColour (MinigunLookAndFeel::amber);
            g.fillPath (playTriangle (iconArea));
        }
        else
        {
            g.setColour (juce::Colour (0xff6b6f77));
            g.strokePath (waveformPath (iconArea), juce::PathStrokeType (1.4f));
        }

        g.setColour (playing ? MinigunLookAndFeel::amber
                    : (e.isDir || e.isUp) ? MinigunLookAndFeel::text
                                          : juce::Colour (0xffb9bcc3));
        g.setFont (MinigunLookAndFeel::monoFont (12.0f));
        auto textArea = juce::Rectangle<int> (28, 0, width - 34, height);
        g.drawText (e.isUp ? juce::String ("..") : (e.file.getFileName().isEmpty() ? e.file.getFullPathName() : e.file.getFileName()), textArea, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { owner.rowClicked (row); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { owner.rowDoubleClicked (row); }
    void returnKeyPressed (int row) override { owner.rowDoubleClicked (row); } // Enter opens a folder / adds a file

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.size() != 1) return {};
        int row = rows[0];
        if (row < 0 || row >= (int) owner.entries.size()) return {};
        auto& e = owner.entries[(size_t) row];
        if (e.isDir || e.isUp) return {};
        return PadComponent::makeDragDescription ({ e.file });
    }

private:
    BrowserPanel& owner;
};

//==============================================================================
BrowserPanel::BrowserPanel (MinigunAudioProcessor& processorIn)
    : processor (processorIn), thumbnail (512, processorIn.getFormatManager(), thumbnailCache)
{
    setPaintingIsUnclipped (true);

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Minigun";
    opts.filenameSuffix = "settings";
    opts.folderName = "Minigun";
    opts.osxLibrarySubFolder = "Application Support";
    properties = std::make_unique<juce::PropertiesFile> (opts);

    auto lastDir = properties->getValue ("lastBrowserDir");
    currentDir = (lastDir.isNotEmpty() && juce::File (lastDir).isDirectory())
                     ? juce::File (lastDir)
                     : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    history.push_back (currentDir); // the start folder is the first history entry so Back can return to it
    historyPos = 0;

    previewToggle.setToggleState (true, juce::dontSendNotification);
    previewToggle.onClick = [this] { if (! previewToggle.getToggleState()) processor.stopPreview(); };
    addAndMakeVisible (previewToggle);

    previewLabel.setFont (MinigunLookAndFeel::labelFont (10.0f));
    previewLabel.setColour (juce::Label::textColourId, MinigunLookAndFeel::label);
    previewLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (previewLabel);

    listModel = std::make_unique<ListModel> (*this);
    listBox.setModel (listModel.get());
    listBox.setRowHeight (22);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (listBox);

    // Editable path inside the LCD: the LCD frame and up-arrow are painted by this component,
    // so the editor itself is transparent.
    pathEditor.setMultiLine (false);
    pathEditor.setReturnKeyStartsNewLine (false);
    pathEditor.setScrollbarsShown (false);
    pathEditor.setSelectAllWhenFocused (true);
    pathEditor.setFont (MinigunLookAndFeel::monoFont (11.0f));
    pathEditor.setIndents (4, 0);
    pathEditor.setJustification (juce::Justification::centredLeft);
    pathEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    pathEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    pathEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    pathEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffb9bcc3));
    pathEditor.setColour (juce::TextEditor::highlightColourId, MinigunLookAndFeel::teal.withAlpha (0.35f));
    pathEditor.setColour (juce::TextEditor::highlightedTextColourId, MinigunLookAndFeel::text);
    pathEditor.setColour (juce::CaretComponent::caretColourId, MinigunLookAndFeel::amber);
    pathEditor.onReturnKey = [this] { applyTypedPath(); };
    pathEditor.onEscapeKey = [this] { updatePathText(); giveAwayKeyboardFocus(); };
    pathEditor.onFocusLost = [this] { updatePathText(); repaint(); };
    addAndMakeVisible (pathEditor);

    MinigunLookAndFeel::setAccentButton (addToLayerButton, true);
    addToLayerButton.onClick = [this] { addFileToLayer (selectedFile); };
    addAndMakeVisible (addToLayerButton);

    thumbnail.addChangeListener (this);

    refreshEntries();
}

BrowserPanel::~BrowserPanel()
{
    thumbnail.removeChangeListener (this);
}

bool BrowserPanel::hasAudioExtension (const juce::File& f)
{
    static const char* exts[] = { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".wma" };
    auto lower = f.getFullPathName().toLowerCase();
    for (auto* e : exts)
        if (lower.endsWith (e)) return true;
    return false;
}

void BrowserPanel::setCurrentDirectory (const juce::File& dir)
{
    if (! navigatingHistory && (history.empty() || history[(size_t) juce::jmax (0, historyPos)] != dir))
    {
        // Drop the "forward" part, append the new folder, cap the size.
        if (historyPos >= 0 && historyPos + 1 < (int) history.size())
            history.erase (history.begin() + historyPos + 1, history.end());
        history.push_back (dir);
        if (history.size() > 100) history.erase (history.begin());
        historyPos = (int) history.size() - 1;
    }

    currentDir = dir;
    properties->setValue ("lastBrowserDir", currentDir.getFullPathName());
    properties->saveIfNeeded();
    refreshEntries();
}

void BrowserPanel::goBack()
{
    if (historyPos <= 0) return;
    navigatingHistory = true;
    --historyPos;
    setCurrentDirectory (history[(size_t) historyPos]);
    navigatingHistory = false;
}

void BrowserPanel::goForward()
{
    if (historyPos + 1 >= (int) history.size()) return;
    navigatingHistory = true;
    ++historyPos;
    setCurrentDirectory (history[(size_t) historyPos]);
    navigatingHistory = false;
}

bool BrowserPanel::keyPressed (const juce::KeyPress& key)
{
    if (pathEditor.hasKeyboardFocus (true))
        return false; // typing a path: leave Backspace etc. to the editor

    if (key == juce::KeyPress::backspaceKey)                                { goUp();      return true; }
    if (key == juce::KeyPress (juce::KeyPress::leftKey,  juce::ModifierKeys::altModifier, 0)) { goBack();    return true; }
    if (key == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)) { goForward(); return true; }
    return false;
}

void BrowserPanel::parentHierarchyChanged()
{
    xButtons.attach();
    xButtons.onXButton = [this] (int button, juce::Point<int> pos)
    {
        if (! getLocalBounds().contains (pos)) return; // only when the mouse is over the browser
        if (button == 1) goBack();
        else if (button == 2) goForward();
    };
}

void BrowserPanel::goUp()
{
    if (isAtDrivesLevel()) return;
    if (currentDir.getParentDirectory() == currentDir) setCurrentDirectory (juce::File()); // drive root -> drives list
    else setCurrentDirectory (currentDir.getParentDirectory());
}

void BrowserPanel::refreshEntries()
{
    entries.clear();

    juce::Array<juce::File> dirs, files;

    if (isAtDrivesLevel())
    {
        juce::File::findFileSystemRoots (dirs);
    }
    else
    {
        Entry up;
        up.isUp = true;
        entries.push_back (up);

        for (const auto& f : juce::RangedDirectoryIterator (currentDir, false, "*", juce::File::findDirectories))
            dirs.add (f.getFile());
        for (const auto& f : juce::RangedDirectoryIterator (currentDir, false, "*", juce::File::findFiles))
            if (hasAudioExtension (f.getFile())) files.add (f.getFile());

        dirs.sort();
        files.sort();
    }

    for (auto& d : dirs)
    {
        Entry e; e.file = d; e.isDir = true;
        entries.push_back (e);
    }
    for (auto& f : files)
    {
        Entry e; e.file = f;
        entries.push_back (e);
    }

    listBox.deselectAllRows(); // a stale selection index would frame an unrelated row in the new folder
    listBox.updateContent();
    listBox.repaint();
    updatePathText();
    repaint();
}

void BrowserPanel::updatePathText()
{
    pathEditor.setText (isAtDrivesLevel() ? juce::String ("Computer")
                                          : currentDir.getFullPathName().replaceCharacter ('\\', '/'),
                        juce::dontSendNotification);
    pathEditor.setCaretPosition (pathEditor.getTotalNumChars());
}

void BrowserPanel::applyTypedPath()
{
    auto text = pathEditor.getText().trim().unquoted().trim();

    if (text.isEmpty() || text.equalsIgnoreCase ("Computer"))
    {
        setCurrentDirectory (juce::File());
    }
    else if (juce::File::isAbsolutePath (text))
    {
        juce::File f (text);
        if (f.isDirectory())
        {
            setCurrentDirectory (f);
        }
        else if (f.existsAsFile())
        {
            // A file path: open its folder and select/preview the file.
            setCurrentDirectory (f.getParentDirectory());
            for (int i = 0; i < (int) entries.size(); ++i)
                if (entries[(size_t) i].file == f)
                {
                    listBox.selectRow (i);
                    rowClicked (i);
                    break;
                }
        }
        else
        {
            updatePathText(); // unknown path: revert
        }
    }
    else
    {
        updatePathText();
    }

    giveAwayKeyboardFocus();
}

void BrowserPanel::rowClicked (int row)
{
    if (row < 0 || row >= (int) entries.size()) return;
    auto& e = entries[(size_t) row];
    if (e.isDir || e.isUp) return;
    selectFile (e.file);
}

void BrowserPanel::rowDoubleClicked (int row)
{
    if (row < 0 || row >= (int) entries.size()) return;
    auto& e = entries[(size_t) row];
    if (e.isUp) { goUp(); return; }
    if (e.isDir) { setCurrentDirectory (e.file); return; }
    addFileToLayer (e.file);
}

void BrowserPanel::selectFile (const juce::File& f)
{
    selectedFile = f;
    listBox.repaint();

    if (previewToggle.getToggleState())
        processor.previewFile (f);

    thumbnail.setSource (new juce::FileInputSource (f));

    infoLine = {};
    if (auto* reader = processor.getFormatManager().createReaderFor (f))
    {
        double seconds = reader->lengthInSamples / juce::jmax (1.0, reader->sampleRate);
        infoLine = juce::String (reader->sampleRate / 1000.0, 1) + juce::String::fromUTF8 ("k \xC2\xB7 ")
                 + juce::String ((int) reader->bitsPerSample) + juce::String::fromUTF8 ("b \xC2\xB7 ")
                 + juce::String (seconds, 2) + " s";
        delete reader;
    }

    repaint();
}

void BrowserPanel::addFileToLayer (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    if (onAddToLayer) onAddToLayer ({ f });
}

void BrowserPanel::setCurrentLayerNumber (int layerNumberOneBased)
{
    currentLayerNumber = juce::jmax (1, layerNumberOneBased);
    addToLayerButton.setButtonText ("Add to L" + juce::String (currentLayerNumber));
}

void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void BrowserPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (MinigunLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (MinigunLookAndFeel::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Header label.
    g.setColour (MinigunLookAndFeel::label);
    g.setFont (MinigunLookAndFeel::labelFont (11.0f));
    g.drawText ("BROWSER", juce::Rectangle<int> (12, 12, 100, 16), juce::Justification::centredLeft);

    // Path LCD.
    g.setColour (MinigunLookAndFeel::lcdBg);
    g.fillRoundedRectangle (pathLcdBounds.toFloat(), 4.0f);
    g.setColour (MinigunLookAndFeel::lcdBorder);
    g.drawRoundedRectangle (pathLcdBounds.toFloat().reduced (0.5f), 4.0f, 1.0f);

    juce::Path chevron;
    auto c = upArrowBounds.toFloat().reduced (5.0f);
    chevron.startNewSubPath (c.getRight(), c.getY());
    chevron.lineTo (c.getX(), c.getCentreY());
    chevron.lineTo (c.getRight(), c.getBottom());
    g.setColour (juce::Colour (0xff6b6f77));
    g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Path text is shown by pathEditor (editable). Teal frame while the user is typing a path.
    if (pathEditor.hasKeyboardFocus (true))
    {
        g.setColour (MinigunLookAndFeel::teal);
        g.drawRoundedRectangle (pathLcdBounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    // Footer.
    auto footer = getLocalBounds().removeFromBottom (76).reduced (12, 10);
    auto waveArea = footer.removeFromTop (34);
    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour (MinigunLookAndFeel::amber);
        thumbnail.drawChannels (g, waveArea, 0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (MinigunLookAndFeel::lcdBorder);
        g.drawHorizontalLine (waveArea.getCentreY(), (float) waveArea.getX(), (float) waveArea.getRight());
    }

    footer.removeFromTop (6);
    auto infoRow = footer.removeFromTop (18);
    g.setColour (juce::Colour (0xff6b6f77));
    g.setFont (MinigunLookAndFeel::monoFont (10.0f));
    g.drawText (infoLine, infoRow.removeFromLeft (infoRow.getWidth() - 90), juce::Justification::centredLeft);
}

void BrowserPanel::mouseDown (const juce::MouseEvent& e)
{
    if (upArrowBounds.contains (e.getPosition()))
        goUp();
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (56).reduced (12, 8);
    header.removeFromTop (18);
    auto toggleRow = header;
    previewToggle.setBounds (toggleRow.removeFromRight (34));
    previewLabel.setBounds (toggleRow.removeFromRight (54));

    auto pathRow = area.removeFromTop (8 + 26 + 8);
    pathRow.removeFromTop (8);
    pathLcdBounds = pathRow.reduced (12, 0).withHeight (26);
    upArrowBounds = pathLcdBounds.removeFromLeft (24);
    pathLcdBounds = pathRow.reduced (12, 0).withHeight (26);
    pathEditor.setBounds (pathLcdBounds.withTrimmedLeft (24).withTrimmedRight (4).reduced (0, 2));

    area.removeFromTop (2);
    auto footerArea = area.removeFromBottom (76);
    listBox.setBounds (area.reduced (8, 4));

    addToLayerButton.setBounds (footerArea.reduced (12, 10).removeFromBottom (18).removeFromRight (90));
}

} // namespace minigun
