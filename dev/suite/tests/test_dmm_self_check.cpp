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
#include "core/quantity.hpp"
#include "hal/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

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

TEST_F( DmmSelfCheckFixture, PassesWhenTheReferenceReadsNominal)
{
    Measure.inject( "Dmm1.Voltage", Voltage{ 5.01 });

    EXPECT_TRUE( verdictOf( dmmSelfCheck));
}

TEST_F( DmmSelfCheckFixture, FailsWhenTheReferenceIsOutOfTolerance)
{
    Measure.inject( "Dmm1.Voltage", Voltage{ 5.20 });   // outside +/-50mV

    EXPECT_FALSE( verdictOf( dmmSelfCheck));
}

//
// The boundary, in both directions. Worth having explicitly rather than
// trusting the two above: an epsilon that had drifted by a factor of ten would
// still pass the nominal case and fail the 5.20 case, and only a check at the
// edge notices.
//
TEST_F( DmmSelfCheckFixture, HoldsTheToleranceAtItsEdge)
{
    Measure.inject( "Dmm1.Voltage", Voltage{ 5.04 });
    EXPECT_TRUE( verdictOf( dmmSelfCheck));

    Measure.inject( "Dmm1.Voltage", Voltage{ 5.06 });
    EXPECT_FALSE( verdictOf( dmmSelfCheck));
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
