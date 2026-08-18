#include "core/session.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <variant>

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

TEST( CoreSession, ScriptedSessionErrorMessageContainsPointName)
{
    core::ScriptedSession session;

    try
    {
        (void)session.fetch( "MissingPoint", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0));
        FAIL() << "Expected std::runtime_error";
    }
    catch( const std::runtime_error & e)
    {
        const std::string message( e.what());
        EXPECT_TRUE( message.find( "MissingPoint") != std::string::npos) << "Error message: " << message;
        EXPECT_TRUE( message.find( "nothing programmed") != std::string::npos) << "Error message: " << message;
    }
}

TEST( CoreSession, ScriptedSessionErrorMessageOnExhaustionMentionsPointName)
{
    core::ScriptedSession session;
    session.program( "SingleValue", core::sourceOf( std::vector{ Voltage{ 5.0 } }));

    (void)session.fetch( "SingleValue", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0));

    try
    {
        (void)session.fetch( "SingleValue", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0));
        FAIL() << "Expected std::runtime_error";
    }
    catch( const std::runtime_error & e)
    {
        const std::string message( e.what());
        EXPECT_TRUE( message.find( "SingleValue") != std::string::npos) << "Error message: " << message;
        EXPECT_TRUE( message.find( "no programmed value left") != std::string::npos) << "Error message: " << message;
    }
}

TEST( CoreSession, ConstantSourceYieldsSameValueForever)
{
    auto source = core::constantSource( Voltage{ 42.0 });

    for( int i = 0; i < 10; ++i)
    {
        const auto value = source();
        ASSERT_TRUE( value.has_value());
        EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( *value).value(), 42.0);
    }
}

TEST( CoreSession, RecordingSessionDelegatesToInnerSession)
{
    core::ScriptedSession scripted;
    scripted.program( "TestPoint", Voltage{ 7.77 });

    core::RecordingSession recording( scripted);

    const auto value = recording.fetch( "TestPoint", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( value).value(), 7.77);
    ASSERT_EQ( recording.samples().size(), 1u);
    EXPECT_EQ( recording.samples()[0].mPointName, "TestPoint");
}

TEST( CoreSession, RecordingSessionCapturesInstrumentId)
{
    core::LiveSession     live;
    core::RecordingSession recording( live);

    (void)recording.fetch( "Point", "CustomDmm", core::QuantityKind::Voltage, liveVoltage( 1.0));

    ASSERT_EQ( recording.samples().size(), 1u);
    EXPECT_EQ( recording.samples()[0].mInstrumentId, "CustomDmm");
}

//
// A recorded sample's kind comes from the value it actually holds, and cannot
// be stated separately from it.
//
// This test used to assert the opposite, and it is worth saying why it changed
// rather than quietly rewriting it: it passed a declared kind of Current
// alongside a live read that produced a Voltage, and asserted the sample came
// back saying Current. That was a real state the old two-field RecordedSample
// could be in -- a row whose kind and value disagreed -- and exactly the defect
// core::MeasureEngine's own comment describes on the logging path. There is now
// no second field to disagree, so the question the test can ask is the stronger
// one: what the sample says it is, is what it holds.
//
TEST( CoreSession, ARecordedSamplesKindComesFromItsValue)
{
    core::LiveSession      live;
    core::RecordingSession recording( live);

    (void)recording.fetch( "Point", "Dmm1", core::QuantityKind::Current, liveVoltage( 1.0));

    ASSERT_EQ( recording.samples().size(), 1u);

    const auto & value = std::get<core::QuantityVariant>( recording.samples()[0].mValue);

    EXPECT_EQ( static_cast<core::QuantityKind>( value.index()), core::QuantityKind::Voltage);
}

TEST( CoreSession, SwitchableSessionCanSwitchMultipleTimes)
{
    core::LiveSession       live;
    core::SwitchableSession switchable( live);
    core::ScriptedSession   scripted1, scripted2;

    scripted1.program( "Point", Voltage{ 1.11 });
    scripted2.program( "Point", Voltage{ 2.22 });

    switchable.use( scripted1);
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        switchable.fetch( "Point", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 1.11);

    switchable.use( scripted2);
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        switchable.fetch( "Point", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 2.22);

    switchable.useDefault();
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        switchable.fetch( "Point", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 3.33))).value(), 3.33);
}

TEST( CoreSession, ScriptedSessionLoadFromNonExistentFileThrows)
{
    EXPECT_THROW(
        core::ScriptedSession::loadFromFile( "nonexistent_file_that_does_not_exist.tsv"),
        std::runtime_error);
}

TEST( CoreSession, ScriptedSessionLoadFromFilePreservesMultiplePoints)
{
    const auto path = std::filesystem::temp_directory_path() / "thorium_test_multi_point.tsv";

    {
        std::ofstream out( path);
        out << "0\t1000\tPointA\tDmm1\tVoltage\t1.0\n";
        out << "1\t1001\tPointB\tDmm1\tCurrent\t2.0\n";
        out << "2\t1002\tPointA\tDmm1\tVoltage\t3.0\n";
    }

    auto session = core::ScriptedSession::loadFromFile( path.string());

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        session.fetch( "PointA", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 1.0);
    EXPECT_DOUBLE_EQ( core::asQuantity<Current>(
        session.fetch( "PointB", "Dmm1", core::QuantityKind::Current, liveVoltage( 0.0))).value(), 2.0);
    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>(
        session.fetch( "PointA", "Dmm1", core::QuantityKind::Voltage, liveVoltage( 0.0))).value(), 3.0);

    std::remove( path.string().c_str());
}

TEST( CoreSession, ScriptedSessionKindMismatchErrorContainsBothKinds)
{
    core::ScriptedSession session;
    session.program( "Point", Voltage{ 1.0 });

    try
    {
        (void)session.fetch( "Point", "Dmm1", core::QuantityKind::Current, liveVoltage( 0.0));
        FAIL() << "Expected std::runtime_error";
    }
    catch( const std::runtime_error & e)
    {
        const std::string message( e.what());
        EXPECT_TRUE( message.find( "Voltage") != std::string::npos) << "Error message: " << message;
        EXPECT_TRUE( message.find( "Current") != std::string::npos) << "Error message: " << message;
    }
}
