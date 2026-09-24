#pragma once

#include <optional>
#include <string>

namespace launcher
{
    //
    // Looks for Chrome the same way Windows Explorer does: the "App Paths"
    // registry key a Chrome installer writes, checked per-user first and then
    // per-machine. Deliberately not a hunt through Program Files -- App Paths
    // is the one place that stays correct after Google changes the install
    // layout again, which it has done before.
    //
    [[nodiscard]]
    auto findChrome() -> std::optional<std::wstring>;

    //
    // Builds the command line for one console window: --app= so there is no
    // tab strip, no address bar and no bookmarks bar, but still a real,
    // resizable, closable window -- not a kiosk lock and not an embedded
    // webview control. See framework/ui/README.md Sec.5 for why an embedded
    // webview is the trap this avoids on purpose.
    //
    // A dedicated --user-data-dir keeps the console's window state -- and its
    // complete lack of the operator's own extensions and bookmarks -- separate
    // from whatever Chrome profile the operator otherwise uses on that PC.
    //
    [[nodiscard]]
    auto buildAppModeCommandLine(
        const std::wstring & chromePath,
        const std::wstring & url,
        const std::wstring & userDataDir) -> std::wstring;
}
