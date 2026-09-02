//
// hal::io::ScpiSession's own tests -- the layer that knows what a reply means,
// tested against a transport that is a list of canned replies.
//
// No instrument and no socket, and neither is a limitation: what is under test
// here is the *interpretation* -- what "+0,\"No error\"" means, what
// "-113,\"Undefined header\"" does to the command that caused it, what
// "+9.90000000E+37" is, and how a reply that came back one packet at a time
// gets parsed. Whether the bytes reach an instrument is
// hal/io/socket_transport.hpp's business and is tested next door in
// test_socket_transport.cpp, against a real socket.
//
#include "hal/io/scpi.hpp"
#include "hal/io/transport.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    //
    // A transport that hands back whatever a test queued, in order, and records
    // everything sent to it.
    //
    // Replies are queued rather than derived from the commands, which is the
    // opposite of the fake in the EDU34450A driver's own tests -- and both are
    // right for what they test. There, the question is "what does the driver
    // send", so the fake has to keep answering plausibly through a long
    // exchange. Here, the question is "what does the session make of this exact
    // reply", so the reply is the input to the test and stating it is the whole
    // setup.
    //
    class ScriptedTransport final : public hal::io::ITransport
    {
        public:
            explicit ScriptedTransport( std::vector<std::string> replies) : mReplies( std::move( replies)) {}

            auto send( const std::string_view command) -> void override
            {
                mSent.emplace_back( command);
            }

            auto receive() -> std::string override
            {
                if( mNext >= mReplies.size())
                {
                    //
                    // Out of canned replies is the same silence a real
                    // instrument answers a rejected query with, so it is
                    // reported as the same exception -- which is what makes the
                    // queryChecked() test below a test of the real path rather
                    // than of a contrivance.
                    //
                    throw hal::io::TransportTimeout( "no reply queued");
                }

                return mReplies[ mNext++];
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "scripted";
            }

            [[nodiscard]]
            auto sent() const -> const std::vector<std::string> &
            {
                return mSent;
            }

        private:
            std::vector<std::string> mReplies;
            std::vector<std::string> mSent;
            std::size_t              mNext{ 0 };
    };

    struct Scripted
    {
        hal::io::ScpiSession  Session;
        ScriptedTransport *   Transport;
    };

    [[nodiscard]]
    auto scripted( std::vector<std::string> replies) -> Scripted
    {
        auto transport = std::make_unique<ScriptedTransport>( std::move( replies));
        auto raw       = transport.get();

        return Scripted{ hal::io::ScpiSession{ std::move( transport) }, raw };
    }
} // namespace

TEST( ScpiSession, ARefusedTransportIsNotASession)
{
    EXPECT_THROW( hal::io::ScpiSession{ nullptr }, hal::io::TransportError);
}

TEST( ScpiSession, QueryTrimsTheReplyAndSendsTheQuestionUnchanged)
{
    auto scpi = scripted( { "  Keysight Technologies,EDU34450A,MY60012345,01.00-01.00 \r" });

    EXPECT_EQ( scpi.Session.query( "*IDN?"), "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00");
    EXPECT_EQ( scpi.Transport->sent(), ( std::vector<std::string>{ "*IDN?" }));
}

//
// The empty error queue, in the instrument's own spelling. "+0" and not "0":
// SCPI signs its numbers, and a parser that did not expect the plus would read
// every clean queue as a fault.
//
TEST( ScpiSession, NoErrorIsAnEmptyQueueRatherThanAnErrorWithCodeZero)
{
    auto scpi = scripted( { "+0,\"No error\"" });

    EXPECT_FALSE( scpi.Session.nextError().has_value());
}

TEST( ScpiSession, ParsesAQueueEntrysNumberAndWords)
{
    auto scpi = scripted( { "-113,\"Undefined header\"" });

    const auto error = scpi.Session.nextError();

    ASSERT_TRUE( error.has_value());
    EXPECT_EQ( error->Code,    -113);
    EXPECT_EQ( error->Message, "Undefined header");
}

//
// The message half can contain commas, and the ones that do are the ones worth
// reading -- a settings conflict names both settings. So the number ends at the
// first comma and everything after it is the message, rather than the whole
// thing being split on commas.
//
TEST( ScpiSession, AQueueEntrysMessageMayContainCommas)
{
    auto scpi = scripted( { "-221,\"Settings conflict; DC voltage, autorange\"" });

    const auto error = scpi.Session.nextError();

    ASSERT_TRUE( error.has_value());
    EXPECT_EQ( error->Message, "Settings conflict; DC voltage, autorange");
}

//
// A command that provoked nothing costs one extra round trip and returns.
//
TEST( ScpiSession, CheckedSendsTheCommandThenAsksTheErrorQueue)
{
    auto scpi = scripted( { "+0,\"No error\"" });

    scpi.Session.checked( "CONF:VOLT:DC 10,1.5E-6");

    EXPECT_EQ( scpi.Transport->sent(), ( std::vector<std::string>{ "CONF:VOLT:DC 10,1.5E-6", "SYST:ERR?" }));
}

//
// And a command that did throws, naming itself. This is the whole reason
// checked() exists: without it the instrument carries on measuring whatever it
// was measuring before, and the next reading is a plausible number for the
// wrong configuration.
//
TEST( ScpiSession, CheckedThrowsNamingTheCommandAndTheInstrumentsWords)
{
    auto scpi = scripted( { "-222,\"Data out of range\"", "+0,\"No error\"" });

    try
    {
        scpi.Session.checked( "CONF:VOLT:DC 5000");

        FAIL() << "a queued error should not be swallowed";
    }
    catch( const hal::io::ScpiFault & fault)
    {
        EXPECT_EQ( fault.command(), "CONF:VOLT:DC 5000");
        EXPECT_EQ( fault.error().Code, -222);
        EXPECT_NE( std::string( fault.what()).find( "Data out of range"), std::string::npos);
        EXPECT_NE( std::string( fault.what()).find( "CONF:VOLT:DC 5000"),  std::string::npos);
    }
}

//
// A ScpiFault is not a TransportError, and this asserts the distinction rather
// than leaving it to the class comment. A caller catching TransportError to
// mean "the bench is unreachable" must not also catch a rejected command,
// which would report a driver bug as a missing instrument.
//
TEST( ScpiSession, ARejectedCommandIsNotATransportFailure)
{
    auto scpi = scripted( { "-113,\"Undefined header\"", "+0,\"No error\"" });

    EXPECT_THROW( scpi.Session.checked( "NOSUCH:COMMAND"), hal::io::ScpiFault);

    static_assert( ! std::is_base_of_v<hal::io::TransportError, hal::io::ScpiFault>);
}

//
// One bad command can queue two errors -- a parse error and a settings conflict
// behind it -- and the second must not be left to be attributed to whatever is
// sent next. So the queue is drained, not sampled.
//
TEST( ScpiSession, CheckedDrainsTheWholeQueueSoTheNextCommandStartsClean)
{
    auto scpi = scripted( {
        "-113,\"Undefined header\"",
        "-221,\"Settings conflict\"",
        "+0,\"No error\"" });

    EXPECT_THROW( scpi.Session.checked( "NOSUCH:COMMAND"), hal::io::ScpiFault);

    EXPECT_EQ( scpi.Transport->sent(), ( std::vector<std::string>{
        "NOSUCH:COMMAND",
        "SYST:ERR?",
        "SYST:ERR?",
        "SYST:ERR?" }));
}

//
// A query the instrument refused does not reply at all, so the transport times
// out -- and "timed out" is a poor report of "undefined header". queryChecked
// asks the queue once the timeout has happened and reports what the instrument
// actually said.
//
TEST( ScpiSession, ATimedOutQueryIsUpgradedToWhateverTheErrorQueueSays)
{
    //
    // The first receive() has no reply queued and so times out; the two after
    // it answer the queue read that follows.
    //
    auto scpi = scripted( { } );

    EXPECT_THROW( static_cast<void>( scpi.Session.queryChecked( "NOSUCH:QUERY?")), hal::io::TransportTimeout);
}

TEST( ScpiSession, ANumericReplyIsParsedIncludingItsLeadingSignAndExponent)
{
    auto scpi = scripted( { "+1.86850000E-03" });

    EXPECT_DOUBLE_EQ( scpi.Session.queryNumber( "READ?"), 0.0018685);
}

TEST( ScpiSession, ANonNumericReplyWhereANumberWasAskedForIsADesynchronisedSession)
{
    auto scpi = scripted( { "+0,\"No error\"" });

    EXPECT_THROW( static_cast<void>( scpi.Session.queryNumber( "READ?")), hal::io::TransportError);
}

//
// Several answers to one query -- which on the EDU34450A is what READ? sends
// when the secondary display is on.
//
TEST( ScpiSession, ACommaSeparatedReplyIsParsedAsEveryValueInIt)
{
    auto scpi = scripted( { "+5.02010000E+00,+1.20000000E-02" });

    const auto readings = scpi.Session.queryNumbers( "READ?");

    ASSERT_EQ( readings.size(), 2u);
    EXPECT_DOUBLE_EQ( readings[ 0], 5.0201);
    EXPECT_DOUBLE_EQ( readings[ 1], 0.012);
}

TEST( ScpiSession, ASingleValueIsStillAListOfOne)
{
    auto scpi = scripted( { "+5.02010000E+00" });

    EXPECT_EQ( scpi.Session.queryNumbers( "READ?").size(), 1u);
}

TEST( ScpiSession, AnEmptyReplyWhereNumbersWereAskedForThrows)
{
    auto scpi = scripted( { "" });

    EXPECT_THROW( static_cast<void>( scpi.Session.queryNumbers( "READ?")), hal::io::TransportError);
}

//
// The overload sentinel, in both signs and from two instrument families -- a
// Keysight DMM sends 9.9E+37 for an input beyond its range, an Infiniium
// 9.99999E+37 for a measurement it could not make. Compared against a
// threshold rather than for equality precisely because the exact value differs
// and every one of them is far outside any real reading.
//
TEST( ScpiSession, RecognisesTheOverloadSentinelWhicheverFamilySentIt)
{
    EXPECT_TRUE( hal::io::ScpiSession::isOverload(  9.9e37));
    EXPECT_TRUE( hal::io::ScpiSession::isOverload( -9.9e37));
    EXPECT_TRUE( hal::io::ScpiSession::isOverload(  9.99999e37));

    EXPECT_FALSE( hal::io::ScpiSession::isOverload( 5.0201));
    EXPECT_FALSE( hal::io::ScpiSession::isOverload( 0.0));
    EXPECT_FALSE( hal::io::ScpiSession::isOverload( -1000.0));
}

//
// How a number goes *into* a command. Neither of the two obvious C++ spellings
// is right: std::to_string( 0.1) is "0.100000" and std::to_string( 1e-6) is
// "0.000001", which is a different number of significant figures than was
// asked for.
//
TEST( ScpiSession, WritesACommandArgumentTheWayAnInstrumentsParserExpects)
{
    EXPECT_EQ( hal::io::ScpiSession::number( 10.0),   "10");
    EXPECT_EQ( hal::io::ScpiSession::number( 0.1),    "0.1");
    EXPECT_EQ( hal::io::ScpiSession::number( 1000.0), "1000");
    EXPECT_EQ( hal::io::ScpiSession::number( 0.001),  "0.001");
}

TEST( ScpiSession, ResetIsAFactoryResetAndAClearedStatusInThatOrder)
{
    auto scpi = scripted( { } );

    scpi.Session.reset();

    EXPECT_EQ( scpi.Transport->sent(), ( std::vector<std::string>{ "*RST", "*CLS" }));
}

//
// *OPC? blocks and its answer is discarded: what is wanted is the blocking,
// not the value -- an instrument that answers at all has finished.
//
TEST( ScpiSession, WaitForCompleteAsksAndDiscardsTheAnswer)
{
    auto scpi = scripted( { "1" });

    scpi.Session.waitForComplete();

    EXPECT_EQ( scpi.Transport->sent(), ( std::vector<std::string>{ "*OPC?" }));
}

//
// Draining is bounded at the queue's own depth plus one, so a wedged instrument
// whose queue refills as fast as it is read fails rather than hanging. The
// scripted transport here never says "no error", which is exactly that
// instrument.
//
TEST( ScpiSession, DrainingABottomlessErrorQueueTerminates)
{
    std::vector<std::string> endless( 100, "-350,\"Error queue overflow\"");

    auto scpi = scripted( std::move( endless));

    scpi.Session.clearErrors();

    EXPECT_LE( scpi.Transport->sent().size(), 21u);
}
