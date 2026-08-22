#include "suite/scripts.hpp"

//
// Reaches the rig's instrument globals and the fabric directly, for the same
// reason test_rig_power_off.cpp does -- see that file's header comment.
//
#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/measure.hpp"
#include "hal/route.hpp"
#include "hal/source.hpp"
#include "hal/switch_fabric.hpp"

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

    EXPECT_TRUE( DcP1.isEnabled());
    EXPECT_TRUE( DcP2.isEnabled());
    EXPECT_TRUE( DcP3.isEnabled());
    EXPECT_TRUE( AcP1.isEnabled());

    EXPECT_EQ( DcP1.outputVoltage(), 28_V);
    EXPECT_EQ( DcP2.outputVoltage(), 28_V);
    EXPECT_EQ( DcP3.outputVoltage(), 24_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::Phase::A), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::Phase::B), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::Phase::C), 115_V);
}

//
// The relays close, and -- the half worth asserting -- they close for the two
// instruments that have one. DcP1/DcP2 are hal::N6701ADirect and cannot be
// connected at all; that is a compile-time fact, so the only thing a runtime
// test can add is that nothing else was left routed on their behalf.
//
TEST_F( RigPowerOnFixture, ClosesTheIsolationRelaysTheEnergisedSourcesNeed)
{
    constexpr hal::SwitchElementId dcP3Path{ hal::SwitchDeviceId::Spst1, 4 };
    constexpr hal::SwitchElementId acP1PhaseA{ hal::SwitchDeviceId::Spst1, 0 };
    constexpr hal::SwitchElementId acP1Neutral{ hal::SwitchDeviceId::Spst1, 3 };

    EXPECT_TRUE( rigPowerOn());

    EXPECT_TRUE( hal::fabric.isClosed( dcP3Path));
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
    Measure.inject( "DcP1.Voltage", Voltage{ 28.0 });
    Measure.inject( "DcP2.Voltage", Voltage{ 28.0 });
    Measure.inject( "DcP3.Voltage", Voltage{ 19.4 });   // battery rail low -- 24 V +/-0.1 V

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

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( DcP2.isEnabled());
    EXPECT_FALSE( DcP3.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( acP1PhaseA));
}
