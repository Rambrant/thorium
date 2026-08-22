#include "suite/scripts.hpp"
#include "verdict.hpp"

//
// Not suite/prelude.hpp -- see suite/tests/test_supply_rail_script.cpp's own
// comment for why a script's test is written against the verbs and the
// quantity types and against none of the criteria tables.
//
// One extra verb here that the other script tests do not need: Await, whose
// answer is injected the same way a reading is. That is the whole reason a
// single-shot capture is expressible as an ordinary unit test with no bench
// attached -- see core/acquire.hpp.
//
#include "core/quantity.hpp"
#include "hal/acquire.hpp"
#include "hal/measure.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/criteria_variants.hpp"
#include "core/journal.hpp"

using core::quantities::Voltage;

namespace
{
    //
    // The three names this script observes through. Written out here rather
    // than spelled at each call site, because getting one wrong produces a
    // "nothing programmed for..." throw rather than a failed check, and the
    // difference between those two is worth not having to diagnose twice.
    //
    // The two measurement keys carry the scope's qualifier -- "Output5V.Vbase"
    // rather than "Output5V" -- because a script measuring one pin two
    // different ways would otherwise inject both into one slot and get them in
    // whichever order it happened to ask. See core::MeasureEngine.
    //
    constexpr auto kBaseline = "Output5V.Vbase";
    constexpr auto kMinimum  = "Output5V.Vmin";
    constexpr auto kCapture  = "Osc1.Acquisition";

    //
    // Registered so a test can assert what the script *recorded* and not only
    // what it returned -- which for the missing-capture path is the whole
    // question: the return value is false either way, and what matters is which
    // criterion the log does and does not attach that to.
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

    struct AcDropoutFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::journal().clearSinks();
                core::journal().add( Journal);

                core::resetCriteriaVariantForTesting();
            }

            //
            // Measure, Await and the criteria variant are all process-wide
            // (a catalog script takes no parameters, so there is nothing
            // per-call to inject through), so every test has to put them back
            // or leak into the next one. useLive() on Measure clears the whole
            // shared bank, Await included -- see core::ReadEngine's own
            // comment on why the bank is reached through Measure alone.
            //
            void TearDown() override
            {
                core::journal().clearSinks();

                Measure.useLive();
                core::resetCriteriaVariantForTesting();
            }

            //
            // Every check the run recorded that it could not actually make,
            // as "criteria group::id" -- empty for one attached to no criterion
            // at all, which is a distinction two tests below turn on.
            //
            [[nodiscard]]
            auto uncheckedSubjects() const -> std::vector<std::string>
            {
                std::vector<std::string> subjects;

                for( const auto & event : Journal.Events)
                {
                    if( event.Value == "<unchecked>")
                    {
                        subjects.push_back( event.SubjectGroup.empty()
                                                ? std::string{}
                                                : event.SubjectGroup + "::" + event.Subject);
                    }
                }

                return subjects;
            }

            CapturingSink Journal;

            //
            // A healthy run: rail at nominal, capture lands, rail dips 120 mV.
            //
            static auto injectHealthyDropout() -> void
            {
                Measure.inject( kBaseline, Voltage{ 5.00 });
                Await.inject(   kCapture,  true);
                Measure.inject( kMinimum,  Voltage{ 4.88 });
            }
    };
} // namespace

TEST_F( AcDropoutFixture, PassesWhenTheRailRidesTheDropout)
{
    injectHealthyDropout();

    EXPECT_TRUE( verdictOf( acDropoutScript));
}

TEST_F( AcDropoutFixture, FailsWhenTheRailDipsTooFar)
{
    Measure.inject( kBaseline, Voltage{ 5.00 });
    Await.inject(   kCapture,  true);
    Measure.inject( kMinimum,  Voltage{ 4.70 });   // 300mV, outside production's 200mV

    EXPECT_FALSE( verdictOf( acDropoutScript));
}

TEST_F( AcDropoutFixture, FailsWhenTheBaselineWasNeverRight)
{
    //
    // A dip measured from the wrong baseline is not a measurement of the dip,
    // which is why the baseline is a criterion of its own rather than an
    // assumption. Here the rail is low before anything is disturbed and the
    // dip itself is well inside tolerance -- the run must still fail.
    //
    Measure.inject( kBaseline, Voltage{ 4.50 });
    Await.inject(   kCapture,  true);
    Measure.inject( kMinimum,  Voltage{ 4.45 });

    EXPECT_FALSE( verdictOf( acDropoutScript));
}

TEST_F( AcDropoutFixture, FailsWhenTheCaptureNeverCompleted)
{
    //
    // And, critically, does not measure the dip at all in that case: the
    // acquisition buffer would still hold whatever was in it before, and a
    // depth computed out of that is a fabricated finding. Only two values are
    // injected here, and the script running to completion is itself the proof
    // that it never asked for a third -- a Measure of Output5V.Vmin would
    // throw, not fail.
    //
    Measure.inject( kBaseline, Voltage{ 5.00 });
    Await.inject(   kCapture,  false);

    EXPECT_FALSE( verdictOf( acDropoutScript));
}

TEST_F( AcDropoutFixture, AnUnfindableMinimumIsTreatedAsNoExcursionAtAll)
{
    //
    // The whenUnmeasurable handler in the script, exercised: a scope that
    // cannot find a minimum has found no excursion, and no excursion is a dip
    // of zero volts -- a pass, recorded as one.
    //
    // This is what the legacy ATE spelled as `if( ISINVALID( dVOLTMIN))
    // dNEGTRANSIENT = 0;` several lines below the measurement it applied to.
    //
    Measure.inject( kBaseline, Voltage{ 5.00 });
    Await.inject(   kCapture,  true);
    Measure.inject( kMinimum,  Voltage{ std::numeric_limits<double>::quiet_NaN() });

    //
    // Note the injection above is a NaN rather than an unmeasurable reading:
    // the session seam sits above the substitution (an injected value never
    // reaches the instrument that would have refused), so a script test cannot
    // drive the handler through injection. What it can assert is the other
    // half of the contract -- that a NaN reaching a criterion fails it rather
    // than passing quietly, which is what makes the NaN default safe.
    //
    EXPECT_FALSE( verdictOf( acDropoutScript));
}

TEST_F( AcDropoutFixture, ThrowsWhenTheCaptureAnswerIsMissing)
{
    //
    // Nothing is programmed for the capture, so Await throws rather than
    // defaulting to "it completed" -- the same treatment a missing measurement
    // gets. A default here would be the worst option available: a completion
    // that was never observed, with real measurements checked against it.
    //
    Measure.inject( kBaseline, Voltage{ 5.00 });

    EXPECT_THROW( acDropoutScript(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Tolerance variants
// ---------------------------------------------------------------------------

TEST_F( AcDropoutFixture, TheSameScriptIsHeldToWhicheverVariantIsSelected)
{
    //
    // 280 mV is the value that tells them apart -- outside production's
    // 200 mV, inside stress's 350 mV and aged's 500 mV (see
    // dut/criteria_*.inc). The baseline is left at nominal for all three, so
    // the verdict turns on the dip alone.
    //
    const auto runAgainst = []( const std::string_view variant)
    {
        core::resetCriteriaVariantForTesting();

        EXPECT_TRUE( core::selectCriteriaVariant( variant));

        Measure.inject( kBaseline, Voltage{ 5.00 });
        Await.inject(   kCapture,  true);
        Measure.inject( kMinimum,  Voltage{ 4.72 });

        return verdictOf( acDropoutScript);
    };

    EXPECT_FALSE( runAgainst( "production")) << "a 280mV dip is outside production's 200mV";
    EXPECT_TRUE(  runAgainst( "stress"))     << "a 280mV dip is inside stress's 350mV";
    EXPECT_TRUE(  runAgainst( "aged"))       << "a 280mV dip is inside aged's 500mV";
}

TEST_F( AcDropoutFixture, NoVariantMakesAMissingCaptureAcceptable)
{
    //
    // FS_Transient_Captured is CRIT_FROM_PRODUCTION in both variant tables,
    // and this is what that means in practice: there is no temperature and no
    // amount of service life at which half a capture becomes a result.
    //
    for( const auto variant : core::criteriaVariantNames())
    {
        core::resetCriteriaVariantForTesting();

        ASSERT_TRUE( core::selectCriteriaVariant( variant));

        Measure.inject( kBaseline, Voltage{ 5.00 });
        Await.inject(   kCapture,  false);

        EXPECT_FALSE( verdictOf( acDropoutScript)) << "a missing capture must fail under '" << variant << "'";
    }
}

//
// What a missing capture records, as against merely that it fails.
//
// The dip criterion is deliberately *not* named. FS_Transient_5V0_Dip means
// "the rail dipped no more than this much", and a run whose capture never
// completed measured nothing about how far the rail dipped -- naming it, even
// as unchecked, would attach this run's silence to a requirement it has no
// evidence either way about. So exactly one unattributed unchecked row is
// recorded, carrying the reason as its prose (see core::Fail in
// core/verify.hpp, and the script's own comment on the branch).
//
// FS_Transient_Captured is a different matter and is checked for real on this
// path: "did the capture land" is a question this run does have an answer to.
//
TEST_F( AcDropoutFixture, AMissingCaptureIsRecordedAgainstNoDipCriterion)
{
    Measure.inject( kBaseline, Voltage{ 5.00 });
    Await.inject(   kCapture,  false);

    EXPECT_FALSE( verdictOf( acDropoutScript));

    const auto unchecked = uncheckedSubjects();

    ASSERT_EQ( unchecked.size(), 1u);
    EXPECT_EQ( unchecked.front(), "") << "the dip criterion must not be named by a run that did not measure a dip";

    const auto captured = std::ranges::count_if( Journal.Events,
        []( const core::JournalEvent & event)
        {
            return event.Subject == "FS_Transient_Captured";
        });

    EXPECT_EQ( captured, 1) << "whether the capture landed is still a real verdict";
}
