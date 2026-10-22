#include "suite/scripts.hpp"

//
// Unlike the other two test files here, this one reaches the rig's instrument
// globals and the fabric directly. It has to: the hook under test takes no
// arguments and reads nothing back, so the only thing there is to assert on is
// the state it leaves the rig in -- the same reason rig/tests/test_safing.cpp
// drives those globals rather than locals of its own. Still not the prelude: a
// test of a hook is no more a script than a test of a script is.
//
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"
#include "hal/fabric/switch_fabric.hpp"

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
            //
            // Within each output's own badge, which is not decoration on this
            // bench: both DC rails are 30 V / 1 A outputs of one EDU36311A, so
            // a limit past 1 A is refused before anything is remembered (see
            // hal::keysight_edu36311a::RatingExceeded) and this fixture would
            // throw rather than energise.
            //
            Apply( DcP6.dc().voltage( 24.0_V).currentLimit( 1.0_A));
            Apply( DcP7.dc().voltage( 12.0_V).currentLimit( 0.5_A));
            Apply( AcP1.ac().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

            Connect( DcP6.dc());
            Connect( DcP7.dc());
            Connect( AcP1.ac());
        }
    };
} // namespace

//
// Every source the teardown *names*, which is not every source the rig has:
// DcP5 is declared in rig/instrument.inc and mentioned by neither hook -- the
// EDU36311A's 6 V / 5 A output, which nothing on this DUT wants -- so nothing
// here powers it up and nothing but hal::safeRig() takes it down. It is
// deliberately not asserted on: an expectation that it stays enabled would
// enshrine the gap, and one that it goes off would be testing safing from the
// wrong file. It is the spare DcP4 used to be.
//
TEST_F( RigPowerOffFixture, EverySourceItNamesEndsUpDisabled)
{
    energiseEverything();

    ASSERT_TRUE( DcP6.isEnabled());
    ASSERT_TRUE( AcP1.isEnabled());

    EXPECT_TRUE( rigPowerOff());

    EXPECT_FALSE( DcP6.isEnabled());
    EXPECT_FALSE( DcP7.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
}

//
// The relay half, and the boundary that goes with it. Channel ids are this
// rig's (rig/wiring.inc); naming them here is the price of asserting on the
// fabric from outside hal_rig, where the wiring tables are file-local.
//
TEST_F( RigPowerOffFixture, EveryIsolationRelayItClosedEndsUpOpen)
{
    constexpr hal::SwitchElementId dcP6Path{ hal::SwitchDeviceId::Spst1, 6 };
    constexpr hal::SwitchElementId acP1PhaseA{ hal::SwitchDeviceId::Spst1, 0 };
    constexpr hal::SwitchElementId acP1Neutral{ hal::SwitchDeviceId::Spst1, 3 };

    energiseEverything();

    ASSERT_TRUE( hal::fabric.isClosed( dcP6Path));
    ASSERT_TRUE( hal::fabric.isClosed( acP1Neutral));

    EXPECT_TRUE( rigPowerOff());

    EXPECT_FALSE( hal::fabric.isClosed( dcP6Path));

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
    constexpr hal::SwitchElementId someoneElsesRoute{ hal::SwitchDeviceId::Mux1, 3 };

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

    EXPECT_FALSE( DcP6.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
}
