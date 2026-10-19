#include "console_client.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <httplib.h>

namespace launcher
{
    namespace
    {
        // A status query is answered from memory; anything slower than this
        // is a server in trouble, and the caller simply asks again.
        constexpr auto  kConnectTimeoutSeconds = 1;
        constexpr auto  kReadTimeoutSeconds = 2;

        constexpr auto  kPollInterval = std::chrono::milliseconds( 100);
    }

    auto queryStatus( unsigned short port) -> ConsoleStatus
    {
        httplib::Client  client( "127.0.0.1", port);
        client.set_connection_timeout( kConnectTimeoutSeconds);
        client.set_read_timeout( kReadTimeoutSeconds);

        const auto  response = client.Get( "/api/status");

        ConsoleStatus  status;
        if ( !response)
        {
            // Refused or unreachable is nobody listening; anything else
            // happened on a connection that was made.
            status.Server = response.error() == httplib::Error::Connection
                                ? ConsoleStatus::State::Gone
                                : ConsoleStatus::State::Hung;
            return status;
        }
        if ( response->status != 200)
        {
            status.Server = ConsoleStatus::State::Hung;
            return status;
        }

        // The body is one flat object thorium_webui writes itself --
        // {"viewers":N,"running":bool} -- so finding two keys is all the
        // parsing it needs.
        const auto &  body = response->body;
        const auto    viewers = body.find( "\"viewers\":");
        const auto    running = body.find( "\"running\":");
        if ( viewers == std::string::npos || running == std::string::npos)
        {
            status.Server = ConsoleStatus::State::Hung;
            return status;
        }

        status.Server = ConsoleStatus::State::Up;
        status.Viewers = std::atoi( body.c_str() + viewers + 10);
        status.Running = body.compare( running + 10, 4, "true") == 0;
        return status;
    }

    auto waitForServer( unsigned short port, unsigned timeoutMs) -> bool
    {
        const auto  deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs);

        do
        {
            if ( queryStatus( port).Server == ConsoleStatus::State::Up)
            {
                return true;
            }
            std::this_thread::sleep_for( kPollInterval);
        }
        while ( std::chrono::steady_clock::now() < deadline);

        return false;
    }
}
