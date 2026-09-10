#include "WinMouseXButtons.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <commctrl.h>
 #include <windowsx.h>
 #pragma comment (lib, "comctl32.lib")
 #include <map>
 #include <vector>
 #include <algorithm>
#endif

namespace minigun
{

#if JUCE_WINDOWS
namespace
{
    // One HWND may host several JUCE components that want X-button events (only the browser today,
    // but keep it general): a single subclass per HWND fans out to all registered hooks.
    struct HwndHooks
    {
        std::vector<WinMouseXButtons*> hooks;
    };

    std::map<HWND, HwndHooks>& registry()
    {
        static std::map<HWND, HwndHooks> r;
        return r;
    }

    constexpr UINT_PTR kSubclassId = 0x4D4E4758; // 'MNGX'

    LRESULT CALLBACK subclassProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        if (msg == WM_XBUTTONDOWN)
        {
            const int button = (int) GET_XBUTTON_WPARAM (wParam); // XBUTTON1 = 1 (back), XBUTTON2 = 2 (forward)
            const juce::Point<int> clientPos (GET_X_LPARAM (lParam), GET_Y_LPARAM (lParam));

            auto it = registry().find (hwnd);
            if (it != registry().end())
            {
                auto hooksCopy = it->second.hooks; // callbacks may detach
                for (auto* h : hooksCopy)
                    h->onXButtonFromPeer (button, clientPos);
            }
            return TRUE; // handled: stops DefWindowProc from turning it into WM_APPCOMMAND for the host
        }

        if (msg == WM_XBUTTONUP)
            return TRUE;

        return DefSubclassProc (hwnd, msg, wParam, lParam);
    }
}
#endif

//==============================================================================
WinMouseXButtons::WinMouseXButtons (juce::Component& ownerIn) : owner (ownerIn) {}

WinMouseXButtons::~WinMouseXButtons()
{
    detach();
}

void WinMouseXButtons::attach()
{
   #if JUCE_WINDOWS
    auto* peer = owner.getPeer();
    auto hwnd = peer != nullptr ? (HWND) peer->getNativeHandle() : nullptr;

    if (hwnd == (HWND) attachedHwnd)
        return;

    detach();
    if (hwnd == nullptr)
        return;

    auto& entry = registry()[hwnd];
    if (entry.hooks.empty())
        SetWindowSubclass (hwnd, subclassProc, kSubclassId, 0);
    entry.hooks.push_back (this);
    attachedHwnd = hwnd;
   #endif
}

void WinMouseXButtons::detach()
{
   #if JUCE_WINDOWS
    if (attachedHwnd == nullptr)
        return;

    auto hwnd = (HWND) attachedHwnd;
    attachedHwnd = nullptr;

    auto it = registry().find (hwnd);
    if (it == registry().end())
        return;

    auto& hooks = it->second.hooks;
    hooks.erase (std::remove (hooks.begin(), hooks.end(), this), hooks.end());
    if (hooks.empty())
    {
        RemoveWindowSubclass (hwnd, subclassProc, kSubclassId);
        registry().erase (it);
    }
   #endif
}

void WinMouseXButtons::onXButtonFromPeer (int button, juce::Point<int> peerClientPos)
{
    if (onXButton == nullptr)
        return;

    // The peer's client area is the top-level component's local space (physical pixels →
    // logical via the peer's scale factor).
    if (auto* peer = owner.getPeer())
    {
        auto logical = peerClientPos.toFloat() / (float) peer->getPlatformScaleFactor();
        auto* top = owner.getTopLevelComponent();
        auto inOwner = owner.getLocalPoint (top, logical.roundToInt());
        onXButton (button, inOwner);
    }
}

} // namespace minigun
