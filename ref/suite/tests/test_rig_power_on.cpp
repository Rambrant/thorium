#include "suite/scripts.hpp"

//
// Reaches the rig's instrument globals and the fabric directly, for the same
// reason test_rig_power_off.cpp does -- see that file's header comment.
//
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"
#include "hal/fabric/switch_fabric.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using core::quantities::Voltage;

namespace
{
    struct RigPowerOnFixture : ::testing::Test
    {
        //
        // Powering down afterwards is not tidiness here: this hook is the one
        // thing in the binary that deliberately leaves the rig energised, and
        // GoogleTest runs every test in one process. Without this, the next
        // fixture's "establish the state I need" would be starting from a live
        // 115 V AC source it never asked for.
        //
        void TearDown() override
        {
            Measure.useLive();

            static_cast<void>( rigPowerOff());
        }
    };
} // namespace

TEST_F( RigPowerOnFixture, BringsEverySourceUpToItsSetpoint)
{
    EXPECT_TRUE( rigPowerOn());

    EXPECT_TRUE( DcP6.isEnabled());
    EXPECT_TRUE( DcP7.isEnabled());
    EXPECT_TRUE( AcP1.isEnabled());

    EXPECT_EQ( DcP6.outputVoltage(), 28_V);
    EXPECT_EQ( DcP7.outputVoltage(), 24_V);

    //
    // And the current limits, which this hook no longer gets to choose: both
    // rails are 30 V / 1 A outputs of one EDU36311A, so 1 A is the badge and
    // the most that can be asked for. Asserted because the number moved for a
    // hardware reason (these rails were 7 A and 4 A on the N6701A this bench
    // no longer has) and a later edit walking it back up would be refused at
    // runtime rather than caught here -- see
    // hal::keysight_edu36311a::RatingExceeded, and rig/instrument.inc's TODO
    // on what it means for a DUT that draws more.
    //
    EXPECT_EQ( DcP6.currentLimit(), 1_A);
    EXPECT_EQ( DcP7.currentLimit(), 1_A);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::A), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::B), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::C), 115_V);
}

//
// The relays close, and every source this hook brings up now has one -- which
// is a change worth noting rather than a simplification. While an N6701A fed
// the DC rails, DcP1/DcP2 were hal::keysight_edu36311a::DirectOutput1 and could not be
// connected at all, so this test could only assert that nothing was left
// routed on their behalf. The EDU36311A outputs that replaced them are 1 A and
// fit inside a 1260-18 relay, so there is a real relay per rail to assert on.
//
TEST_F( RigPowerOnFixture, ClosesTheIsolationRelaysTheEnergisedSourcesNeed)
{
    constexpr hal::SwitchElementId dcP6Path{  hal::SwitchDeviceId::Spst1, 6 };
    constexpr hal::SwitchElementId dcP7Path{  hal::SwitchDeviceId::Spst1, 7 };
    constexpr hal::SwitchElementId acP1PhaseA{ hal::SwitchDeviceId::Spst1, 0 };
    constexpr hal::SwitchElementId acP1Neutral{ hal::SwitchDeviceId::Spst1, 3 };

    EXPECT_TRUE( rigPowerOn());

    EXPECT_TRUE( hal::fabric.isClosed( dcP6Path));
    EXPECT_TRUE( hal::fabric.isClosed( dcP7Path));
    EXPECT_TRUE( hal::fabric.isClosed( acP1PhaseA));
    EXPECT_TRUE( hal::fabric.isClosed( acP1Neutral));
}

//
// The point of a setup that returns a verdict at all: a rail that comes up
// wrong stops the run before the first test, rather than handing every script
// after it a DUT that was never powered the way it expects.
//
// Injected by the key a point-free reading uses -- "<instrument>.<quantity>",
// not a point name, since an instrument readback never travels the fabric and
// so has no point to be keyed by (see core::MeasureEngine's own comment). All
// six are injected because injection is a mode, not a per-point override: with
// anything queued, a reading with nothing queued for it throws rather than
// quietly falling back to the live rig.
//
// AcP1's three keys carry the phase as well -- "AcP1.B.Voltage" -- because one
// instrument reports a voltage per phase and an unqualified key would make all
// three the same slot (see core::Port::qualifiedBy).
//
TEST_F( RigPowerOnFixture, FailsWhenARailDoesNotComeUp)
{
    Measure.inject( "AcP1.A.Voltage", Voltage{ 115.0 });
    Measure.inject( "AcP1.B.Voltage", Voltage{ 115.0 });
    Measure.inject( "AcP1.C.Voltage", Voltage{ 115.0 });
    Measure.inject( "DcP6.Voltage", Voltage{ 28.0 });
    Measure.inject( "DcP7.Voltage", Voltage{ 19.4 });   // battery rail low -- 24 V +/-0.1 V

    EXPECT_FALSE( rigPowerOn());
}

//
// The two hooks as one sequence and its inverse, which is how they are meant to
// be read: whatever the power-up energises and routes, the power-down undoes.
//
TEST_F( RigPowerOnFixture, WhatItBringsUpThePowerDownTakesBackDown)
{
    constexpr hal::SwitchElementId acP1PhaseA{ hal::SwitchDeviceId::Spst1, 0 };

    ASSERT_TRUE( rigPowerOn());
    ASSERT_TRUE( AcP1.isEnabled());
    ASSERT_TRUE( hal::fabric.isClosed( acP1PhaseA));

    EXPECT_TRUE( rigPowerOff());

    EXPECT_FALSE( DcP6.isEnabled());
    EXPECT_FALSE( DcP7.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( acP1PhaseA));
}
