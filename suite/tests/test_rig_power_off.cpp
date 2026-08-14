#include "suite/scripts.hpp"

//
// Unlike the other two test files here, this one reaches the rig's instrument
// globals and the fabric directly. It has to: the hook under test takes no
// arguments and reads nothing back, so the only thing there is to assert on is
// the state it leaves the rig in -- the same reason hal/tests/test_safing.cpp
// drives those globals rather than locals of its own. Still not the prelude: a
// test of a hook is no more a script than a test of a script is.
//
#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/apply.hpp"
#include "hal/switch_fabric.hpp"

#include <gtest/gtest.h>

using namespace core::literals;

namespace
{
    struct RigPowerOffFixture : ::testing::Test
    {
        //
        // Whatever some earlier test left behind is visible here -- GoogleTest
        // runs the whole binary in one process -- so each test establishes the
        // energised state it needs rather than assuming one, and powering down
        // has to work from any starting state anyway.
        //
        static auto energiseEverything() -> void
        {
            Apply( DcP1.dc().voltage(  24.0_V).currentLimit( 7.0_A));
            Apply( DcP2.dc().voltage(   5.0_V).currentLimit( 2.0_A));
            Apply( DcP3.dc().voltage(  12.0_V).currentLimit( 1.0_A));
            Apply( DcP4.dc().voltage(  48.0_V).currentLimit( 0.5_A));
            Apply( AcP1.threePhaseWye().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

            // Only the three that have a relay at all -- Connect( DcP1.dc())
            // would not compile (see hal::SwitchableIsolation).
            Connect( DcP3.dc());
            Connect( DcP4.dc());
            Connect( AcP1.threePhaseWye());
        }
    };
} // namespace

TEST_F( RigPowerOffFixture, EverySourceEndsUpDisabled)
{
    energiseEverything();

    ASSERT_TRUE( DcP1.isEnabled());
    ASSERT_TRUE( AcP1.isEnabled());

    EXPECT_TRUE( rigPowerOff());

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( DcP2.isEnabled());
    EXPECT_FALSE( DcP3.isEnabled());
    EXPECT_FALSE( DcP4.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
}

//
// The relay half, and the boundary that goes with it. Channel ids are this
// rig's (rig/wiring.inc); naming them here is the price of asserting on the
// fabric from outside hal_rig, where the wiring tables are file-local.
//
TEST_F( RigPowerOffFixture, EveryIsolationRelayItClosedEndsUpOpen)
{
    constexpr hal::SwitchElementId dcP3Path{ hal::SwitchDeviceKind::Matrix, "Matrix2", 24 };
    constexpr hal::SwitchElementId dcP4Path{ hal::SwitchDeviceKind::Matrix, "Matrix2", 25 };
    constexpr hal::SwitchElementId acP1PhaseA{ hal::SwitchDeviceKind::Matrix, "Matrix2", 22 };
    constexpr hal::SwitchElementId acP1Neutral{ hal::SwitchDeviceKind::Matrix, "Matrix2", 27 };

    energiseEverything();

    ASSERT_TRUE( hal::fabric.isClosed( dcP3Path));
    ASSERT_TRUE( hal::fabric.isClosed( acP1Neutral));

    EXPECT_TRUE( rigPowerOff());

    EXPECT_FALSE( hal::fabric.isClosed( dcP3Path));
    EXPECT_FALSE( hal::fabric.isClosed( dcP4Path));

    // Both ends of AcP1's four fixed channels -- phases and the neutral/ground
    // return open together, which is the isolation the return is modelled for.
    EXPECT_FALSE( hal::fabric.isClosed( acP1PhaseA));
    EXPECT_FALSE( hal::fabric.isClosed( acP1Neutral));
}

//
// What separates this hook from hal::safeRig(), asserted rather than only
// argued in a comment: powering down opens the relays it knows about and
// nothing else. Clearing the fabric wholesale is the crash path's job, and it
// happens right after this hook anyway -- a teardown that did it too would be
// making a claim about routes it never made.
//
TEST_F( RigPowerOffFixture, ARouteItNeverMadeIsLeftAlone)
{
    constexpr hal::SwitchElementId someoneElsesRoute{ hal::SwitchDeviceKind::Mux, "Mux1", 3 };

    energiseEverything();
    hal::fabric.close( someoneElsesRoute);

    EXPECT_TRUE( rigPowerOff());

    EXPECT_TRUE( hal::fabric.isClosed( someoneElsesRoute));

    hal::fabric.open( someoneElsesRoute);
}

//
// Idempotent from an already-idle rig: the hook runs on every way out of a run,
// including ones where a script had already removed what it applied, so a
// second power-down must be a no-op rather than an error.
//
TEST_F( RigPowerOffFixture, PoweringDownAnAlreadyIdleRigIsHarmless)
{
    energiseEverything();

    EXPECT_TRUE( rigPowerOff());
    EXPECT_TRUE( rigPowerOff());

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
}
