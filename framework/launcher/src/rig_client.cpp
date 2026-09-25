#include "rig_client.hpp"

#include <cstdlib>
#include <string>

#include <windows.h>
#include <winhttp.h>

namespace launcher
{
    namespace
    {
        // Short for the status poll, which runs every second and must not
        // pile up behind a hung server; longer for the safe, which starts
        // run_scripts --safe and waits for it.
        constexpr int  kStatusTimeoutMs = 2000;
        constexpr int  kSafeTimeoutMs   = 15000;

        struct Response
        {
            DWORD        Status{ 0 };
            std::string  Body;
        };

        auto request( unsigned short port, const wchar_t * verb, const wchar_t * path, int timeoutMs)
            -> std::optional<Response>
        {
            const auto session = WinHttpOpen(
                L"ThoriumLauncher/1.0",
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if ( session == nullptr)
            {
                return std::nullopt;
            }

            WinHttpSetTimeouts( session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

            const auto connection = WinHttpConnect( session, L"127.0.0.1", port, 0);
            if ( connection == nullptr)
            {
                WinHttpCloseHandle( session);
                return std::nullopt;
            }

            const auto req = WinHttpOpenRequest(
                connection, verb, path,
                nullptr, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

            std::optional<Response>  result;
            if ( req != nullptr)
            {
                if ( WinHttpSendRequest(
                         req,
                         WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != 0
                  && WinHttpReceiveResponse( req, nullptr) != 0)
                {
                    Response  response;
                    DWORD     size = sizeof( response.Status);
                    WinHttpQueryHeaders(
                        req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &response.Status, &size,
                        WINHTTP_NO_HEADER_INDEX);

                    DWORD  available = 0;
                    while ( WinHttpQueryDataAvailable( req, &available) != 0 && available > 0)
                    {
                        std::string  chunk( available, '\0');
                        DWORD        read = 0;
                        if ( WinHttpReadData( req, chunk.data(), available, &read) == 0)
                        {
                            break;
                        }
                        response.Body.append( chunk, 0, read);
                    }

                    result = std::move( response);
                }

                WinHttpCloseHandle( req);
            }

            WinHttpCloseHandle( connection);
            WinHttpCloseHandle( session);
            return result;
        }
    }

    auto postSafeTheRig( unsigned short port) -> bool
    {
        const auto  response = request( port, L"POST", L"/safe", kSafeTimeoutMs);
        return response && response->Status >= 200 && response->Status < 300;
    }

    auto getConsoleStatus( unsigned short port) -> std::optional<ConsoleStatus>
    {
        const auto  response = request( port, L"GET", L"/api/status", kStatusTimeoutMs);
        if ( !response || response->Status != 200)
        {
            return std::nullopt;
        }

        // The body is one flat object thorium_webui writes itself --
        // {"viewers":N,"running":bool} -- so finding two keys is all the
        // parsing it needs, exactly as in macos/rig_client.cpp.
        const auto &  body = response->Body;
        const auto    viewers = body.find( "\"viewers\":");
        const auto    running = body.find( "\"running\":");
        if ( viewers == std::string::npos || running == std::string::npos)
        {
            return std::nullopt;
        }

        ConsoleStatus  status;
        status.Viewers = std::atoi( body.c_str() + viewers + 10);
        status.Running = body.compare( running + 10, 4, "true") == 0;
        return status;
    }
}
