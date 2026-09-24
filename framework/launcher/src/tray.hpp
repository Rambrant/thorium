#pragma once

#include <functional>
#include <string>

#include <windows.h>

#include <shellapi.h>

namespace launcher
{
    //
    // The launcher's only visible chrome once the console window is open: an
    // icon in the notification area with three actions. Owns the message
    // loop, since a Win32 tray icon needs a window to receive its callback
    // messages and that window needs somebody pumping GetMessage for it --
    // this class is that somebody, not a side effect of it.
    //
    class TrayIcon
    {
        public:
            struct Handlers
            {
                //
                // Braced rather than bare, matching ui::ChildProcess::Handlers
                // (framework/ui/src/app/process.hpp) and for the same reason:
                // every call site uses designated initialisers and names only
                // the handlers it cares about.
                //
                std::function<void()>  OnShowConsole{};
                std::function<void()>  OnSafeTheRig{};
                std::function<void()>  OnQuit{};
            };

            TrayIcon( std::wstring tooltip, Handlers handlers);
            ~TrayIcon();

            TrayIcon( const TrayIcon &) = delete;
            auto operator=( const TrayIcon &) -> TrayIcon & = delete;

            //
            // Runs the message loop, blocking until something (OnQuit's
            // handler, ordinarily) calls PostQuitMessage. Returns the
            // WM_QUIT exit code.
            //
            auto run() -> int;

        private:
            static auto CALLBACK windowProc(
                HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT;

            auto handleTrayMessage( LPARAM lParam) -> void;
            auto showContextMenu() -> void;

            Handlers          mHandlers;
            HWND              mWindow{ nullptr };
            NOTIFYICONDATAW   mIconData{};
    };
}
