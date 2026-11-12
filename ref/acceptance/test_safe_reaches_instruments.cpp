#include "runner/testing/acceptance.hpp"

//
// run_scripts --safe, against a supply that is really there.
//
// The claim under test is the one --safe exists to make and did not: that a
// fresh process, holding no session to anything, turns the outputs off. A
// driver's safe() goes only down a session that is already open (see
// hal::SafeableInstrument), so until --safe opened them itself
// (hal::contactForSafing()) it sent nothing, printed "Rig safed" and exited 0.
// Nothing short of the bytes on the wire tells those two apart, so this stands
// up a listener that answers as an EDU36311A and reads what it was sent.
//
// A file of its own because it needs a socket, and test_acceptance.cpp's
// opening comment is right that the scenarios there need nothing of the kind.
// POSIX sockets only: on Windows the scenario is compiled out rather than
// given a second implementation, since what it checks -- the order of calls in
// run_scripts' main() -- is not platform-dependent.
//
#ifndef _WIN32

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mutex>

using namespace thorium_acceptance;

namespace
{
    struct AcceptanceSafeOnTheWire : Acceptance {};

    //
    // One connection's worth of EDU36311A: answers *IDN? with the model the
    // driver's check wants and any other query with an empty error queue, and
    // keeps every line it was sent.
    //
    class FakeSupply
    {
        public:

            FakeSupply()
            {
                mListener = ::socket( AF_INET, SOCK_STREAM, 0);

                sockaddr_in address{};
                address.sin_family      = AF_INET;
                address.sin_addr.s_addr = htonl( INADDR_LOOPBACK);
                address.sin_port        = 0;

                socklen_t length = sizeof( address);

                if( mListener < 0
                 || ::bind( mListener, reinterpret_cast<sockaddr *>( &address), sizeof( address)) != 0
                 || ::listen( mListener, 1) != 0
                 || ::getsockname( mListener, reinterpret_cast<sockaddr *>( &address), &length) != 0)
                {
                    throw std::runtime_error( "the test could not create a listening socket");
                }

                mPort   = ntohs( address.sin_port);
                mServer = std::thread( [ this] { serve(); });
            }

            ~FakeSupply()
            {
                //
                // A connection to itself is what wakes an accept() nobody
                // connected to; closing the listener from another thread does
                // not reliably do it.
                //
                const int waker = ::socket( AF_INET, SOCK_STREAM, 0);

                sockaddr_in address{};
                address.sin_family      = AF_INET;
                address.sin_addr.s_addr = htonl( INADDR_LOOPBACK);
                address.sin_port        = htons( mPort);

                static_cast<void>( ::connect( waker, reinterpret_cast<sockaddr *>( &address), sizeof( address)));
                ::close( waker);

                mServer.join();
                ::close( mListener);
            }

            [[nodiscard]] auto port() const -> int { return mPort; }

            [[nodiscard]] auto received() -> std::vector<std::string>
            {
                const std::lock_guard lock{ mMutex };

                return mLines;
            }

        private:

            auto serve() -> void
            {
                const int connection = ::accept( mListener, nullptr, nullptr);

                if( connection < 0)
                {
                    return;
                }

                std::string pending;
                char        buffer[ 256];

                while( true)
                {
                    const auto count = ::recv( connection, buffer, sizeof( buffer), 0);

                    if( count <= 0)
                    {
                        break;
                    }

                    pending.append( buffer, static_cast<std::size_t>( count));

                    for( auto end = pending.find( '\n'); end != std::string::npos; end = pending.find( '\n'))
                    {
                        auto line = pending.substr( 0, end);
                        pending.erase( 0, end + 1);

                        if( !line.empty() && line.back() == '\r')
                        {
                            line.pop_back();
                        }

                        {
                            const std::lock_guard lock{ mMutex };
                            mLines.push_back( line);
                        }

                        if( line.find( '?') == std::string::npos)
                        {
                            continue;
                        }

                        const std::string reply = line.starts_with( "*IDN")
                            ? "Keysight Technologies,EDU36311A,CN00000001,1.0.2-1.0.1\n"
                            : "+0,\"No error\"\n";

                        static_cast<void>( ::send( connection, reply.data(), reply.size(), 0));
                    }
                }

                ::close( connection);
            }

            int                       mListener{ -1 };
            int                       mPort{ 0 };
            std::thread               mServer;
            std::mutex                mMutex;
            std::vector<std::string>  mLines;
    };
} // namespace

TEST_F( AcceptanceSafeOnTheWire, AFreshProcessTurnsTheSupplysOutputsOff)
{
    FakeSupply supply;

    EXPECT_EQ( run( { "--safe", "--address=BenchPsu=lan:127.0.0.1:" + std::to_string( supply.port()) }), 0) << mErr;

    EXPECT_TRUE( containsText( outPath(), mOut, "Rig safed"));

    //
    // All three outputs of the one box, each told to switch off. The exact
    // spelling of the rest is the driver's and its own tests'; that an OUTP
    // off was sent at all, per output, is this one's.
    //
    const auto lines = supply.received();

    const auto outputsSwitchedOff = std::ranges::count_if( lines, []( const std::string & line)
    {
        return line.starts_with( "OUTP 0");
    });

    std::string sent;

    for( const auto & line : lines)
    {
        sent += "  " + line + "\n";
    }

    EXPECT_EQ( outputsSwitchedOff, 3) << "the supply was sent:\n" << sent;
}

#endif // _WIN32
