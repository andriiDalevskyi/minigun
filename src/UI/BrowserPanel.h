// Minigun drum sampler - Copyright (C) 2026 Andrii Dalevskyi (Dallas Audio)
// SPDX-License-Identifier: AGPL-3.0-or-later
// This file is part of Minigun, released under the GNU AGPL v3.0 or later; see LICENSE.

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "WinMouseXButtons.h"
#include "ClickFocusTextEditor.h"
#include <functional>
#include <memory>
#include <vector>

class MinigunAudioProcessor;

namespace minigun
{

/** Sample browser: folder navigation, preview toggle, waveform + info footer, and
    drag sources for dropping files onto pads. */
class BrowserPanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    explicit BrowserPanel (MinigunAudioProcessor& processor);
    ~BrowserPanel() override;

    /** Called by the editor when the selected pad/layer changes, to label the
        "Add to Lx" button. layerNumberOneBased may be 0 if there is no layer yet. */
    void setCurrentLayerNumber (int layerNumberOneBased);

    /** Fires with a single file: from a double-click on an audio file, or the
        "Add to Lx" button. */
    std::function<void (juce::Array<juce::File>)> onAddToLayer;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void focusOfChildComponentChanged (FocusChangeType) override { repaint(); } // teal frame around the path LCD while editing
    bool keyPressed (const juce::KeyPress&) override;   // Backspace = up, Alt+Left/Right = back/forward
    void parentHierarchyChanged() override;             // (re)attach the mouse X-button hook to the window

    /** Goes to the parent folder, or to the drives list when already at a drive root. */
    void goUp();
    /** Navigation history like a file manager: mouse Back/Forward buttons, Alt+Left/Right. */
    void goBack();
    void goForward();
    bool isAtDrivesLevel() const noexcept { return currentDir == juce::File(); }

private:
    MinigunAudioProcessor& processor;

    struct Entry
    {
        juce::File file;
        bool isDir = false;
        bool isUp = false;
    };
    std::vector<Entry> entries;
    juce::File currentDir;
    juce::File selectedFile;
    int currentLayerNumber = 1;

    std::vector<juce::File> history;   // visited folders (invalid File = drives level)
    int historyPos = -1;
    bool navigatingHistory = false;    // true while goBack/goForward apply a folder
    WinMouseXButtons xButtons { *this };

    std::unique_ptr<juce::PropertiesFile> properties;

    juce::ToggleButton previewToggle;
    juce::Label previewLabel { {}, "Preview" };
    juce::Rectangle<int> upArrowBounds;
    juce::Rectangle<int> pathLcdBounds;
    ClickFocusTextEditor pathEditor;   // editable path shown inside the LCD; Enter navigates, Esc reverts

    class ListModel;
    std::unique_ptr<ListModel> listModel;
    juce::ListBox listBox;

    juce::TextButton addToLayerButton { "Add to L1" };
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail;
    juce::String infoLine;

    void setCurrentDirectory (const juce::File& dir);
    void refreshEntries();
    void updatePathText();   // shows the current folder (or "Computer") in the path editor
    void applyTypedPath();   // navigates to the folder/file typed into the path editor
    void rowClicked (int row);
    void rowDoubleClicked (int row);
    void selectFile (const juce::File& f);
    void addFileToLayer (const juce::File& f);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    static bool hasAudioExtension (const juce::File& f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};

} // namespace minigun
