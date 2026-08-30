#include "core/session/session.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

#include "core/quantities/bytes.hpp"
#include "core/session/recording.hpp"

//
// The payload seam -- core::ISession::fetchData and the three sessions behind
// it. These are the same claims framework/core/tests/session/test_session.cpp already makes
// about the quantity seam, asked of the byte one, because the whole argument
// for a second method rather than a widened variant is that a serial read has
// to be scriptable, recordable and replayable on exactly the same terms as a
// measurement. A framework where a script's rail readings replay and its
// console dialogue silently goes live would produce green runs that mean
// nothing.
//

using core::Bytes;

namespace
{
    auto liveReply( const char * text) -> std::function<Bytes()>
    {
        return [ text]() -> Bytes { return Bytes( text); };
    }

    //
    // A live read that fails the test if it is ever performed -- how "this
    // session did not touch the instrument" is asserted, rather than inferred
    // from the value that came back.
    //
    auto neverRead() -> std::function<Bytes()>
    {
        return []() -> Bytes
        {
            ADD_FAILURE() << "the live read was performed by a session that should never reach the instrument";

            return Bytes{};
        };
    }
} // namespace

TEST( CoreSessionData, ALiveSessionPerformsTheRead)
{
    core::LiveSession live;

    EXPECT_EQ( live.fetchData( "Console", "Ser1", liveReply( "ACK\r")), Bytes( "ACK\r"));
}

TEST( CoreSessionData, AProgrammedPayloadIsHandedBackWithoutTouchingTheInstrument)
{
    core::ScriptedSession scripted;

    scripted.programData( "Console", Bytes( "0xF5\r"));

    EXPECT_EQ( scripted.fetchData( "Console", "Ser1", neverRead()), Bytes( "0xF5\r"));
}

//
// A constant source never exhausts, so "this port always answers ACK" holds
// however many times the script under test reads it.
//
TEST( CoreSessionData, AProgrammedConstantNeverRunsOut)
{
    core::ScriptedSession scripted;

    scripted.programData( "Console", Bytes( "ACK\r"));

    for( int i = 0; i < 5; ++i)
    {
        EXPECT_EQ( scripted.fetchData( "Console", "Ser1", neverRead()), Bytes( "ACK\r"));
    }
}

TEST( CoreSessionData, ASequenceIsHandedBackInOrder)
{
    core::ScriptedSession scripted;

    scripted.programData( "Console", core::dataSourceOf( std::vector<Bytes>{ Bytes( "ACK\r"), Bytes( "0xF5\r") }));

    EXPECT_EQ( scripted.fetchData( "Console", "Ser1", neverRead()), Bytes( "ACK\r"));
    EXPECT_EQ( scripted.fetchData( "Console", "Ser1", neverRead()), Bytes( "0xF5\r"));
}

//
// Reading a port more times than the test authored replies for it means the
// script has diverged from what was expected -- which is the thing worth
// failing on, not something to paper over by repeating the last reply.
//
TEST( CoreSessionData, AnExhaustedSequenceThrows)
{
    core::ScriptedSession scripted;

    scripted.programData( "Console", core::dataSourceOf( std::vector<Bytes>{ Bytes( "ACK\r") }));

    EXPECT_EQ( scripted.fetchData( "Console", "Ser1", neverRead()), Bytes( "ACK\r"));
    EXPECT_THROW( (void) scripted.fetchData( "Console", "Ser1", neverRead()), std::runtime_error);
}

TEST( CoreSessionData, AnUnprogrammedNameThrowsRatherThanReadingLive)
{
    core::ScriptedSession scripted;

    EXPECT_THROW( (void) scripted.fetchData( "Console", "Ser1", neverRead()), std::runtime_error);
}

//
// The two maps are independent: programming a quantity under a name says
// nothing about payloads under it, and each seam reports its own absence.
//
TEST( CoreSessionData, TheQuantityAndPayloadSeamsDoNotSeeEachOther)
{
    core::ScriptedSession scripted;

    scripted.programData( "Console", Bytes( "ACK\r"));

    EXPECT_THROW( (void) scripted.fetch( "Console", "Ser1", core::QuantityKind::Voltage,
                                         []() -> core::QuantityVariant { return core::quantities::Voltage{ 1.0 }; }),
                  std::runtime_error);
}

TEST( CoreSessionData, ASwitchableSessionRoutesPayloadsToWhicheverSessionItHolds)
{
    core::LiveSession       live;
    core::ScriptedSession   scripted;
    core::SwitchableSession switchable( live);

    scripted.programData( "Console", Bytes( "canned"));

    EXPECT_EQ( switchable.fetchData( "Console", "Ser1", liveReply( "live")), Bytes( "live"));

    switchable.use( scripted);
    EXPECT_EQ( switchable.fetchData( "Console", "Ser1", neverRead()), Bytes( "canned"));

    switchable.useDefault();
    EXPECT_EQ( switchable.fetchData( "Console", "Ser1", liveReply( "live")), Bytes( "live"));
}

//
// Payloads and quantities go into one ordered vector, not two. Replay
// correctness is a matter of order, and a run that measured, read and measured
// again has to come back in that order.
//
TEST( CoreSessionData, PayloadsAndQuantitiesShareOneOrderedRecording)
{
    core::LiveSession      live;
    core::RecordingSession recording( live);

    (void) recording.fetch( "Output5V", "Dmm1", core::QuantityKind::Voltage,
                            []() -> core::QuantityVariant { return core::quantities::Voltage{ 5.02 }; });
    (void) recording.fetchData( "Console", "Ser1", liveReply( "ACK\r"));
    (void) recording.fetch( "Output3V3", "Dmm1", core::QuantityKind::Voltage,
                            []() -> core::QuantityVariant { return core::quantities::Voltage{ 3.31 }; });

    const auto & samples = recording.samples();

    ASSERT_EQ( samples.size(), 3u);

    EXPECT_EQ( samples[0].mSequence, 0u);
    EXPECT_EQ( samples[1].mSequence, 1u);
    EXPECT_EQ( samples[2].mSequence, 2u);

    EXPECT_TRUE( std::holds_alternative<core::QuantityVariant>( samples[0].mValue));
    EXPECT_TRUE( std::holds_alternative<Bytes>(                 samples[1].mValue));
    EXPECT_TRUE( std::holds_alternative<core::QuantityVariant>( samples[2].mValue));

    EXPECT_EQ( samples[1].mPointName,    "Console");
    EXPECT_EQ( samples[1].mInstrumentId, "Ser1");
}

//
// A payload row writes its value as unspaced hex rather than as the text it may
// well be, precisely so this holds: a reply containing a tab or a newline would
// otherwise split or truncate the row it was written on.
//
TEST( CoreSessionData, APayloadWithTabsAndNewlinesSurvivesTheFileRoundTrip)
{
    const auto awkward = Bytes( "a\tb\nc\r");

    const std::vector<core::RecordedSample> written{
        core::RecordedSample{ 0, 1000, "Console", "Console", "Ser1", awkward },
        core::RecordedSample{ 1, 1001, "SupplyRail", "Output5V", "Dmm1",
                              core::QuantityVariant{ core::quantities::Voltage{ 5.0 } } }
    };

    std::ostringstream out;
    core::writeRecording( out, written);

    std::istringstream in( out.str());
    const auto         readBack = core::readRecording( in);

    ASSERT_EQ( readBack.size(), 2u);
    EXPECT_EQ( std::get<Bytes>( readBack[0].mValue), awkward);
    EXPECT_EQ( readBack[0].mPointName, "Console");
    EXPECT_TRUE( std::holds_alternative<core::QuantityVariant>( readBack[1].mValue));
}

//
// An empty reply is a real thing to record -- a port that answered nothing
// within its timeout -- and must not read back as a malformed row.
//
TEST( CoreSessionData, AnEmptyPayloadRoundTrips)
{
    const std::vector<core::RecordedSample> written{
        core::RecordedSample{ 0, 1000, "Console", "Console", "Ser1", Bytes{} }
    };

    std::ostringstream out;
    core::writeRecording( out, written);

    std::istringstream in( out.str());
    const auto         readBack = core::readRecording( in);

    ASSERT_EQ( readBack.size(), 1u);
    EXPECT_TRUE( std::get<Bytes>( readBack[0].mValue).empty());
}

//
// A corrupt payload row is reported the way every other malformed row is: a
// std::runtime_error naming the row.
//
// Worth pinning because the natural implementation does not do this.
// Bytes::fromHex throws std::invalid_argument -- correct for it, since a caller
// passing bad hex made a programming error -- and letting that escape would
// mean a consumer catching what readRecording documents itself as throwing
// misses exactly the malformed input it was guarding against, and that the
// message it did get names a stray character with no indication of which row or
// which recording it came from.
//
TEST( CoreSessionData, ACorruptPayloadRowIsReportedAsAMalformedRow)
{
    std::istringstream in( "0\t0\tConsole\tConsole\tSer1\t<bytes>\t4143Z\n");

    try
    {
        (void) core::readRecording( in);

        ADD_FAILURE() << "a row with a non-hex digit in its payload should not be accepted";
    }
    catch( const std::runtime_error & error)
    {
        const std::string message = error.what();

        EXPECT_NE( message.find( "malformed"), std::string::npos);
        EXPECT_NE( message.find( "Console"),   std::string::npos) << "the row itself has to be in the message";
    }
}

//
// An odd digit count is half a byte, and the same contract applies.
//
TEST( CoreSessionData, ATruncatedPayloadRowIsReportedAsAMalformedRow)
{
    std::istringstream in( "0\t0\tConsole\tConsole\tSer1\t<bytes>\t414\n");

    EXPECT_THROW( (void) core::readRecording( in), std::runtime_error);
}
