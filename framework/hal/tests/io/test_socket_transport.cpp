//
// hal::io::SocketTransport's own tests, against a real socket.
//
// The one test file in this tree that does I/O, and it earns that: everything
// else about talking to an instrument can be tested against a fake -- what the
// driver sends (instruments/keysight_edu34450a/tests), what a reply means
// (test_scpi.cpp) -- but whether the bytes actually leave the process cannot
// be. A transport that compiles, links, and has never opened a socket is
// exactly the kind of code that works until the first time it matters.
//
// So this file stands up a listener on 127.0.0.1, hands the transport its port,
// and asserts on both ends of the conversation. No instrument, no network, no
// port left open after the test: an ephemeral port on the loopback interface,
// which is as close to hermetic as a socket test gets.
//
// What it is really testing is the two things about TCP that catch people, and
// which no amount of reading the manual reveals:
//
//   there are no message boundaries. A reply written in one call can arrive in
//   three, and three replies written separately can arrive in one. Both happen
//   on a real LAN with a real instrument, and both are tested below.
//
//   a connection to a host that is not there does not fail, it *waits* -- for
//   between one and three minutes, on both platforms, unless something makes
//   it stop. An unattended run that lost an instrument would sit there for a
//   minute per reading, which is the failure this transport's connect timeout
//   exists to prevent and which is asserted here rather than assumed.
//
#include "hal/io/socket_transport.hpp"
#include "hal/io/transport.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
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
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
    using SocketHandle = SOCKET;

    constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

    auto closeSocket( const SocketHandle socket) -> void
    {
        ::closesocket( socket);
    }

    //
    // The test needs its own WSAStartup: it creates the listening socket
    // *before* the transport under test creates the connecting one, so the
    // library's own initialisation has not run yet.
    //
    auto ensureNetworkStack() -> void
    {
        struct Winsock
        {
            Winsock()  { WSADATA data{}; WSAStartup( MAKEWORD( 2, 2), &data); }
            ~Winsock() { WSACleanup(); }
        };

        static const Winsock winsock;
    }
#else
    using SocketHandle = int;

    constexpr SocketHandle kInvalidSocket = -1;

    auto closeSocket( const SocketHandle socket) -> void
    {
        ::close( socket);
    }

    auto ensureNetworkStack() -> void
    {
    }
#endif

    //
    // A listener on the loopback interface pretending to be an instrument: it
    // accepts one connection, reads one command line, writes the chunks it was
    // given, and then waits to be shut down.
    //
    // Chunks rather than replies, and that distinction is the point of several
    // tests below. A chunk is written to the socket verbatim, terminators
    // included, with a pause between chunks -- so a test can say "this reply
    // arrives in two packets" or "these two replies arrive in one", which is
    // what a real instrument on a real network does and what the transport's
    // buffering has to survive.
    //
    class FakeInstrument
    {
        public:
            explicit FakeInstrument( std::vector<std::string> chunks, const bool closeAfterCommand = false) :
                mChunks(            std::move( chunks)),
                mCloseAfterCommand( closeAfterCommand)
            {
                ensureNetworkStack();

                mListener = ::socket( AF_INET, SOCK_STREAM, 0);

                if( mListener == kInvalidSocket)
                {
                    throw std::runtime_error( "the test could not create a listening socket");
                }

                sockaddr_in address{};

                address.sin_family      = AF_INET;
                address.sin_addr.s_addr = htonl( INADDR_LOOPBACK);
                //
                // Port zero: the kernel picks a free one, which is what keeps
                // this test from colliding with anything else on the machine
                // (including a second copy of itself under a parallel ctest).
                // Which port it picked is then read back with getsockname.
                //
                address.sin_port = 0;

                if( ::bind( mListener, reinterpret_cast<sockaddr *>( &address), sizeof( address)) != 0)
                {
                    closeSocket( mListener);

                    throw std::runtime_error( "the test could not bind a loopback port");
                }

                sockaddr_in bound{};
                socklen_t   boundSize = sizeof( bound);

                ::getsockname( mListener, reinterpret_cast<sockaddr *>( &bound), &boundSize);

                mPort = ntohs( bound.sin_port);

                ::listen( mListener, 1);

                mThread = std::thread( [ this] { serve(); });
            }

            ~FakeInstrument()
            {
                //
                // Closing the listener is what wakes a blocked accept(), and
                // closing the accepted connection is what wakes a blocked
                // recv() -- so the order here is what makes the join return
                // rather than hanging the test binary.
                //
                mStopping = true;

                if( mListener != kInvalidSocket)
                {
                    closeSocket( mListener);
                }

                if( mConnection != kInvalidSocket)
                {
                    closeSocket( mConnection);
                }

                if( mThread.joinable())
                {
                    mThread.join();
                }
            }

            FakeInstrument( const FakeInstrument &)                    = delete;
            auto operator=( const FakeInstrument &) -> FakeInstrument & = delete;

            [[nodiscard]]
            auto port() const -> int
            {
                return mPort;
            }

            //
            // The exact bytes this listener was sent, terminator included --
            // which is how the "the transport appends the terminator" test can
            // assert on a byte a std::string reply would have hidden.
            //
            // Read only after the transport under test has been destroyed, so
            // the serving thread is no longer writing to it.
            //
            [[nodiscard]]
            auto received() const -> std::string
            {
                return mReceived;
            }

        private:
            auto serve() -> void
            {
                mConnection = ::accept( mListener, nullptr, nullptr);

                if( mConnection == kInvalidSocket)
                {
                    return;
                }

                //
                // One command line. Reading until the newline rather than once,
                // for the same reason the transport does: the command may
                // arrive in pieces.
                //
                char buffer[ 512];

                while( mReceived.find( '\n') == std::string::npos && !mStopping)
                {
#ifdef _WIN32
                    const int received = ::recv( mConnection, buffer, static_cast<int>( sizeof( buffer)), 0);
#else
                    const auto received = ::recv( mConnection, buffer, sizeof( buffer), 0);
#endif

                    if( received <= 0)
                    {
                        return;
                    }

                    mReceived.append( buffer, static_cast<std::size_t>( received));
                }

                if( mCloseAfterCommand)
                {
                    closeSocket( mConnection);

                    mConnection = kInvalidSocket;

                    return;
                }

                for( const auto & chunk : mChunks)
                {
#ifdef _WIN32
                    ::send( mConnection, chunk.data(), static_cast<int>( chunk.size()), 0);
#else
                    ::send( mConnection, chunk.data(), chunk.size(), 0);
#endif

                    //
                    // A real pause between chunks, so that a multi-chunk reply
                    // genuinely arrives in separate packets rather than being
                    // coalesced by the kernel into one -- which would make the
                    // split-reply test pass without testing anything.
                    //
                    std::this_thread::sleep_for( std::chrono::milliseconds( 20));
                }
            }

            std::vector<std::string> mChunks;
            bool                     mCloseAfterCommand{ false };
            SocketHandle             mListener{   kInvalidSocket };
            SocketHandle             mConnection{ kInvalidSocket };
            int                      mPort{};
            std::string              mReceived;
            std::atomic<bool>        mStopping{ false };
            std::thread              mThread;
    };

    //
    // Short timeouts throughout: these tests are about whether a deadline is
    // honoured, not about how long the default one is, and a test suite that
    // waits two seconds to prove a timeout works is a test suite somebody turns
    // off.
    //
    [[nodiscard]]
    auto quickTimeouts() -> hal::io::TransportOptions
    {
        return hal::io::TransportOptions{
            std::chrono::milliseconds( 500),
            std::chrono::milliseconds( 300) };
    }
} // namespace

TEST( SocketTransport, SendsACommandAndReadsTheReply)
{
    FakeInstrument instrument{ { "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00\n" } };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    transport.send( "*IDN?");

    EXPECT_EQ( transport.receive(), "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00");
}

//
// The terminator is the transport's, not the caller's -- which is what lets a
// USBTMC transport frame the same bytes differently without every call site
// being wrong (see hal::io::ITransport::send). Asserted on the listener's own
// buffer, because a reply-side assertion could not see it.
//
TEST( SocketTransport, AppendsTheTerminatorTheInstrumentsParserWaitsFor)
{
    FakeInstrument instrument{ { "ok\n" } };

    {
        hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

        transport.send( "CONF:VOLT:DC 10,1.5E-6");

        static_cast<void>( transport.receive());
    }

    EXPECT_EQ( instrument.received(), "CONF:VOLT:DC 10,1.5E-6\n");
}

//
// A reply that arrives in three packets is still one reply. This is the failure
// that a transport written as "one recv() per receive()" passes on loopback and
// fails on a real network the first time a reading straddles a packet boundary.
//
TEST( SocketTransport, AssemblesAReplyThatArrivesInSeveralPackets)
{
    FakeInstrument instrument{ { "+5.0", "2010", "000E+00\n" } };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    transport.send( "READ?");

    EXPECT_EQ( transport.receive(), "+5.02010000E+00");
}

//
// And two replies that arrive in one packet are still two replies -- the second
// waits in the transport's buffer rather than being handed over with the first,
// which would desynchronise the session in a way nothing would notice until
// several readings later.
//
TEST( SocketTransport, KeepsASecondReplyThatArrivedWithTheFirst)
{
    FakeInstrument instrument{ { "+0,\"No error\"\n+5.02010000E+00\n" } };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    transport.send( "SYST:ERR?");

    EXPECT_EQ( transport.receive(), "+0,\"No error\"");
    EXPECT_EQ( transport.receive(), "+5.02010000E+00");
}

//
// Some instruments answer CRLF whatever they were asked for, and a reading of
// "+5.02010000E+00\r" parses as a number on a lenient parser and as garbage on
// a strict one. The carriage return is part of the terminator and is stripped
// with it.
//
TEST( SocketTransport, StripsACarriageReturnAsPartOfTheTerminator)
{
    FakeInstrument instrument{ { "+5.02010000E+00\r\n" } };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    transport.send( "READ?");

    EXPECT_EQ( transport.receive(), "+5.02010000E+00");
}

//
// An instrument that accepts the command and never answers -- which is exactly
// what one does when it has rejected the command and queued an error instead.
// The read has to give up, and give up saying which of the two failures it was:
// hal::io::ScpiSession::queryChecked reads the error queue on precisely this
// exception and turns the silence into the instrument's own words.
//
TEST( SocketTransport, TimesOutOnAnInstrumentThatNeverAnswers)
{
    FakeInstrument instrument{ { } };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    transport.send( "NOSUCH:QUERY?");

    EXPECT_THROW( static_cast<void>( transport.receive()), hal::io::TransportTimeout);
}

//
// A closed connection is not a slow one, and the two must not be reported the
// same way: waiting longer fixes a timeout and cannot fix this.
//
TEST( SocketTransport, DistinguishesAClosedConnectionFromASlowOne)
{
    FakeInstrument instrument{ { }, /* closeAfterCommand */ true };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    transport.send( "READ?");

    try
    {
        static_cast<void>( transport.receive());

        FAIL() << "a closed connection should not read as a reply";
    }
    catch( const hal::io::TransportTimeout &)
    {
        FAIL() << "a closed connection is not a timeout";
    }
    catch( const hal::io::TransportError & error)
    {
        EXPECT_NE( std::string( error.what()).find( "closed the connection"), std::string::npos);
    }
}

//
// Nothing listening: refused rather than waited out, and fast. The port is one
// the kernel handed out for a listener that has since been closed, which is as
// reliable a "nothing here" as a test can arrange.
//
TEST( SocketTransport, ARefusedConnectionFailsRatherThanWaiting)
{
    int closedPort = 0;

    {
        FakeInstrument instrument{ { } };

        closedPort = instrument.port();
    }

    const auto started = std::chrono::steady_clock::now();

    EXPECT_THROW(
        ( hal::io::SocketTransport{ "127.0.0.1", closedPort, quickTimeouts() }),
        hal::io::TransportError);

    EXPECT_LT( std::chrono::steady_clock::now() - started, std::chrono::seconds( 2));
}

//
// A hostname that resolves to nothing -- a re-cabled rack, a DHCP lease that
// moved, a typo in the rig's instrument table. The message names the address as
// the table wrote it, which is what sends a reader to the right row.
//
TEST( SocketTransport, AnUnresolvableHostSaysWhatItCouldNotResolve)
{
    try
    {
        hal::io::SocketTransport transport{
            "no-such-host.invalid", 5025, quickTimeouts() };

        FAIL() << "a hostname that resolves to nothing is not an instrument";
    }
    catch( const hal::io::TransportError & error)
    {
        EXPECT_NE( std::string( error.what()).find( "no-such-host.invalid:5025"), std::string::npos);
    }
}

TEST( SocketTransport, DescribesItselfAsTheAddressItWasOpenedFrom)
{
    FakeInstrument instrument{ { } };

    hal::io::SocketTransport transport{ "127.0.0.1", instrument.port(), quickTimeouts() };

    EXPECT_EQ( transport.description(), "Lan 127.0.0.1:" + std::to_string( instrument.port()));
}

//
// ===========================================================================
// The factory, as far as a socket is concerned
// ===========================================================================
//
// Only the two answers that are about *this* transport. Which bus kinds the
// build can open at all is no longer a question with one answer -- GPIB, USB
// and serial go through whatever VISA is installed, so the answer differs
// between this machine and the bench -- and it is asserted in
// test_visa_transport.cpp, which owns that question. What stays here is the
// half that is true everywhere: a LAN address is served by a socket and needs
// nothing installed, and a Simulated address is not something to open.
//
TEST( OpenTransport, OpensALanAddressWithASocketAndNoVendorSoftware)
{
    FakeInstrument instrument{ { "ok\n" } };

    const auto transport = hal::io::openTransport(
        hal::Lan{ "127.0.0.1", instrument.port() }, quickTimeouts());

    ASSERT_NE( transport, nullptr);

    transport->send( "*IDN?");

    EXPECT_EQ( transport->receive(), "ok");

    //
    // A socket, not VISA -- which the description says, and which matters
    // because a machine with VISA installed could plausibly have served this
    // address either way. LAN deliberately never goes through the vendor
    // library: this transport is tested against a real listener and that one
    // cannot be.
    //
    EXPECT_EQ( transport->description(), "Lan 127.0.0.1:" + std::to_string( instrument.port()));
}

//
// A Simulated address is the one refusal that is not a gap: there is genuinely
// nothing at the other end, which is what the type means. A driver reaching
// here with one has skipped its own simulation hooks, and the message says so
// rather than describing a missing feature.
//
TEST( OpenTransport, ASimulatedAddressIsNotSomethingToOpen)
{
    try
    {
        static_cast<void>( hal::io::openTransport( hal::Simulated{} ));

        FAIL() << "a Simulated address has nothing to connect to";
    }
    catch( const hal::io::UnsupportedTransport & unsupported)
    {
        EXPECT_NE( std::string( unsupported.what()).find( "simulation hooks"), std::string::npos);
    }
}

TEST( OpenTransport, SaysALanAddressIsSupportedWithoutTryingIt)
{
    EXPECT_TRUE(  hal::io::isSupported( hal::Lan{ "bench-dmm1" } ));
    EXPECT_FALSE( hal::io::isSupported( hal::Simulated{} ));
}
