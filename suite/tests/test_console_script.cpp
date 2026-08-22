#include "suite/scripts.hpp"

//
// Not suite/prelude.hpp: these tests are not scripts. They call one and inject
// its *replies* -- Read.inject( "Ser1.Data", ...) -- so they need the Read verb
// and core::Bytes, and none of the criteria tables or adapter points a script
// body is written against. Same split as
// suite/tests/test_supply_rail_script.cpp, and for the same reason.
//
#include "core/bytes.hpp"
#include "core/journal.hpp"
#include "hal/measure.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
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
    //
    // Registered on the one process-wide journal so a test can assert what the
    // script *recorded*, not only what it returned. Which matters for exactly
    // one thing here: a check that could not be made has to reach the report
    // naming the criterion it was about, and the return value cannot show that.
    //
    class CapturingSink : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override
            {
                Events.push_back( event);
            }

            std::vector<core::JournalEvent> Events;
    };

    struct ConsoleFixture : ::testing::Test
    {
        protected:
            void SetUp() override
            {
                core::journal().clearSinks();
                core::journal().add( Journal);
            }

            void TearDown() override
            {
                core::journal().clearSinks();

                Measure.useLive();
            }

            //
            // Criterion ids as strings, because this file deliberately does not
            // include the criteria tables (see the header comment above) -- and
            // because what is being asserted is what a report reader sees, which
            // is the string either way.
            //
            [[nodiscard]]
            auto uncheckedCriteria() const -> std::vector<std::string>
            {
                std::vector<std::string> ids;

                for( const auto & event : Journal.Events)
                {
                    if( event.Value == "<unchecked>")
                    {
                        ids.push_back( event.Subject);
                    }
                }

                return ids;
            }

            CapturingSink Journal;
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

//
// And it says so about *both* status criteria, by name. A truncated reply is
// not evidence that the DUT is unready or faulted -- it is evidence of nothing
// at all -- so each criterion is recorded as unchecked rather than as failed,
// and a consumer tracking either one across runs sees that this run could not
// answer it (see core::Fail in core/verify.hpp).
//
// Worth a test of its own rather than trusting the line above, because the
// return value is false either way. This path used to post a single ad-hoc
// check naming neither criterion, while the comment beside it claimed both were
// recorded.
//
TEST_F( ConsoleFixture, ATruncatedReplyRecordsBothStatusCriteriaAsUnchecked)
{
    Read.inject( "Ser1.Data", Bytes( "ACK\r"));

    EXPECT_FALSE( consoleScript());

    const auto unchecked = uncheckedCriteria();

    ASSERT_EQ( unchecked.size(), 2u);

    EXPECT_NE( std::ranges::find( unchecked, std::string( "FS_Console_Ready")), unchecked.end());
    EXPECT_NE( std::ranges::find( unchecked, std::string( "FS_Console_Fault")), unchecked.end());

    //
    // The one fact in the reason that is not already in the criteria table: how
    // much the DUT actually sent.
    //
    const auto reasons = std::ranges::count_if( Journal.Events,
        []( const core::JournalEvent & event)
        {
            return event.Detail.find( "4 bytes") != std::string::npos;
        });

    EXPECT_EQ( reasons, 2);
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
