#include "core/session.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    auto liveVoltage( double v) -> std::function<core::QuantityVariant()>
    {
        return [v]() -> core::QuantityVariant { return Voltage{ v }; };
    }
} // namespace

TEST( CoreSession, LiveSessionAlwaysCallsTheLiveRead)
{
    core::LiveSession session;

    const auto value = session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 5.02));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( value).value(), 5.02);
}

TEST( CoreSession, ScriptedSessionStickyValueRepeatsForever)
{
    core::ScriptedSession session;
    session.program( "5VOutput", Voltage{ 5.02 });

    for( int i = 0; i < 3; ++i)
    {
        const auto value = session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 999.0));
        EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( value).value(), 5.02);
    }
}

TEST( CoreSession, ScriptedSessionNeverCallsLiveRead)
{
    core::ScriptedSession session;
    session.program( "5VOutput", Voltage{ 5.02 });

    bool liveReadCalled = false;
    auto explodingLiveRead = [&]() -> core::QuantityVariant { liveReadCalled = true; return Voltage{ 0.0 }; };

    (void)session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, explodingLiveRead);

    EXPECT_FALSE( liveReadCalled);
}

TEST( CoreSession, ScriptedSessionThrowsOnKindMismatch)
{
    core::ScriptedSession session;
    session.program( "5VOutput", Voltage{ 5.02 });

    EXPECT_THROW(
        (void)session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Current, liveVoltage( 0.0)),
        std::runtime_error);
}

TEST( CoreSession, ScriptedSessionThrowsWhenNothingWasProgrammed)
{
    core::ScriptedSession session;

    EXPECT_THROW(
        (void)session.fetch( "Unprogrammed", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0)),
        std::runtime_error);
}

//
// Value sources. These cover the seam itself -- the kind check, and the two
// ends of the exhaustion question -- where test_measure.cpp covers the
// Measure.inject() spellings that produce one.
//
TEST( CoreSession, ScriptedSessionSourceIsConsumedInOrderThenThrows)
{
    core::ScriptedSession session;
    session.program( "5VOutput", core::sourceOf( std::vector{ Voltage{ 5.02 }, Voltage{ 5.03 } }));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 5.02);
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 5.03);

    EXPECT_THROW(
        (void)session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0)),
        std::runtime_error);
}

TEST( CoreSession, ScriptedSessionSourceValuesAreCheckedForTheirKind)
{
    core::ScriptedSession session;
    session.program( "5VOutput", core::sourceOf( std::vector{ Voltage{ 5.02 } }));

    EXPECT_THROW(
        (void)session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Current, liveVoltage( 0.0)),
        std::runtime_error);
}

//
// A source is free to never end -- which is what makes "run this until it
// fails" expressible without deciding up front how many passes that will take.
//
TEST( CoreSession, ScriptedSessionAnEndlessSourceNeverRunsOut)
{
    core::ScriptedSession session;

    double next = 5.0;
    session.program( "5VOutput", [&next]() -> std::optional<core::QuantityVariant>
    {
        return Voltage{ next += 0.01 };
    });

    for( int i = 0; i < 1000; ++i)
    {
        EXPECT_NO_THROW(
            (void)session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0)));
    }
}

//
// program() replacing a source matters because MeasureEngine::inject() is
// built on it: injecting a point twice in one test must not leave the first
// sequence half-consumed underneath the second.
//
TEST( CoreSession, ScriptedSessionProgrammingAPointAgainReplacesItsSource)
{
    core::ScriptedSession session;
    session.program( "5VOutput", core::sourceOf( std::vector{ Voltage{ 1.0 }, Voltage{ 2.0 } }));
    session.program( "5VOutput", core::sourceOf( std::vector{ Voltage{ 9.0 } }));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 9.0);
    EXPECT_THROW(
        (void)session.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0)),
        std::runtime_error);
}

TEST( CoreSession, SwitchableSessionDefaultsToTheConstructedSession)
{
    core::LiveSession       live;
    core::SwitchableSession switchable( live);

    const auto value = switchable.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 5.02));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( value).value(), 5.02);
}

TEST( CoreSession, SwitchableSessionUseSwapsToAnotherSessionAndBack)
{
    core::LiveSession       live;
    core::SwitchableSession switchable( live);
    core::ScriptedSession   scripted;
    scripted.program( "5VOutput", Voltage{ 9.99 });

    switchable.use( scripted);
    const auto scriptedValue = switchable.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 5.02));
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( scriptedValue).value(), 9.99);

    switchable.useDefault();
    const auto liveValue = switchable.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 5.02));
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( liveValue).value(), 5.02);
}

TEST( CoreSession, RecordingSessionLogsEachFetchInOrder)
{
    core::LiveSession     live;
    core::RecordingSession recording( live);

    (void)recording.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 5.02));
    (void)recording.fetch( "3V3Output", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 3.29));

    ASSERT_EQ( recording.samples().size(), 2u);
    EXPECT_EQ( recording.samples()[0].mSequence, 0u);
    EXPECT_EQ( recording.samples()[0].mPointName, "5VOutput");
    EXPECT_EQ( recording.samples()[1].mSequence, 1u);
    EXPECT_EQ( recording.samples()[1].mPointName, "3V3Output");
}

TEST( CoreSession, RecordingDumpedToFileReplaysInOrderThenThrowsWhenExhausted)
{
    core::LiveSession      live;
    core::RecordingSession recording( live);

    // Two readings of the same point -- e.g. before and after a step change --
    // so playback has to preserve order rather than collapsing to one value.
    (void)recording.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 4.98));
    (void)recording.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 5.02));

    const auto path = std::filesystem::temp_directory_path() / "thorium_test_recording.tsv";

    {
        std::ofstream out( path);
        core::writeRecording( out, recording.samples());
    }

    auto playback = core::ScriptedSession::loadFromFile( path.string());

    const auto first  = playback.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0));
    const auto second = playback.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( first).value(),  4.98);
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( second).value(), 5.02);

    EXPECT_THROW(
        (void)playback.fetch( "5VOutput", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0)),
        std::runtime_error);

    std::remove( path.string().c_str());
}
