#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace minigun
{

/** Delivers the mouse "extra" buttons (XBUTTON1 = Back, XBUTTON2 = Forward) that JUCE does not
    report through MouseEvent. Windows only: subclasses the native HWND of the component's peer and
    listens for WM_XBUTTONDOWN. On other platforms it does nothing.

    Usage: keep one instance per component, call attach() once the component is on screen
    (e.g. from parentHierarchyChanged()); the callback receives the button (1 = back, 2 = forward)
    and the position in the *owner component's* coordinates. Runs on the message thread. */
class WinMouseXButtons
{
public:
    explicit WinMouseXButtons (juce::Component& owner);
    ~WinMouseXButtons();

    std::function<void (int button, juce::Point<int> posInOwner)> onXButton;

    /** (Re)attaches to the owner's current peer. Safe to call repeatedly. */
    void attach();
    void detach();

    /** Internal: called by the native hook with the peer-client position (physical pixels). */
    void onXButtonFromPeer (int button, juce::Point<int> peerClientPos);

private:
    juce::Component& owner;
    void* attachedHwnd = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WinMouseXButtons)
};

} // namespace minigun
