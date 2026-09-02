#include "hal/verbs/safing.hpp"

#include "hal/topology/active_instruments.hpp"
#include "hal/keysight_ac6834b.hpp"
#include "hal/keysight_dso8064a.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/keysight_l4411a.hpp"
#include "hal/keysight_n6701a.hpp"
#include "hal/fabric/switch_fabric.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <stdexcept>

using namespace core::literals;

namespace
{
    //
    // The compile-time half of the safing contract: every driver in this
    // rig satisfies hal::SafeableInstrument, sources and passive
    // instruments alike. hal::safeRig() already static_asserts this per
    // instance it actually finds in hal/src/verbs/safing.cpp, so a driver missing
    // safe() can't reach a test run at all -- these repeat it per *type* so
    // the requirement is visible where the rest of the safing behaviour is
    // documented, rather than only inside safeRig()'s own reflection loop.
    //
    static_assert( hal::SafeableInstrument< hal::keysight_n6701a::Direct> );
    static_assert( hal::SafeableInstrument< hal::keysight_n6701a::Relay> );
    static_assert( hal::SafeableInstrument< hal::keysight_ac6834b::Ac6834B> );
    static_assert( hal::SafeableInstrument< hal::keysight_l4411a::L4411A> );
    static_assert( hal::SafeableInstrument< hal::keysight_dso8064a::DSO8064A> );

    //
    // The other compile-time half, specific to reflecting over InstrumentTag
    // rather than a hand-written list: safeRig() only ever *finds* an
    // instrument to safe by reflecting over global variables whose type
    // derives from hal::InstrumentTag (see that struct's own comment in
    // hal/driver/instrument.hpp) -- a type satisfying SafeableInstrument without
    // also deriving from InstrumentTag would not fail any static_assert at
    // all, because safeRig()'s loop would simply never reach it; there is
    // no case to fall into, the same silent-skip shape hal::keysight_l4411a::L4411A::safe()'s
    // own comment warns an opt-in mechanism would have. These static_asserts
    // are what actually closes that gap: every real driver type is checked
    // against both requirements independently here, so a future driver
    // missing either one is caught at this line, not discovered only when
    // it silently never gets safed on the bench.
    //
    static_assert( std::derived_from< hal::keysight_n6701a::Direct, hal::InstrumentTag> );
    static_assert( std::derived_from< hal::keysight_n6701a::Relay,  hal::InstrumentTag> );
    static_assert( std::derived_from< hal::keysight_ac6834b::Ac6834B,      hal::InstrumentTag> );
    static_assert( std::derived_from< hal::keysight_l4411a::L4411A,       hal::InstrumentTag> );
    static_assert( std::derived_from< hal::keysight_dso8064a::DSO8064A,      hal::InstrumentTag> );

    //
    // The other direction, which is the half that actually demonstrates
    // the guarantee is real: a driver without safe() does not satisfy the
    // concept, so hal::safeRig()'s static_assert would reject it rather
    // than quietly skipping it. Checked the concept-wrapped way -- see
    // core/tests/criteria/test_static_constraints.cpp's own comment for why a bare
    // static_assert(!requires{...}) hard-fails on GCC 13/Clang 18 instead
    // of soft-failing, and why this form is the only reliable one.
    //
    struct DriverThatForgotToSafe
    {
        // Deliberately empty -- stands in for a future instrument driver
        // whose author never wrote safe(). If hal::safeRig() had been
        // built as an opt-in customization point defaulting to a no-op,
        // this type would be silently accepted and never safed; see
        // hal::keysight_l4411a::L4411A::safe() for why it isn't.
    };

    static_assert( !hal::SafeableInstrument< DriverThatForgotToSafe> );

    //
    // safeRig() acts on the rig's actual global instruments and global
    // fabric (hal/topology/active_instruments.hpp), not on locals a fixture owns --
    // it takes no arguments precisely because it cannot be handed a
    // substitute. So these tests drive those globals directly, and each
    // one establishes the state it needs rather than assuming the rig is
    // idle on entry: GoogleTest runs the whole binary in one process, so
    // an earlier test's leftovers are visible here, and safing is
    // specified to work from any starting state anyway.
    //
    struct SafingFixture : ::testing::Test
    {
        // Two arbitrary elements standing in for whatever a dead test
        // might have left closed. Their identity doesn't matter -- see
        // FabricIsOpenedWithoutConsultingWhoClosedWhat below.
        static constexpr hal::SwitchElementId someElement{ hal::SwitchDeviceId::Spst1, 4 };
        static constexpr hal::SwitchElementId otherElement{ hal::SwitchDeviceId::Mux1, 3 };

        auto energiseEverything() const -> void
        {
            DcP1.applyOutput( 24.0_V, 7.0_A);
            DcP2.applyOutput( 5.0_V, 2.0_A);
            DcP3.applyOutput( 12.0_V, 1.0_A);
            DcP4.applyOutput( 48.0_V, 0.5_A);
            AcP1.applyOutput( 115.0_V, 400.0_Hz, 3.0_A);

            hal::fabric.close( someElement);
            hal::fabric.close( otherElement);
        }
    };
} // namespace

TEST_F( SafingFixture, SafeRigDisablesEveryDcSourceAndZeroesItsSetpoint)
{
    energiseEverything();
    ASSERT_TRUE( DcP1.isEnabled());

    hal::safeRig();

    // Both halves matter: the output is off, and the setpoint it would
    // come back at is zero -- see hal::keysight_n6701a::N6701A::safe() on why safing
    // clears the setpoint rather than only disabling the output.
    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( DcP2.isEnabled());
    EXPECT_FALSE( DcP3.isEnabled());
    EXPECT_FALSE( DcP4.isEnabled());

    EXPECT_DOUBLE_EQ( DcP1.outputVoltage().value(), 0.0);
    EXPECT_DOUBLE_EQ( DcP2.outputVoltage().value(), 0.0);
    EXPECT_DOUBLE_EQ( DcP3.outputVoltage().value(), 0.0);
    EXPECT_DOUBLE_EQ( DcP4.outputVoltage().value(), 0.0);
}

TEST_F( SafingFixture, SafeRigDisablesTheAcSourceAndZeroesItsSetpoint)
{
    energiseEverything();
    ASSERT_TRUE( AcP1.isEnabled());

    hal::safeRig();

    EXPECT_FALSE( AcP1.isEnabled());
    EXPECT_DOUBLE_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::A).value(), 0.0);
    EXPECT_DOUBLE_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::B).value(), 0.0);
    EXPECT_DOUBLE_EQ( AcP1.phaseVoltage( hal::keysight_ac6834b::Phase::C).value(), 0.0);
}

TEST_F( SafingFixture, SafeRigLeavesCurrentLimitsInPlace)
{
    energiseEverything();

    hal::safeRig();

    // Not an oversight -- with the output off and the setpoint at zero a
    // stale limit has nothing to limit, and an accidental re-enable is
    // better off finding one still set than finding none. See
    // hal::keysight_n6701a::N6701A::safe().
    ASSERT_TRUE( DcP1.currentLimit().has_value());
    EXPECT_DOUBLE_EQ( DcP1.currentLimit()->value(), 7.0);
}

TEST_F( SafingFixture, SafeRigOpensEveryRelayInTheFabric)
{
    energiseEverything();
    ASSERT_TRUE( hal::fabric.isClosed( someElement));
    ASSERT_TRUE( hal::fabric.isClosed( otherElement));

    hal::safeRig();

    EXPECT_FALSE( hal::fabric.isClosed( someElement));
    EXPECT_FALSE( hal::fabric.isClosed( otherElement));
}

TEST_F( SafingFixture, SafeRigOpensRelaysHeldByMoreThanOneUnreleasedCaller)
{
    // The case a reference-counted disconnect() cannot clean up: a crashed
    // script left two closes on one element and released neither, so a
    // matched open() would only take the count from two to one and leave
    // the relay closed. safeRig() clears the counts outright instead --
    // see hal/src/verbs/safing.cpp on why it calls openAll() rather than
    // computing paths to disconnect.
    hal::fabric.close( someElement);
    hal::fabric.close( someElement);
    ASSERT_TRUE( hal::fabric.isClosed( someElement));

    hal::safeRig();

    EXPECT_FALSE( hal::fabric.isClosed( someElement));
}

TEST_F( SafingFixture, SafeRigIsIdempotent)
{
    energiseEverything();

    hal::safeRig();
    hal::safeRig();
    hal::safeRig();

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( AcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( someElement));
}

TEST_F( SafingFixture, SafeRigWorksOnAnAlreadyIdleRigWithNoSetupAtAll)
{
    // The state safing is actually invoked from is unknown, so "already
    // idle" has to be as valid a starting point as "mid-test" -- a caller
    // that isn't sure whether safing already happened should just call it.
    hal::safeRig();
    hal::safeRig();

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( someElement));
}

TEST_F( SafingFixture, SafeRigDoesNotDisturbPassiveInstrumentState)
{
    // A DMM's mode and a scope's mode/channel are the last thing those
    // instruments were told to look at. Nothing about them can energise
    // the DUT, and safing runs after a script has already died, so there
    // is no reason to discard the one piece of state still worth reading
    // afterwards -- see hal::keysight_dso8064a::DSO8064A::safe().
    static_cast<void>( Dmm1.acVoltage());
    static_cast<void>( Osc1.channel<3>());
    Osc1.setMode( hal::keysight_dso8064a::DSO8064A::Mode::Vrms);

    const auto dmmMode     = Dmm1.mode();
    const auto scopeMode   = Osc1.mode();
    const auto scopeChannel = Osc1.channelNumber();

    hal::safeRig();

    EXPECT_EQ( Dmm1.mode(), dmmMode);
    EXPECT_EQ( Osc1.mode(), scopeMode);
    EXPECT_EQ( Osc1.channelNumber(), scopeChannel);
}

TEST( Safing, SourceSafeIsIndependentOfRemoveAndNeedsNoFabricOrWiring)
{
    // safe() is not Remove(...) under another name: Remove goes through a
    // config and a builder chain, which means knowing which supply a
    // script was driving. safe() is called when that is exactly what
    // nobody knows, so it is reachable on a bare instrument with no
    // engine, no fabric, and no wiring table in sight.
    hal::keysight_n6701a::Relay dcP3{ hal::InstrumentId::DcP3, hal::Simulated{}, 3 };

    dcP3.applyOutput( 24.0_V, 7.0_A);
    ASSERT_TRUE( dcP3.isEnabled());

    dcP3.safe();

    EXPECT_FALSE( dcP3.isEnabled());
    EXPECT_DOUBLE_EQ( dcP3.outputVoltage().value(), 0.0);
}

TEST_F( SafingFixture, RigSafingGuardSafesOnNormalScopeExit)
{
    {
        hal::RigSafingGuard safeOnExit;

        energiseEverything();
        ASSERT_TRUE( DcP1.isEnabled());
    }

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( someElement));
}

TEST_F( SafingFixture, RigSafingGuardSafesWhenUnwoundByAnException)
{
    // The case the guard exists for: a script throwing partway through,
    // same as core::asQuantity does on a kind mismatch (see
    // core/quantities/quantity_kind.hpp). The guard's destructor has to run during
    // that unwind, not only on the return path energiseEverything() itself
    // takes.
    energiseEverything();
    ASSERT_TRUE( DcP1.isEnabled());

    try
    {
        hal::RigSafingGuard safeOnExit;
        throw std::runtime_error( "script blew up mid-measurement");
    }
    catch ( const std::runtime_error &)
    {
    }

    EXPECT_FALSE( DcP1.isEnabled());
    EXPECT_FALSE( hal::fabric.isClosed( someElement));
}

TEST( Safing, DirectWiredSupplySafesTheSameWayARelayIsolatedOneDoes)
{
    // Isolation is about whether there is a relay to Connect/Disconnect
    // (see hal::keysight_n6701a::SwitchableIsolation) -- it says nothing about whether the
    // output can be dropped. Both kinds safe identically, which is why
    // safe() is unconstrained where connectDriver is not.
    hal::keysight_n6701a::Direct dcP1{ hal::InstrumentId::DcP1, hal::Simulated{}, 1 };

    dcP1.applyOutput( 24.0_V, std::nullopt);
    dcP1.safe();

    EXPECT_FALSE( dcP1.isEnabled());
    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 0.0);
}
