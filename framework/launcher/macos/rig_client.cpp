#include "rig_client.hpp"

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
    }

    auto postSafeTheRig( unsigned short port) -> bool
    {
        const Socket  sock;
        if ( sock.fd() < 0)
        {
            return false;
        }

        // Both directions: a server that accepts and then never answers must
        // not hang this thread forever.
        const timeval  timeout{ kSafeTimeoutSeconds, 0 };
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
            return false;
        }

        const std::string  request =
            "POST /safe HTTP/1.1\r\n"
            "Host: 127.0.0.1:" + std::to_string( port) + "\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        if ( !sendAll( sock.fd(), request))
        {
            return false;
        }

        // Only the status line matters: "HTTP/1.1 200 OK".
        std::string  response;
        char         buffer[ 256];
        while ( response.find( "\r\n") == std::string::npos)
        {
            const auto  n = recv( sock.fd(), buffer, sizeof( buffer), 0);
            if ( n <= 0)
            {
                break;
            }
            response.append( buffer, static_cast<std::size_t>( n));
        }

        const auto  space = response.find( ' ');
        if ( !response.starts_with( "HTTP/") || space == std::string::npos || space + 1 >= response.size())
        {
            return false;
        }
        return response[ space + 1] == '2';
    }
}
