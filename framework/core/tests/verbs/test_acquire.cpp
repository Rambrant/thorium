#include "core/verbs/acquire.hpp"

#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/session/recording.hpp"
#include "core/session/session.hpp"

using namespace core::literals;
using namespace core::quantities;

//
// A minimal armable instrument, satisfying exactly what core::ArmEngine and
// core::AwaitEngine need and nothing more: a builder with a config(), an ADL
// armDriver/awaitDriver pair over that config, and a describeConfig for the
// log. hal::keysight_dsox1202g::DSOX1202G is one concrete type that happens to satisfy the same
// shape; core knows nothing about it.
//
// Deliberately not a scope. The verbs are generic -- "arm a capture, let the
// event happen, find out whether it landed" is equally a transient recorder,
// a digitizer or a counter with an armed gate -- and a mock shaped like an
// oscilloscope would have quietly made the tests below read as a claim about
// oscilloscopes.
//
//
// Unnamed namespace, so this file's mock is this file's own -- see
// test_bench.cpp's mock for why that is a correctness requirement in this
// binary rather than a matter of taste.
//
namespace
{
namespace mock
{
    class Recorder;

    struct CaptureConfig
    {
        Recorder &                             Instrument;
        std::optional<core::quantities::Time>  Timeout;
    };

    class CaptureBuilder
    {
        public:
            using Config = CaptureConfig;

            explicit CaptureBuilder( Recorder & instrument) : mConfig{ instrument, std::nullopt } {}

            [[nodiscard]]
            auto timeout( const core::quantities::Time value) const -> CaptureBuilder
            {
                auto copy = *this;
                copy.mConfig.Timeout = value;
                return copy;
            }

            [[nodiscard]] auto config() const -> const Config & { return mConfig; }

        private:
            Config mConfig;
    };

    class Recorder
    {
        public:
            [[nodiscard]] auto capture() -> CaptureBuilder { return CaptureBuilder{ *this }; }

            auto arm() -> void { mArmed = true; ++mArmCount; }

            [[nodiscard]] auto poll() -> bool
            {
                const auto completed = mArmed && mCompletes;

                mArmed = false;
                ++mPollCount;

                return completed;
            }

            auto setCompletes( const bool completes) -> void { mCompletes = completes; }

            [[nodiscard]] auto armCount() const  -> int { return mArmCount; }
            [[nodiscard]] auto pollCount() const -> int { return mPollCount; }

        private:
            bool mArmed{ false };
            bool mCompletes{ true };
            int  mArmCount{ 0 };
            int  mPollCount{ 0 };
    };

    inline auto armDriver( const CaptureConfig & config) -> void
    {
        config.Instrument.arm();
    }

    [[nodiscard]]
    inline auto awaitDriver( const CaptureConfig & config) -> bool
    {
        return config.Instrument.poll();
    }

    inline auto describeConfig( const CaptureConfig & ) -> core::SourceDescription
    {
        return core::SourceDescription{ "Rec1", "capture" };
    }
} // namespace mock
} // namespace

namespace
{
    struct AcquireFixture : ::testing::Test
    {
        mock::Recorder     rec1;
        core::SessionBank  sessions;
        core::ArmEngine    Arm;
        core::AwaitEngine  Await{ sessions };
    };
} // namespace

TEST_F( AcquireFixture, ArmingGoesStraightToTheInstrument)
{
    //
    // Arm is stimulus, not observation: it does not go through the session
    // bank, so there is nothing for a replay to reproduce about it and
    // nothing for a test to inject. Same split Write and Read already draw.
    //
    Arm( rec1.capture().timeout( 2_s));

    EXPECT_EQ( rec1.armCount(),  1);
    EXPECT_EQ( rec1.pollCount(), 0);
}

TEST_F( AcquireFixture, AwaitReportsWhetherTheArmedCaptureCompleted)
{
    Arm( rec1.capture());

    EXPECT_TRUE( Await( rec1.capture()));
}

TEST_F( AcquireFixture, ACaptureThatNeverTriggersComesBackFalseRatherThanThrowing)
{
    //
    // "The transient never arrived" is a finding about the DUT, not an error
    // in the bench -- and it may well be the thing the test was written to
    // check. So it comes back as a value a criterion can be pointed at.
    //
    rec1.setCompletes( false);

    Arm( rec1.capture());

    EXPECT_FALSE( Await( rec1.capture()));
}

TEST_F( AcquireFixture, AnInjectedAnswerBypassesTheInstrumentEntirely)
{
    //
    // What makes a script built around a single-shot capture testable with no
    // bench attached -- the flag-side twin of Measure.inject and Read.inject.
    //
    Await.inject( "Rec1.Acquisition", true);
    rec1.setCompletes( false);   // would fail this test if it were ever polled

    EXPECT_TRUE( Await( rec1.capture()));
    EXPECT_EQ( rec1.pollCount(), 0);
}

TEST_F( AcquireFixture, InjectedAnswersCanBeASequence)
{
    // For a script that captures more than once, or a test that wants the
    // second capture to be the one that times out.
    Await.inject( "Rec1.Acquisition", { true, false });

    EXPECT_TRUE(  Await( rec1.capture()));
    EXPECT_FALSE( Await( rec1.capture()));
}

TEST_F( AcquireFixture, RunningPastTheEndOfAnInjectedSequenceThrows)
{
    //
    // Rather than silently repeating the last answer, which would be the
    // worst option available: a "completed" that was never observed, with
    // real measurements checked against it.
    //
    Await.inject( "Rec1.Acquisition", { true });

    EXPECT_TRUE( Await( rec1.capture()));
    EXPECT_THROW( (void)Await( rec1.capture()), std::runtime_error);
}

TEST_F( AcquireFixture, AnAwaitIsRecordedInOrderAlongsideTheOtherObservations)
{
    //
    // The reason a completion flag is recorded at all: an Await sits in the
    // middle of a sequence whose meaning depends on it -- arm, cause the
    // event, wait, then measure the transient the wait is the evidence for.
    // Two independently sequenced recordings could not reconstruct that.
    //
    sessions.startRecording();

    Arm( rec1.capture());
    (void)Await( rec1.capture());

    ASSERT_EQ( sessions.samples().size(), 1u);
    EXPECT_EQ( sessions.samples().front().mPointName,    "Rec1.Acquisition");
    EXPECT_EQ( sessions.samples().front().mInstrumentId, "Rec1");
    ASSERT_TRUE( std::holds_alternative<bool>( sessions.samples().front().mValue));
    EXPECT_TRUE( std::get<bool>( sessions.samples().front().mValue));
}

TEST_F( AcquireFixture, ARecordedCaptureReplaysAsTheAnswerThatWasRecorded)
{
    //
    // End to end, through the file format: record a run in which the capture
    // timed out, then replay it against an instrument that would now say the
    // opposite. The replay has to answer from the file.
    //
    rec1.setCompletes( false);
    sessions.startRecording();

    Arm( rec1.capture());
    EXPECT_FALSE( Await( rec1.capture()));

    std::ostringstream written;
    sessions.dump( written);

    std::istringstream read( written.str());
    const auto         replayed = core::readRecording( read);

    ASSERT_EQ( replayed.size(), 1u);
    ASSERT_TRUE( std::holds_alternative<bool>( replayed.front().mValue));
    EXPECT_FALSE( std::get<bool>( replayed.front().mValue));

    core::SessionBank  replayBank;
    core::AwaitEngine  ReplayedAwait{ replayBank };

    replayBank.injectFlag( "Rec1.Acquisition", false);
    rec1.setCompletes( true);   // the bench now disagrees with the file

    EXPECT_FALSE( ReplayedAwait( rec1.capture()));
}

//
// ---------------------------------------------------------------------
// The recording format itself
// ---------------------------------------------------------------------
//

TEST( RecordingFlags, AFlagRowRoundTrips)
{
    const std::vector<core::RecordedSample> samples{
        { 0, 1'700'000'000'000, "AcDropout", "Osc1.Acquisition", "Osc1", true },
        { 1, 1'700'000'000'050, "AcDropout", "Osc2.Acquisition", "Osc2", false }
    };

    std::ostringstream out;
    core::writeRecording( out, samples);

    std::istringstream in( out.str());
    const auto         readBack = core::readRecording( in);

    ASSERT_EQ( readBack.size(), 2u);
    EXPECT_TRUE(  std::get<bool>( readBack[ 0].mValue));
    EXPECT_FALSE( std::get<bool>( readBack[ 1].mValue));
}

TEST( RecordingFlags, AFlagRowIsSelfDescribingAndCannotCollideWithAUnit)
{
    // The kind column says which alternative the value column is in, spelled
    // with angle brackets so no unit name added to core/quantities/quantity_kind.hpp can
    // ever take the token -- see core::kFlagKind.
    const std::vector<core::RecordedSample> samples{ { 0, 0, "AcDropout", "Osc1.Acquisition", "Osc1", true } };

    std::ostringstream out;
    core::writeRecording( out, samples);

    EXPECT_NE( out.str().find( "<flag>"), std::string::npos);
    EXPECT_EQ( out.str(), "0\t0\tAcDropout\tOsc1.Acquisition\tOsc1\t<flag>\t1\n");
}

TEST( RecordingFlags, AnythingButOneOrZeroInAFlagColumnIsACorruptRow)
{
    //
    // Not read generously as truthy or falsy: a replay that quietly read
    // "yes" as false would report an acquisition that never completed, and
    // the checks beneath it would then be failing for a reason the file does
    // not contain.
    //
    std::istringstream in( "0\t0\tAcDropout\tOsc1.Acquisition\tOsc1\t<flag>\tyes\n");

    EXPECT_THROW( (void)core::readRecording( in), std::runtime_error);
}
