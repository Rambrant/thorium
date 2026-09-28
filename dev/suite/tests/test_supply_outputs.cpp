#include "dev/suite/scripts.hpp"

//
// The SupplyOutputs scripts, with their readbacks injected and the bench
// detached.
//
// Detached, which the meter's tests do not need and these do. Those scripts
// only read, and an injected reading never reaches the instrument. These
// Apply and Remove as well, and on this desk the supply's rows are Lan, not
// Simulated (see dev/rig/instrument.inc) -- so an attached Apply here would
// open a socket to dev-psu from a unit test. Detaching is how the runner's
// --inject keeps a run off the hardware (see core/session/bench.hpp), and it
// is the same thing here: every Apply is still posted to the journal, and
// none of them goes anywhere.
//
// Keys are "<instrument>.<quantity>", as for the meter: DcP5.Voltage is read
// three times -- low setpoint, high setpoint, after Remove -- so it is
// injected as a sequence in that order.
//
#include "suite/tests/verdict.hpp"

#include "core/quantities/quantity.hpp"
#include "core/session/bench.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Current;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    struct SupplyOutputsFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::bench().detach();
            }

            void TearDown() override
            {
                Measure.useLive();

                core::bench().attach();
            }
    };

    //
    // One healthy output, as the three readbacks and one current its script
    // takes: low, high, off -- and nothing drawn.
    //
    auto injectHealthy( const std::string_view id, const Voltage low, const Voltage high) -> void
    {
        Measure.inject( std::string( id) + ".Voltage", { low, high, 0.0_V });
        Measure.inject( std::string( id) + ".Current", 0.0_mA);
    }
} // namespace

TEST_F( SupplyOutputsFixture, EachOutputPassesWhenItFollowsItsSetpointsAndTurnsOff)
{
    injectHealthy( "DcP5", 1.0_V, 5.0_V);
    EXPECT_TRUE( verdictOf( psuOutput1Check)) << "psuOutput1Check";

    injectHealthy( "DcP6", 5.0_V, 24.0_V);
    EXPECT_TRUE( verdictOf( psuOutput2Check)) << "psuOutput2Check";

    injectHealthy( "DcP7", 5.0_V, 24.0_V);
    EXPECT_TRUE( verdictOf( psuOutput3Check)) << "psuOutput3Check";
}

//
// Readbacks a few counts either side of the setpoint, which is what a real
// supply returns. Asserted both ways for the off reading in particular: an
// open output reading -3 mV is healthy, and a criterion written as LT( ...)
// would have passed it for the wrong reason and a GT( ...) would have failed it.
//
TEST_F( SupplyOutputsFixture, SmallReadbackErrorsEitherSideStillPass)
{
    Measure.inject( "DcP5.Voltage", { 1.012_V, 4.985_V, Voltage{ -0.003 } });
    Measure.inject( "DcP5.Current", Current{ -0.001 });

    EXPECT_TRUE( verdictOf( psuOutput1Check));
}

//
// The second setpoint is there to catch exactly this: a readback stuck at the
// first value -- a supply that took the first Apply and ignored the second, or
// a driver reading a cached setpoint instead of MEAS:VOLT?.
//
TEST_F( SupplyOutputsFixture, FailsWhenTheReadbackIgnoresTheSecondSetpoint)
{
    Measure.inject( "DcP6.Voltage", { 5.0_V, 5.0_V, 0.0_V });
    Measure.inject( "DcP6.Current", 0.0_mA);

    EXPECT_FALSE( verdictOf( psuOutput2Check));
}

//
// The check safing depends on: Remove must leave the output reading zero.
//
TEST_F( SupplyOutputsFixture, FailsWhenTheOutputDoesNotTurnOff)
{
    Measure.inject( "DcP7.Voltage", { 5.0_V, 24.0_V, 24.0_V });
    Measure.inject( "DcP7.Current", 0.0_mA);

    EXPECT_FALSE( verdictOf( psuOutput3Check));
}

//
// An open output that reports drawing current is either not open -- a lead
// left on something -- or a readback offset worth knowing about. Either way
// not a pass.
//
TEST_F( SupplyOutputsFixture, FailsWhenAnOpenOutputDrawsCurrent)
{
    Measure.inject( "DcP5.Voltage", { 1.0_V, 5.0_V, 0.0_V });
    Measure.inject( "DcP5.Current", 50.0_mA);

    EXPECT_FALSE( verdictOf( psuOutput1Check));
}

//
// And the reason for the fixture's SetUp, stated as behaviour: with the bench
// detached, a script that Applies twice leaves every output exactly as it
// was. If this ever fails, the unit tests above were driving the driver --
// and on this desk, that means the supply.
//
TEST_F( SupplyOutputsFixture, ADetachedRunSendsNothingToTheSupply)
{
    injectHealthy( "DcP5", 1.0_V, 5.0_V);
    static_cast<void>( verdictOf( psuOutput1Check));

    EXPECT_FALSE( DcP5.isEnabled());
    EXPECT_EQ(    DcP5.outputVoltage(), 0.0_V);
}
