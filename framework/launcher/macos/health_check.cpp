#include "health_check.hpp"

#include <chrono>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace launcher
{
    namespace
    {
        auto tryConnect( unsigned short port) -> bool
        {
            const auto  sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if ( sock < 0)
            {
                return false;
            }

            sockaddr_in  addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons( port);
            inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr);

            const auto  result = connect(
                sock, reinterpret_cast<const sockaddr *>( &addr), sizeof( addr));

            close( sock);
            return result == 0;
        }
    }

    auto waitForServer( unsigned short port, unsigned timeoutMs) -> bool
    {
        const auto  deadline = std::chrono::steady_clock::now()
                             + std::chrono::milliseconds( timeoutMs);
        do
        {
            if ( tryConnect( port))
            {
                return true;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100));
        }
        while ( std::chrono::steady_clock::now() < deadline);

        return false;
    }
}
