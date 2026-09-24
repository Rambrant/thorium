#include "rig_client.hpp"

#include <windows.h>
#include <winhttp.h>

namespace launcher
{
    auto postSafeTheRig( unsigned short port) -> bool
    {
        const auto session = WinHttpOpen(
            L"ThoriumLauncher/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if ( session == nullptr)
        {
            return false;
        }

        const auto connection = WinHttpConnect( session, L"127.0.0.1", port, 0);
        if ( connection == nullptr)
        {
            WinHttpCloseHandle( session);
            return false;
        }

        const auto request = WinHttpOpenRequest(
            connection, L"POST", L"/safe",
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

        auto  ok = false;
        if ( request != nullptr)
        {
            ok = WinHttpSendRequest(
                     request,
                     WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != 0
              && WinHttpReceiveResponse( request, nullptr) != 0;

            WinHttpCloseHandle( request);
        }

        WinHttpCloseHandle( connection);
        WinHttpCloseHandle( session);
        return ok;
    }
}
