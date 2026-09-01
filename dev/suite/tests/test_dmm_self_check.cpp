#include "dev/suite/scripts.hpp"

//
// suite/tests/verdict.hpp, reached repo-root-relative rather than copied: what
// it wraps is core::Journal's begin/endTest bracket, which is framework
// behaviour and identical for every deployment. Its home under the bench
// suite's tests/ is now slightly wrong -- it belongs somewhere both suites can
// claim -- and moving it is a separate change from adding this deployment.
//
#include "suite/tests/verdict.hpp"

//
// dmmSelfCheck, with its reading injected -- the dev deployment's analogue of
// suite/tests/test_supply_rail_script.cpp, and the same shape for the same
// reasons. See that file's own comment on why this is not the prelude, and
// verdict.hpp on why a script's verdict has to be taken through a journal
// bracket.
//
// The key is what makes this file worth reading next to the bench's. A routed
// reading is keyed by its DUT point -- "Output5V" -- because that is what the
// measurement is *of*. A readback has no point, so it keys as
// "<instrument>.<quantity>" (see core::MeasureEngine's point-free operator()).
// Nothing has to be looked up to find that out: run_scripts --skeleton prints
// exactly the keys the selection asked for, which is the same file --inject
// consumes.
//
#include "core/quantities/quantity.hpp"
#include "hal/verbs/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Capacitance;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    //
    // Restores live routing after every test, for the reason the bench suite's
    // fixtures do: Measure is one rig-wide object, because a catalog script
    // takes no parameters and so has nothing to be handed a session through.
    // A test that injected and did not clean up would hand its leftovers to
    // whichever test ran next.
    //
    // No criteria-variant reset, unlike the bench's fixtures -- this deployment
    // has exactly one variant (see dev/dut/criteria_production.inc), so there
    // is no process-wide selection for a test to disturb. If a second variant
    // is ever added here, this fixture grows the same two lines that one has.
    //
    struct DmmSelfCheckFixture : ::testing::Test
    {
        protected:

            void TearDown() override
            {
                Measure.useLive();
            }
    };
} // namespace

//
// Both readings, every time. The script takes two now, and a test that queued
// only one would not fail on the missing check -- it would throw out of
// Measure, which reads as a broken test rather than as the thing under test
// (see ThrowsWhenAReadingIsMissing below, which asserts that on purpose).
//
// The capacitance key is "Dmm1.Capacitance" for the same reason the voltage one
// is "Dmm1.Voltage": a readback has no DUT point to key by, so it keys as
// "<instrument>.<quantity>" -- and the quantity half is QuantityKind's own
// enumerator spelling, which is why adding a unit to core is all it took for
// this key to exist. run_scripts --skeleton prints exactly these.
//
namespace
{
    constexpr auto kNominalReference = 5.01_V;
    constexpr auto kNominalBulk      = 470.0_uF;
} // namespace

TEST_F( DmmSelfCheckFixture, PassesWhenBothReferencesReadNominal)
{
    Measure.inject( "Dmm1.Voltage",     kNominalReference);
    Measure.inject( "Dmm1.Capacitance", kNominalBulk);

    EXPECT_TRUE( verdictOf( dmmSelfCheck));
}

TEST_F( DmmSelfCheckFixture, FailsWhenTheReferenceIsOutOfTolerance)
{
    Measure.inject( "Dmm1.Voltage",     Voltage{ 5.20 });   // outside +/-50mV
    Measure.inject( "Dmm1.Capacitance", kNominalBulk);

    EXPECT_FALSE( verdictOf( dmmSelfCheck));
}

//
// The capacitor's own row, failed on its own -- with the voltage left nominal,
// so the verdict turns on the reading this deployment gained with the
// EDU34450A and on nothing else.
//
TEST_F( DmmSelfCheckFixture, FailsWhenTheReferenceCapacitorIsOutOfTolerance)
{
    Measure.inject( "Dmm1.Voltage",     kNominalReference);
    Measure.inject( "Dmm1.Capacitance", Capacitance{ 330.0e-6 });   // outside +/-47uF

    EXPECT_FALSE( verdictOf( dmmSelfCheck));
}

//
// The boundary, in both directions, on both rows. Worth having explicitly
// rather than trusting the tests above: an epsilon that had drifted by a factor
// of ten would still pass the nominal case and fail the far-out case, and only
// a check at the edge notices.
//
// The capacitance edge is also where a wrong literal scale would show. 47_uF
// either side of 470_uF is 423..517 uF; if _uF were off by a thousand in either
// direction, both of these would land on the same side.
//
TEST_F( DmmSelfCheckFixture, HoldsTheToleranceAtItsEdge)
{
    Measure.inject( "Dmm1.Voltage",     5.04_V);
    Measure.inject( "Dmm1.Capacitance", kNominalBulk);
    EXPECT_TRUE( verdictOf( dmmSelfCheck));

    Measure.inject( "Dmm1.Voltage",     5.06_V);
    Measure.inject( "Dmm1.Capacitance", kNominalBulk);
    EXPECT_FALSE( verdictOf( dmmSelfCheck));

    Measure.inject( "Dmm1.Voltage",     kNominalReference);
    Measure.inject( "Dmm1.Capacitance", 516.0_uF);
    EXPECT_TRUE( verdictOf( dmmSelfCheck));

    Measure.inject( "Dmm1.Voltage",     kNominalReference);
    Measure.inject( "Dmm1.Capacitance", 518.0_uF);
    EXPECT_FALSE( verdictOf( dmmSelfCheck));
}

//
// A reading the script takes and the test never queued is a throw, not a quiet
// failure -- the same rule the bench suite's ThrowsWhenAPointIsMissing asserts,
// and worth restating here because the script's second reading is new enough
// that "it passed" would otherwise be an easy thing to believe about a run that
// never took it.
//
TEST_F( DmmSelfCheckFixture, ThrowsWhenAReadingIsMissing)
{
    Measure.inject( "Dmm1.Voltage", kNominalReference);
    // Dmm1.Capacitance not provided at all.

    EXPECT_THROW( dmmSelfCheck(), std::runtime_error);
}

//
// A script that recorded no check cannot pass -- the rule lives in
// core::Journal::endTest rather than in the runner, so it reaches a test that
// calls a script directly (see verdict.hpp). Asserted here because this
// deployment is where somebody will first be tempted to write a script that
// only measures, to watch a driver answer, and that should come back red rather
// than green.
//
TEST_F( DmmSelfCheckFixture, AScriptThatVerifiesNothingDoesNotPass)
{
    EXPECT_FALSE( verdictOf( [] {}));
}
