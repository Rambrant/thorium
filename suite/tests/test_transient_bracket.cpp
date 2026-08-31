#include "suite/scripts.hpp"

//
// Reaches the rig's instrument globals and the fabric directly, for the same
// reason test_rig_power_on.cpp does -- see that file's header comment. What is
// asserted here is bench state, which is where a save-and-restore bracket's
// whole effect is: neither hook records a check, so there is no journal row to
// read it off instead.
//
#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"
#include "hal/fabric/switch_fabric.hpp"

#include <gtest/gtest.h>

using namespace core::literals;

namespace
{
    //
    // AcP1's four fixed channels -- the three phases and the neutral return,
    // which connectDriver closes and opens as one unit (see hal::keysight_ac6834b::Ac6834B).
    // Phase A alone would not catch a restore that reconnected some of them.
    //
    constexpr hal::SwitchElementId kAcP1PhaseA { hal::SwitchDeviceId::Spst1, 0 };
    constexpr hal::SwitchElementId kAcP1Neutral{ hal::SwitchDeviceId::Spst1, 3 };

    struct TransientBracketFixture : ::testing::Test
    {
        //
        // Every test here starts from a rig that is up, because that is the
        // only state the bracket has anything to say about -- and leaves it
        // down, because GoogleTest runs the whole file in one process and the
        // next fixture must not inherit a live 115 V source it never asked
        // for. Same reasoning as RigPowerOnFixture's own TearDown.
        //
        void SetUp() override
        {
            ASSERT_TRUE( rigPowerOn());
        }

        void TearDown() override
        {
            static_cast<void>( rigPowerOff());
        }

        //
        // What acDropoutScript does to the source, and the only part of it this
        // bracket is paired with. Remove before Disconnect -- the relay opens on
        // a dead path (see core/verbs/source.hpp).
        //
        static auto dropTheAcInput() -> void
        {
            Remove(     AcP1.ac());
            Disconnect( AcP1.ac());
        }
    };
} // namespace

//
// The case the pair exists for.
//
TEST_F( TransientBracketFixture, PutsTheSourceBackToWhatTheSetupFound)
{
    ASSERT_TRUE( transientSetup());

    dropTheAcInput();

    ASSERT_FALSE( AcP1.isEnabled());
    ASSERT_FALSE( hal::fabric.isClosed( kAcP1PhaseA));

    EXPECT_TRUE( transientTeardown());

    EXPECT_TRUE( AcP1.isEnabled());
    EXPECT_TRUE( hal::fabric.isClosed( kAcP1PhaseA));
    EXPECT_TRUE( hal::fabric.isClosed( kAcP1Neutral));

    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::A), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::B), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::C), 115_V);

    EXPECT_EQ( AcP1.frequency(),                   400_Hz);
    EXPECT_EQ( AcP1.currentLimit( hal::keysight_ac6834b::Phase::A),  2_A);
}

//
// Whatever it found, not whatever rigPowerOn happens to apply. This is the
// difference from the trailing Connect/Apply this replaced in
// suite/scripts/ac_dropout_script.cpp, which named 115 V / 400 Hz / 2 A
// literally and so restored those three numbers no matter what was actually in
// force when the group was entered.
//
TEST_F( TransientBracketFixture, RestoresTheSetpointsInForce_NotThePowerUpLiterals)
{
    Apply( AcP1.ac().phaseVoltage( 100_V).frequency( 60_Hz).currentLimit( 5_A));

    ASSERT_TRUE( transientSetup());

    dropTheAcInput();

    EXPECT_TRUE( transientTeardown());

    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::A), 100_V);
    EXPECT_EQ( AcP1.frequency(),                  60_Hz);
    EXPECT_EQ( AcP1.currentLimit( hal::keysight_ac6834b::Phase::A), 5_A);
}

//
// Per phase, and not by accident: the readback is three values and the restore
// puts three back. A balanced restore would quietly rebalance a source that a
// test had deliberately left unbalanced.
//
TEST_F( TransientBracketFixture, RestoresAnUnbalancedSourcePhaseByPhase)
{
    Apply( AcP1.ac().phaseVoltage( hal::keysight_ac6834b::phaseA( 113_V), hal::keysight_ac6834b::phaseB( 115_V), hal::keysight_ac6834b::phaseC( 117_V))
                    .frequency( 400_Hz));

    ASSERT_TRUE( transientSetup());

    dropTheAcInput();

    EXPECT_TRUE( transientTeardown());

    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::A), 113_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::B), 115_V);
    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::C), 117_V);
}

//
// The teardown runs on every path, including the ones where nothing was taken
// away -- a script that returned early, or a group that grows a second test
// which does not touch the source. Re-connecting there would leave
// hal::SwitchFabric's use count for AcP1's four channels one too high, which is
// invisible on a single pass and cumulative under --repeat.
//
TEST_F( TransientBracketFixture, LeavesAnUndisturbedSourceAloneRatherThanReconnectingIt)
{
    ASSERT_TRUE( transientSetup());

    EXPECT_TRUE( transientTeardown());

    EXPECT_TRUE( AcP1.isEnabled());

    // The count, not merely the state: one spurious Connect still reads as
    // closed here, and only shows up when the run-level teardown's single
    // Disconnect fails to open it.
    static_cast<void>( rigPowerOff());

    EXPECT_FALSE( hal::fabric.isClosed( kAcP1PhaseA));
    EXPECT_FALSE( hal::fabric.isClosed( kAcP1Neutral));
}

//
// And the mirror of that, one pass earlier: a setup that finds the source
// already down must not energise one. This is the detached-bench case --
// --replay, --inject and --skeleton all reach a group's hooks with no Apply
// having gone anywhere (see core/session/bench.hpp).
//
TEST_F( TransientBracketFixture, DoesNotEnergiseASourceTheSetupFoundAlreadyDown)
{
    dropTheAcInput();

    ASSERT_TRUE( transientSetup());

    EXPECT_TRUE( transientTeardown());

    EXPECT_FALSE( AcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( kAcP1PhaseA));
}

//
// A teardown reached with nothing recorded. main.cpp constructs the group's
// TeardownGuard *before* its setup, so this is a state the runner can actually
// produce, not a hypothetical.
//
TEST_F( TransientBracketFixture, DoesNothingWhenNoSetupEverRan)
{
    dropTheAcInput();

    EXPECT_TRUE( transientTeardown());

    EXPECT_FALSE( AcP1.isEnabled());
}

//
// Per pass over the selection, not per run: --repeat=3 enters both hooks three
// times, and what the third teardown restores must be what the third setup
// found rather than an accumulation of all three.
//
TEST_F( TransientBracketFixture, RemembersAfreshOnEveryPass)
{
    ASSERT_TRUE( transientSetup());

    dropTheAcInput();

    ASSERT_TRUE( transientTeardown());

    // Second pass, against a source the first pass left at a different setpoint.
    Apply( AcP1.ac().phaseVoltage( 108_V).frequency( 50_Hz).currentLimit( 4_A));

    ASSERT_TRUE( transientSetup());

    dropTheAcInput();

    EXPECT_TRUE( transientTeardown());

    EXPECT_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::A), 108_V);
    EXPECT_EQ( AcP1.frequency(),                  50_Hz);
}
