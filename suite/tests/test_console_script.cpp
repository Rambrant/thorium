#include "suite/scripts.hpp"

//
// Not suite/prelude.hpp: these tests are not scripts. They call one and inject
// its *replies* -- Read.inject( "Ser1.Data", ...) -- so they need the Read verb
// and core::Bytes, and none of the criteria tables or adapter points a script
// body is written against. Same split as
// suite/tests/test_supply_rail_script.cpp, and for the same reason.
//
#include "core/bytes.hpp"
#include "hal/measure.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using core::Bytes;

namespace
{
    //
    // A whole console dialogue, authored in the test. The reply is what the
    // script's own protocol says it should be: "ACK" then the status byte,
    // terminated by a carriage return.
    //
    // 0x08 has bit 3 (READY) set and bit 7 (FAULT) clear -- the healthy case
    // both criteria are written against.
    //
    auto replyWithStatus( const unsigned char status) -> Bytes
    {
        return Bytes( std::vector<std::byte>{
            std::byte{ 'A' }, std::byte{ 'C' }, std::byte{ 'K' }, std::byte{ '\r' }, std::byte{ status } });
    }

    //
    // Read and Measure share one session bank (see core::SessionBank), so
    // useLive() through Measure is what discards this test's injections -- for
    // both seams at once. That is the behaviour worth relying on here: a
    // fixture that cleaned up only half of them would leak canned replies into
    // whichever test ran next.
    //
    struct ConsoleFixture : ::testing::Test
    {
        protected:
            void TearDown() override
            {
                Measure.useLive();
            }
    };
} // namespace

TEST_F( ConsoleFixture, PassesWhenTheDutAcknowledgesAndReportsAHealthyStatus)
{
    Read.inject( "Ser1.Data", replyWithStatus( 0x08));

    EXPECT_TRUE( consoleScript());
}

//
// The distinction the two criteria exist to draw. A DUT that answered
// something other than ACK and one that answered ACK with a fault flag set are
// different findings, and a single "the console works" criterion would report
// them identically.
//
TEST_F( ConsoleFixture, FailsWhenTheDutDoesNotAcknowledge)
{
    Read.inject( "Ser1.Data", Bytes( "NAK\r\x08"));

    EXPECT_FALSE( consoleScript());
}

TEST_F( ConsoleFixture, FailsWhenTheFaultBitIsSet)
{
    // 0x88 -- READY still set, but FAULT (bit 7) set with it.
    Read.inject( "Ser1.Data", replyWithStatus( 0x88));

    EXPECT_FALSE( consoleScript());
}

TEST_F( ConsoleFixture, FailsWhenTheReadyBitIsClear)
{
    Read.inject( "Ser1.Data", replyWithStatus( 0x00));

    EXPECT_FALSE( consoleScript());
}

//
// A reply too short to hold a status byte fails the run rather than throwing
// out of core::Bytes::at -- the script guards the index precisely so a silent
// DUT is reported as a failed check and not as a crash.
//
TEST_F( ConsoleFixture, ATruncatedReplyFailsRatherThanThrowing)
{
    Read.inject( "Ser1.Data", Bytes( "ACK\r"));

    EXPECT_FALSE( consoleScript());
}

TEST_F( ConsoleFixture, ASilentDutFailsRatherThanThrowing)
{
    Read.inject( "Ser1.Data", Bytes{});

    EXPECT_FALSE( consoleScript());
}

//
// A scripted run that never armed this port is a hard error, not a failed
// check -- the same contract Measure has for an unprogrammed point (see
// suite/tests/test_supply_rail_script.cpp's ThrowsWhenAPointIsMissing). A test
// that forgot to author the reply must not look like a test whose DUT stayed
// quiet.
//
// Armed through a different key rather than through none at all, and the
// distinction is the contract: with nothing injected anywhere the bank is still
// live, and a live read of a simulated port legitimately returns nothing. It is
// entering scripted mode and then asking for a port nobody programmed that has
// no honest answer.
//
TEST_F( ConsoleFixture, ThrowsWhenTheRunIsScriptedButThePortWasNeverArmed)
{
    Read.inject( "SomeOtherPort.Data", Bytes( "ACK\r"));

    EXPECT_THROW( (void) consoleScript(), std::runtime_error);
}

//
// The acknowledgement is checked against the bytes *before* the terminator, so
// a reply that merely starts with ACK is not the same as one that acknowledged
// and stopped. This is what the criterion's byte-for-byte equality buys, and
// what a startsWith check would have thrown away.
//
TEST_F( ConsoleFixture, AnAcknowledgementIsComparedInFullNotAsAPrefix)
{
    Read.inject( "Ser1.Data", Bytes( "ACKNOWLEDGED\r\x08"));

    EXPECT_FALSE( consoleScript());
}
