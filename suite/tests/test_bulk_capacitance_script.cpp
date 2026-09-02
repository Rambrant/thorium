#include "suite/scripts.hpp"
#include "verdict.hpp"

//
// Not suite/prelude.hpp: these tests are not scripts. They call one and inject
// its readings by key -- see test_supply_rail_script.cpp's own comment on that
// split, and on why the fixture has to restore both the injection bank and the
// criteria variant.
//
#include "core/quantities/quantity.hpp"
#include "hal/verbs/measure.hpp"

#include <gtest/gtest.h>

#include "core/criteria/criteria_variants.hpp"

using namespace core::literals;

using core::quantities::Capacitance;
using core::quantities::Voltage;

//
// The keys are "BatterySupply.Residual" and "BatterySupply.Bulk", not
// "BatterySupply" twice, and that is the one thing about this script a test has
// to know before it can drive it at all.
//
// A routed reading keys by its DUT point, and this script takes two readings of
// two different quantities at one pin -- which core::MeasureEngine's own comment
// names as the collision it deliberately does not fix, since folding
// QuantityKind into the key would rename every key in every existing recording.
// The remedy it points at is a qualifier, and the script sets one on each
// reading. Without them both would land in one session slot and these tests
// would get whichever value happened to be queued first.
//
namespace
{
    struct BulkCapacitanceFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::resetCriteriaVariantForTesting();
            }

            void TearDown() override
            {
                Measure.useLive();
                core::resetCriteriaVariantForTesting();
            }

            //
            // The ordinary case, as one call: a rail that has discharged and a
            // capacitor at nominal. Every test below is this with one value
            // moved, which is what keeps each one about the thing it is named
            // for.
            //
            static auto inject( const Voltage residual, const Capacitance bulk) -> void
            {
                Measure.inject( "BatterySupply.Residual", residual);
                Measure.inject( "BatterySupply.Bulk",     bulk);
            }
    };
} // namespace

TEST_F( BulkCapacitanceFixture, PassesWhenTheRailIsDeadAndTheCapacitorIsInTolerance)
{
    inject( 20.0_mV, 980.0_uF);

    EXPECT_TRUE( verdictOf( bulkCapacitanceScript));
}

TEST_F( BulkCapacitanceFixture, FailsWhenTheCapacitorIsBelowTolerance)
{
    // 640 uF is the fault this test exists for: an electrolytic that has dried
    // out. It is inside the aged variant's band and outside production's, which
    // is asserted further down -- here the point is only that production fails
    // it.
    inject( 20.0_mV, 640.0_uF);

    EXPECT_FALSE( verdictOf( bulkCapacitanceScript));
}

TEST_F( BulkCapacitanceFixture, FailsWhenTheCapacitorIsAboveTolerance)
{
    //
    // The upper bound is a real check rather than symmetry: a reading well
    // above nominal is what a shorted node or a second capacitor across the
    // pin looks like, and a criterion that only had a floor would pass both.
    //
    inject( 20.0_mV, 2200.0_uF);

    EXPECT_FALSE( verdictOf( bulkCapacitanceScript));
}

//
// The precondition, and the whole reason this script has two readings rather
// than one. A rail that has not discharged gives a capacitance reading that is
// plausible, low, and about nothing -- so the script must not take it.
//
TEST_F( BulkCapacitanceFixture, FailsWithoutMeasuringWhenTheRailHasNotDischarged)
{
    //
    // A healthy capacitor queued behind a live rail. If the script ever stopped
    // guarding the second reading, this test would start passing -- which is
    // exactly the regression worth catching, and is why the good value is here
    // rather than a bad one that would fail either way.
    //
    inject( 24.0_V, 980.0_uF);

    EXPECT_FALSE( verdictOf( bulkCapacitanceScript));
}

//
// And the capacitance reading is genuinely not taken, not merely not checked.
// Nothing consumes "BatterySupply.Bulk" above, so the value queued for it is
// still there afterwards -- injected values are consumed by the read that asks
// for them (see core::ScriptedSession). A script that measured and then declined
// to use the result would leave nothing queued, and would also have put that
// number in the report.
//
TEST_F( BulkCapacitanceFixture, TheUnusedCapacitanceReadingIsStillQueuedAfterwards)
{
    inject( 24.0_V, 980.0_uF);

    EXPECT_FALSE( verdictOf( bulkCapacitanceScript));

    //
    // Only the residual is re-queued. If the first run had consumed the
    // capacitance value, this second run would throw on a missing reading
    // rather than reaching its verdict.
    //
    Measure.inject( "BatterySupply.Residual", 20.0_mV);

    EXPECT_TRUE( verdictOf( bulkCapacitanceScript));
}

TEST_F( BulkCapacitanceFixture, ThrowsWhenAReadingIsMissing)
{
    Measure.inject( "BatterySupply.Residual", 20.0_mV);
    // BatterySupply.Bulk not provided at all -- nothing is queued for it, so
    // Measure throws rather than silently treating the missing reading as a
    // failed check.

    EXPECT_THROW( bulkCapacitanceScript(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Tolerance variants
// ---------------------------------------------------------------------------

//
// The one criterion in dut/criteria_*.inc whose widening is a datasheet curve
// rather than a placeholder, exercised on the value that tells the three tables
// apart.
//
// 700 uF is outside production's 800..1200 uF, inside stress's 670..1260 and
// inside aged's 640..1200 -- so the verdict turns on the variant alone. An
// electrolytic losing capacitance when it is cold and when it is old is the
// textbook reason a suite has more than one tolerance table, and this is the
// row where that is true of the numbers as well as of the comments.
//
TEST_F( BulkCapacitanceFixture, TheSameScriptIsHeldToWhicheverVariantIsSelected)
{
    const auto runAgainst = []( const std::string_view variant)
    {
        core::resetCriteriaVariantForTesting();

        EXPECT_TRUE( core::selectCriteriaVariant( variant));

        inject( 20.0_mV, 700.0_uF);

        return verdictOf( bulkCapacitanceScript);
    };

    EXPECT_FALSE( runAgainst( "production")) << "700 uF is below production's 800 uF floor";
    EXPECT_TRUE(  runAgainst( "stress"))     << "700 uF is inside stress's 670 uF floor";
    EXPECT_TRUE(  runAgainst( "aged"))       << "700 uF is inside aged's 640 uF floor";
}

//
// No variant widens the precondition, and it is not the kind of thing that
// could: a node is discharged or it is not, and there is no temperature or
// service life at which a charged one becomes measurable. Both derived tables
// take that row via CRIT_FROM_MASTER so the fact lives in one place -- the
// same treatment FS_Transient_Captured gets.
//
TEST_F( BulkCapacitanceFixture, NoVariantAcceptsAReadingTakenOnALiveRail)
{
    for( const auto variant : core::criteriaVariantNames())
    {
        core::resetCriteriaVariantForTesting();

        ASSERT_TRUE( core::selectCriteriaVariant( variant));

        inject( 24.0_V, 980.0_uF);

        EXPECT_FALSE( verdictOf( bulkCapacitanceScript))
            << "a live rail must fail the precondition under '" << variant << "'";
    }
}
