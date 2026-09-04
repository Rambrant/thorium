#include "hal/io/socket_transport.hpp"

#include <cerrno>
#include <cstring>
#include <string>

//
// The one translation unit that includes the platform's socket headers -- see
// hal/io/socket_transport.hpp on why they are not in the header.
//
#ifdef _WIN32
    //
    // WIN32_LEAN_AND_MEAN keeps <windows.h> (which <winsock2.h> drags in) from
    // also defining the GDI, Shell and RPC surfaces; NOMINMAX keeps it from
    // defining min/max as macros, which breaks std::min in anything compiled
    // after it. Both must precede the include, which is why they are here
    // rather than as compile definitions: a definition in the build reaches
    // every file, and this is the only file that needs it.
    //
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace hal::io
{
    namespace
    {
        //
        // The two platforms' socket APIs, reduced to the six calls this file
        // makes and the three names they disagree about. Not a portability
        // layer -- there is no ambition here beyond one TCP client -- but the
        // alternative is an #ifdef inside every function below, and those are
        // the #ifdefs that rot: the Windows half of a five-branch function is
        // compiled by nobody who is looking at it.
        //
#ifdef _WIN32
        using SocketHandle = SOCKET;
        constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

        //
        // Winsock needs initialising before any socket call and shutting down
        // after the last, per process. A function-local static does both at the
        // right moments and exactly once, including when two instruments are
        // opened from two threads -- initialisation of a function-local static
        // is thread-safe, and the alternative (a call in main()) would put a
        // requirement on every program that links this library, including every
        // test binary.
        //
        // POSIX needs nothing, so the call has an empty body there rather than
        // being #ifdef'd out at every call site: exactly one of these is
        // compiled, and both are called.
        //
        auto ensureNetworkStack() -> void
        {
            struct Winsock
            {
                Winsock()
                {
                    WSADATA data{};
                    mStarted = WSAStartup( MAKEWORD( 2, 2), &data) == 0;
                }

                ~Winsock()
                {
                    if( mStarted)
                    {
                        WSACleanup();
                    }
                }

                bool mStarted{ false };
            };

            static const Winsock winsock;

            if( !winsock.mStarted)
            {
                throw TransportError( "could not initialise the Windows socket stack (WSAStartup failed)");
            }
        }

        auto closeSocket( const SocketHandle socket) -> void
        {
            ::closesocket( socket);
        }

        [[nodiscard]]
        auto lastErrorText() -> std::string
        {
            return "winsock error " + std::to_string( WSAGetLastError());
        }

        [[nodiscard]]
        auto wouldBlock() -> bool
        {
            const int error = WSAGetLastError();

            return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAETIMEDOUT;
        }

        [[nodiscard]]
        auto wasInterrupted() -> bool
        {
            return WSAGetLastError() == WSAEINTR;
        }

        auto setNonBlocking( const SocketHandle socket, const bool nonBlocking) -> void
        {
            u_long mode = nonBlocking ? 1u : 0u;

            ::ioctlsocket( socket, FIONBIO, &mode);
        }

        //
        // Windows takes the timeout as a DWORD of milliseconds where POSIX
        // takes a struct timeval, which is the third and last thing the two
        // disagree about.
        //
        auto setTimeout( const SocketHandle socket, const int option, const std::chrono::milliseconds timeout) -> void
        {
            const DWORD milliseconds = static_cast<DWORD>( timeout.count());

            ::setsockopt( socket, SOL_SOCKET, option,
                reinterpret_cast<const char *>( &milliseconds), sizeof( milliseconds));
        }

        [[nodiscard]]
        auto pollWritable( const SocketHandle socket, const std::chrono::milliseconds timeout) -> int
        {
            WSAPOLLFD descriptor{ socket, POLLOUT, 0 };

            return ::WSAPoll( &descriptor, 1, static_cast<INT>( timeout.count()));
        }
#else
        using SocketHandle = int;
        constexpr SocketHandle kInvalidSocket = -1;

        auto ensureNetworkStack() -> void
        {
        }

        auto closeSocket( const SocketHandle socket) -> void
        {
            ::close( socket);
        }

        [[nodiscard]]
        auto lastErrorText() -> std::string
        {
            return std::strerror( errno);
        }

        [[nodiscard]]
        auto wouldBlock() -> bool
        {
            return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
        }

        [[nodiscard]]
        auto wasInterrupted() -> bool
        {
            return errno == EINTR;
        }

        auto setNonBlocking( const SocketHandle socket, const bool nonBlocking) -> void
        {
            const int flags = ::fcntl( socket, F_GETFL, 0);

            if( flags < 0)
            {
                return;
            }

            ::fcntl( socket, F_SETFL, nonBlocking ? ( flags | O_NONBLOCK) : ( flags & ~O_NONBLOCK));
        }

        auto setTimeout( const SocketHandle socket, const int option, const std::chrono::milliseconds timeout) -> void
        {
            timeval value{};

            value.tv_sec  = static_cast<decltype( value.tv_sec)>(  timeout.count() / 1000);
            value.tv_usec = static_cast<decltype( value.tv_usec)>( ( timeout.count() % 1000) * 1000);

            ::setsockopt( socket, SOL_SOCKET, option, &value, sizeof( value));
        }

        [[nodiscard]]
        auto pollWritable( const SocketHandle socket, const std::chrono::milliseconds timeout) -> int
        {
            pollfd descriptor{ socket, POLLOUT, 0 };

            return ::poll( &descriptor, 1, static_cast<int>( timeout.count()));
        }
#endif

        //
        // Connect with a deadline, which is the whole reason this is thirty
        // lines rather than one call to ::connect.
        //
        // A blocking connect() to a host that is powered off does not fail: it
        // retransmits its SYN until the OS gives up, which on both platforms is
        // between one and three minutes and is not configurable per socket. An
        // unattended run that lost one instrument would then sit there for a
        // minute per reading. So the socket goes non-blocking, connect() is
        // expected to return "in progress", poll() waits out the caller's own
        // timeout, and SO_ERROR says whether what completed was a connection or
        // a refusal.
        //
        // Restored to blocking afterwards, because from that point on the read
        // and write timeouts do the same job with far less code -- see the
        // SO_RCVTIMEO/SO_SNDTIMEO pair in the constructor.
        //
        auto connectWithTimeout(
            const SocketHandle              socket,
            const sockaddr *                address,
            const socklen_t                 addressLength,
            const std::chrono::milliseconds timeout) -> bool
        {
            setNonBlocking( socket, true);

            const int immediate = ::connect( socket, address, addressLength);

            if( immediate == 0)
            {
                setNonBlocking( socket, false);

                return true;
            }

            if( !wouldBlock())
            {
                return false;
            }

            if( pollWritable( socket, timeout) <= 0)
            {
                //
                // Zero is the timeout and negative is poll() itself failing.
                // Both are "no connection", and neither is worth telling apart
                // at the call site: a poll that fails on a socket this function
                // just created is not a diagnosis anyone can act on.
                //
                return false;
            }

            //
            // Writable does not mean connected. A refused connection also wakes
            // poll(), and the difference is in SO_ERROR -- which is the part of
            // a non-blocking connect that is easy to leave out and produces a
            // socket that appears to work and then fails on first use.
            //
            int       pendingError = 0;
            socklen_t errorSize    = sizeof( pendingError);

#ifdef _WIN32
            ::getsockopt( socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>( &pendingError), &errorSize);
#else
            ::getsockopt( socket, SOL_SOCKET, SO_ERROR, &pendingError, &errorSize);
#endif

            setNonBlocking( socket, false);

            return pendingError == 0;
        }
    } // namespace

    SocketTransport::SocketTransport( const std::string_view host, const int port, const TransportOptions & options) :
        mHost(    host),
        mPort(    port),
        mOptions( options)
    {
        ensureNetworkStack();

        //
        // getaddrinfo rather than gethostbyname or inet_addr: it takes a
        // hostname *or* a literal, answers for IPv6 as readily as IPv4, and is
        // the one that consults the resolver the way the rest of the machine
        // does. hal::Lan's own comment recommends a hostname over a dotted quad
        // on a DHCP bench, and this is what makes that recommendation true.
        //
        addrinfo hints{};

        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        const std::string service = std::to_string( port);

        addrinfo * candidates = nullptr;

        if( const int status = ::getaddrinfo( mHost.c_str(), service.c_str(), &hints, &candidates);
            status != 0 || candidates == nullptr)
        {
            throw TransportError( "cannot resolve " + description() + ": " + ::gai_strerror( status));
        }

        //
        // Every address the name resolved to, in the order the resolver gave
        // them, and the first one that connects wins. A dual-stack bench hands
        // back an AAAA record for a box that is only listening on IPv4 often
        // enough that trying one address and giving up is a bench problem
        // rather than a theoretical one.
        //
        std::string lastFailure = "no address to try";

        for( const addrinfo * candidate = candidates; candidate != nullptr; candidate = candidate->ai_next)
        {
            const SocketHandle socket = ::socket( candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);

            if( socket == kInvalidSocket)
            {
                lastFailure = lastErrorText();

                continue;
            }

            if( !connectWithTimeout( socket, candidate->ai_addr,
                    static_cast<socklen_t>( candidate->ai_addrlen), mOptions.ConnectTimeout))
            {
                lastFailure = lastErrorText();

                closeSocket( socket);

                continue;
            }

            mSocket = static_cast<std::intptr_t>( socket);

            break;
        }

        ::freeaddrinfo( candidates);

        if( mSocket < 0)
        {
            throw TransportError( "cannot reach " + description() + ": " + lastFailure);
        }

        const auto socket = static_cast<SocketHandle>( mSocket);

        //
        // TCP_NODELAY, and it matters more here than the usual hand-wave about
        // latency. SCPI is strictly request/response in short lines, which is
        // precisely the traffic pattern Nagle's algorithm was written to
        // coalesce: without this, a small command can sit in the kernel until
        // the previous reply is acknowledged, adding a round trip to every
        // single reading. A scan of two hundred pins pays it two hundred times.
        //
        int nodelay = 1;

#ifdef _WIN32
        ::setsockopt( socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>( &nodelay), sizeof( nodelay));
#else
        ::setsockopt( socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof( nodelay));
#endif

        //
        // Read and write deadlines, so a wedged instrument fails a reading
        // instead of stopping the run. See TransportOptions on why the number
        // is the caller's and not this file's.
        //
        setTimeout( socket, SO_RCVTIMEO, mOptions.IoTimeout);
        setTimeout( socket, SO_SNDTIMEO, mOptions.IoTimeout);
    }

    SocketTransport::~SocketTransport()
    {
        if( mSocket >= 0)
        {
            closeSocket( static_cast<SocketHandle>( mSocket));
        }
    }

    auto SocketTransport::send( const std::string_view command) -> void
    {
        //
        // The terminator this transport adds, and the reason ITransport::send
        // forbids callers from adding their own. A bare "\n" rather than
        // "\r\n": every SCPI parser accepts it, and the instruments that
        // *require* the carriage return are the ones behind a Telnet-style
        // command shell, which is the port this class deliberately does not
        // connect to.
        //
        const std::string wire = std::string( command) + "\n";

        std::size_t sent = 0;

        while( sent < wire.size())
        {
            const auto socket = static_cast<SocketHandle>( mSocket);

#ifdef _WIN32
            const int written = ::send( socket, wire.data() + sent,
                static_cast<int>( wire.size() - sent), 0);
#else
            //
            // MSG_NOSIGNAL, so writing to a socket the instrument closed
            // returns EPIPE instead of raising SIGPIPE and killing the run.
            // The default there is a process-wide signal disposition, which a
            // library has no business changing on its caller's behalf.
            //
            const auto written = ::send( socket, wire.data() + sent, wire.size() - sent, MSG_NOSIGNAL);
#endif

            if( written > 0)
            {
                sent += static_cast<std::size_t>( written);

                continue;
            }

            if( wasInterrupted())
            {
                continue;
            }

            if( wouldBlock())
            {
                throw TransportTimeout( "timed out sending to " + description() + ": " + std::string( command));
            }

            throw TransportError( "send to " + description() + " failed: " + lastErrorText());
        }
    }

    auto SocketTransport::receive() -> std::string
    {
        //
        // One line, from the buffer if a previous read already carried it. See
        // mPending's own comment: TCP has no message boundaries, so this is
        // where the boundaries are put back.
        //
        for( ;;)
        {
            if( const auto newline = mPending.find( '\n'); newline != std::string::npos)
            {
                std::string line = mPending.substr( 0, newline);

                mPending.erase( 0, newline + 1);

                //
                // A trailing carriage return is stripped as part of the
                // terminator rather than being handed to the caller. Some
                // instruments send CRLF regardless of what they were asked
                // for, and a reply of "+5.02E+00\r" parses as a number on
                // every strtod and as garbage on a std::stod that checks its
                // whole input.
                //
                if( !line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                return line;
            }

            char buffer[ 1024];

            const auto socket = static_cast<SocketHandle>( mSocket);

#ifdef _WIN32
            const int received = ::recv( socket, buffer, static_cast<int>( sizeof( buffer)), 0);
#else
            const auto received = ::recv( socket, buffer, sizeof( buffer), 0);
#endif

            if( received > 0)
            {
                mPending.append( buffer, static_cast<std::size_t>( received));

                continue;
            }

            if( received == 0)
            {
                //
                // The instrument closed the connection. Distinguished from a
                // timeout because the two mean opposite things: this one will
                // not improve by waiting, and the session is gone rather than
                // slow.
                //
                throw TransportError( description() + " closed the connection");
            }

            if( wasInterrupted())
            {
                continue;
            }

            if( wouldBlock())
            {
                throw TransportTimeout( "timed out waiting for a reply from " + description());
            }

            throw TransportError( "receive from " + description() + " failed: " + lastErrorText());
        }
    }

    auto SocketTransport::description() const -> std::string
    {
        return "Lan " + mHost + ":" + std::to_string( mPort);
    }
} // namespace hal::io
