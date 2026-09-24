#include "rig_client.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace launcher
{
    namespace
    {
        // Long enough for run_scripts --safe to open every instrument on the
        // rig and put it in its safe state, which the server waits for before
        // it answers.
        constexpr auto  kSafeTimeoutSeconds = 60;

        // A status query is answered from memory; anything slower than this
        // is a server in trouble, and the poller simply asks again.
        constexpr auto  kStatusTimeoutSeconds = 2;

        class Socket
        {
            public:
                Socket() : mFd( socket( AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}
                ~Socket() { if ( mFd >= 0) { close( mFd); } }

                Socket( const Socket &) = delete;
                auto operator=( const Socket &) -> Socket & = delete;

                [[nodiscard]] auto fd() const -> int { return mFd; }

            private:
                int  mFd;
        };

        auto sendAll( int fd, std::string_view data) -> bool
        {
            while ( !data.empty())
            {
                const auto  n = send( fd, data.data(), data.size(), 0);
                if ( n <= 0)
                {
                    return false;
                }
                data.remove_prefix( static_cast<std::size_t>( n));
            }
            return true;
        }

        //
        // One request on a fresh connection, and everything the server
        // answered before it closed it (Connection: close) -- or nothing if
        // it could not be sent.
        //
        auto request( unsigned short port, const std::string & method, const std::string & path,
                      int timeoutSeconds) -> std::optional<std::string>
        {
            const Socket  sock;
            if ( sock.fd() < 0)
            {
                return std::nullopt;
            }

            // Both directions: a server that accepts and then never answers must
            // not hang this thread forever.
            const timeval  timeout{ timeoutSeconds, 0 };
            setsockopt( sock.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout));
            setsockopt( sock.fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof( timeout));

            // SO_NOSIGPIPE: a server that closes early must fail this send, not
            // kill the launcher with SIGPIPE.
            const int  on = 1;
            setsockopt( sock.fd(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof( on));

            sockaddr_in  addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons( port);
            inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr);

            if ( connect( sock.fd(), reinterpret_cast<const sockaddr *>( &addr), sizeof( addr)) != 0)
            {
                return std::nullopt;
            }

            const std::string  text =
                method + " " + path + " HTTP/1.1\r\n"
                "Host: 127.0.0.1:" + std::to_string( port) + "\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";
            if ( !sendAll( sock.fd(), text))
            {
                return std::nullopt;
            }

            std::string  response;
            char         buffer[ 1024];
            for ( ;;)
            {
                const auto  n = recv( sock.fd(), buffer, sizeof( buffer), 0);
                if ( n <= 0)
                {
                    break;
                }
                response.append( buffer, static_cast<std::size_t>( n));
            }
            return response;
        }

        // 200 from "HTTP/1.1 200 OK", or 0 for anything that is not a status line.
        auto statusCode( const std::string & response) -> int
        {
            const auto  space = response.find( ' ');
            if ( !response.starts_with( "HTTP/") || space == std::string::npos || space + 4 > response.size())
            {
                return 0;
            }
            return std::atoi( response.c_str() + space + 1);
        }
    }

    auto postSafeTheRig( unsigned short port) -> bool
    {
        const auto  response = request( port, "POST", "/safe", kSafeTimeoutSeconds);
        const auto  code = response ? statusCode( *response) : 0;
        return code >= 200 && code < 300;
    }

    auto getConsoleStatus( unsigned short port) -> std::optional<ConsoleStatus>
    {
        const auto  response = request( port, "GET", "/api/status", kStatusTimeoutSeconds);
        if ( !response || statusCode( *response) != 200)
        {
            return std::nullopt;
        }

        // The body is one flat object thorium_webui writes itself --
        // {"viewers":N,"running":bool} -- so finding two keys is all the
        // parsing it needs.
        const auto  body = response->find( "\r\n\r\n");
        const auto  viewers = response->find( "\"viewers\":", body);
        const auto  running = response->find( "\"running\":", body);
        if ( body == std::string::npos || viewers == std::string::npos || running == std::string::npos)
        {
            return std::nullopt;
        }

        ConsoleStatus  status;
        status.Viewers = std::atoi( response->c_str() + viewers + 10);
        status.Running = response->compare( running + 10, 4, "true") == 0;
        return status;
    }
}
