#include "health_check.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

namespace launcher
{
    namespace
    {
        class WinsockGuard
        {
            public:
                WinsockGuard()  { WSAStartup( MAKEWORD( 2, 2), &mData); }
                ~WinsockGuard() { WSACleanup(); }

                WinsockGuard( const WinsockGuard &) = delete;
                auto operator=( const WinsockGuard &) -> WinsockGuard & = delete;

            private:
                WSADATA  mData{};
        };

        auto tryConnect( unsigned short port) -> bool
        {
            const auto sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if ( sock == INVALID_SOCKET)
            {
                return false;
            }

            sockaddr_in  addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons( port);
            InetPtonW( AF_INET, L"127.0.0.1", &addr.sin_addr);

            const auto result = connect(
                sock, reinterpret_cast<const sockaddr *>( &addr), sizeof( addr));

            closesocket( sock);
            return result == 0;
        }
    }

    auto waitForServer( unsigned short port, unsigned timeoutMs) -> bool
    {
        const WinsockGuard  guard;

        const auto  deadline = GetTickCount64() + timeoutMs;
        do
        {
            if ( tryConnect( port))
            {
                return true;
            }
            Sleep( 100);
        }
        while ( GetTickCount64() < deadline);

        return false;
    }
}
