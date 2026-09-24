#include "browser_launch.hpp"

#include <windows.h>

namespace launcher
{
    namespace
    {
        constexpr auto kAppPathsKey =
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe";

        auto readAppPath( HKEY root) -> std::optional<std::wstring>
        {
            wchar_t  buffer[ MAX_PATH]{};
            DWORD    size = sizeof( buffer);

            const auto result = RegGetValueW(
                root, kAppPathsKey, nullptr,
                RRF_RT_REG_SZ, nullptr, buffer, &size);

            if ( result != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            return std::wstring( buffer);
        }
    }

    auto findChrome() -> std::optional<std::wstring>
    {
        if ( auto path = readAppPath( HKEY_CURRENT_USER))
        {
            return path;
        }
        return readAppPath( HKEY_LOCAL_MACHINE);
    }

    auto buildAppModeCommandLine(
        const std::wstring & chromePath,
        const std::wstring & url,
        const std::wstring & userDataDir) -> std::wstring
    {
        return L"\"" + chromePath + L"\""
             + L" --app=" + url
             + L" --user-data-dir=\"" + userDataDir + L"\""
             + L" --window-size=1024,768"
             + L" --no-first-run"
             + L" --no-default-browser-check";
    }
}
