#include "tray.hpp"

#include <utility>

namespace launcher
{
    namespace
    {
        constexpr UINT   kTrayMessage = WM_APP + 1;
        constexpr UINT   kIdShow      = 1;
        constexpr UINT   kIdSafe      = 2;
        constexpr UINT   kIdQuit      = 3;
        constexpr auto   kWindowClass = L"ThoriumLauncherTrayWindow";
    }

    TrayIcon::TrayIcon( std::wstring tooltip, Handlers handlers)
        : mHandlers( std::move( handlers))
    {
        WNDCLASSW  wc{};
        wc.lpfnWndProc = &TrayIcon::windowProc;
        wc.hInstance = GetModuleHandleW( nullptr);
        wc.lpszClassName = kWindowClass;
        RegisterClassW( &wc);

        // HWND_MESSAGE: this window is never shown and never needs to be --
        // its only job is to exist as a target for the tray callback message
        // and for WM_COMMAND from the popup menu.
        mWindow = CreateWindowExW(
            0, kWindowClass, L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

        SetWindowLongPtrW( mWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( this));

        mIconData.cbSize = sizeof( mIconData);
        mIconData.hWnd = mWindow;
        mIconData.uID = 1;
        mIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        mIconData.uCallbackMessage = kTrayMessage;
        mIconData.hIcon = LoadIconW( nullptr, IDI_APPLICATION);

        lstrcpynW(
            mIconData.szTip, tooltip.c_str(),
            static_cast<int>( ARRAYSIZE( mIconData.szTip)));

        Shell_NotifyIconW( NIM_ADD, &mIconData);
    }

    TrayIcon::~TrayIcon()
    {
        Shell_NotifyIconW( NIM_DELETE, &mIconData);
        if ( mWindow != nullptr)
        {
            DestroyWindow( mWindow);
        }
        UnregisterClassW( kWindowClass, GetModuleHandleW( nullptr));
    }

    auto TrayIcon::run() -> int
    {
        MSG  msg{};
        while ( GetMessageW( &msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage( &msg);
            DispatchMessageW( &msg);
        }
        return static_cast<int>( msg.wParam);
    }

    auto TrayIcon::postQuit() const -> void
    {
        PostMessageW( mWindow, WM_COMMAND, MAKEWPARAM( kIdQuit, 0), 0);
    }

    auto CALLBACK TrayIcon::windowProc(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
    {
        auto * self = reinterpret_cast<TrayIcon *>(
            GetWindowLongPtrW( window, GWLP_USERDATA));

        if ( self != nullptr)
        {
            if ( message == kTrayMessage)
            {
                self->handleTrayMessage( lParam);
                return 0;
            }

            if ( message == WM_COMMAND)
            {
                switch ( LOWORD( wParam))
                {
                    case kIdShow:
                        if ( self->mHandlers.OnShowConsole) self->mHandlers.OnShowConsole();
                        return 0;
                    case kIdSafe:
                        if ( self->mHandlers.OnSafeTheRig) self->mHandlers.OnSafeTheRig();
                        return 0;
                    case kIdQuit:
                        if ( self->mHandlers.OnQuit) self->mHandlers.OnQuit();
                        return 0;
                    default:
                        break;
                }
            }
        }

        return DefWindowProcW( window, message, wParam, lParam);
    }

    auto TrayIcon::handleTrayMessage( LPARAM lParam) -> void
    {
        switch ( static_cast<UINT>( lParam))
        {
            case WM_LBUTTONUP:
                if ( mHandlers.OnShowConsole) mHandlers.OnShowConsole();
                break;
            case WM_RBUTTONUP:
                showContextMenu();
                break;
            default:
                break;
        }
    }

    auto TrayIcon::showContextMenu() -> void
    {
        const auto  menu = CreatePopupMenu();
        AppendMenuW( menu, MF_STRING, kIdShow, L"Show console");
        AppendMenuW( menu, MF_STRING, kIdSafe, L"Safe the rig");
        AppendMenuW( menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW( menu, MF_STRING, kIdQuit, L"Quit");

        POINT  cursor{};
        GetCursorPos( &cursor);

        // Required so the menu dismisses itself on a click elsewhere -- a
        // message-only window is never foreground, so Windows never sends it
        // WM_ACTIVATE on its own to trigger that.
        SetForegroundWindow( mWindow);

        TrackPopupMenu(
            menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, mWindow, nullptr);

        DestroyMenu( menu);
    }
}
