#include "hal/verbs/safing.hpp"

#include "hal/topology/active_instruments.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/keysight_edu34450a.hpp"
#include "hal/keysight_edu36311a.hpp"
#include "hal/keysight_dsox1202g.hpp"
#include "hal/fabric/switch_device.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/address_tables.hpp"
#include "hal/topology/wiring.hpp"
#include "hal/verbs/preflight.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

//
// The dev bench, checked against what it claims to be.
//
// rig/tests/ for the bench deployment holds integration tests that need more
// than one instrument. This directory's job is different and smaller, because
// the deployment is: what is worth asserting about a bench with one instrument
// and no switching hardware is precisely that it *is* that -- so that the day
// somebody adds a card or a second meter, the file saying "this is a desk with
// a meter on it" fails and gets read.
//
// Most of it is compile-time, in the style dut/tests/test_wiring_coverage.cpp
// established: the assertions that matter are about tables, and a table is
// checked where it is expanded rather than at a run.
//
// The wiring tables are reached by a plain repo-root-relative #include, the way
// rig/tests/test_wiring_uniqueness.cpp reaches the bench's -- not through
// THORIUM_WIRING_TABLE, which is PRIVATE to hal_rig on purpose (see
// framework/hal/CMakeLists.txt).
//
#include "dev/rig/wiring.inc"

namespace
{
    //
    // -- The bench is a meter, a supply and a scope --------------------------
    //
    // hal::InstrumentId's enumerators come from dev/rig/instrument.inc, so this
    // is that file's row count stated where a reader of the tests will see it.
    // Another INSTRUMENT() row fails here, which is the intent: it is not
    // forbidden, it is a change to what this deployment is, and it should be a
    // deliberate edit to this line rather than a silent widening. This line has
    // been that edit twice: the supply's three outputs, then the scope.
    //
    static_assert( core::meta::values<hal::InstrumentId>.size() == 5);
    static_assert( core::meta::values<hal::InstrumentId>[0] == hal::InstrumentId::Dmm1);
    static_assert( core::meta::values<hal::InstrumentId>[1] == hal::InstrumentId::DcP5);
    static_assert( core::meta::values<hal::InstrumentId>[2] == hal::InstrumentId::DcP6);
    static_assert( core::meta::values<hal::InstrumentId>[3] == hal::InstrumentId::DcP7);
    static_assert( core::meta::values<hal::InstrumentId>[4] == hal::InstrumentId::Osc1);

    //
    // -- And no switching hardware at all ------------------------------------
    //
    // The claim dev/rig/devices.inc exists to make, and the one that decides
    // what the rest of this deployment can do: with no SwitchDeviceId
    // enumerators there is no HOP( ...) anyone could write, so every reading
    // here is an instrument readback and the routed path is out of reach.
    //
    // This is also the assertion that pins down the fix in
    // hal/fabric/switch_device.hpp that made an empty table compile at all -- see its
    // own comment on why switchDevices is a std::array.
    //
    static_assert( core::meta::values<hal::SwitchDeviceId>.empty());
    static_assert( hal::detail::switchDevices.empty());

    //
    // -- Which is why the first three wiring tables are empty ----------------
    //
    // Not an independent fact: a row in any of those blocks would have to name
    // a card, and there are none. Asserted anyway, because "empty because it
    // has to be" and "empty because nobody has written it yet" look identical
    // in the file and are the same two things dev/rig/wiring.inc's own comment
    // distinguishes.
    //
    static_assert( ! hal::isWired( hal::VpcLocation{ hal::VpcRack::A, 1, 3 }, hal::WireRole::Force));
    static_assert( ! hal::isSourceWired( hal::VpcLocation{ hal::VpcRack::A, 1, 3 }));

    //
    // -- And why the fourth is empty for a different reason -------------------
    //
    // TAP_WIRING names no card (see hal::TapWiring -- a tap row is
    // (instrument, pin) with no path at all), so nothing about a bench with no
    // switching hardware forces this one to be empty. What does is that this
    // deployment declares no POINT to name: dev/dut/adapter.inc has none.
    //
    // Asserted as both halves, because they are two different claims and the
    // second is the one that would go stale first: no pin is tapped, and Dmm1
    // taps nothing -- so every reading here takes core::MeasureEngine's
    // point-free overload, and takes it legally rather than by omission. The
    // day a WIRE_TAP row is added, that overload starts refusing Dmm1 and this
    // assertion is what says so.
    //
    static_assert( ! hal::isTapWired( hal::VpcLocation{ hal::VpcRack::A, 1, 3 }));
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::Dmm1));
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::DcP5));
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::DcP6));
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::DcP7));
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::Osc1));

    //
    // -- Each driver still owes the framework what every driver owes ----------
    //
    // safeRig() static_asserts this per instance it finds, so a driver without
    // safe() cannot reach a run -- repeated here per *type*, exactly as
    // rig/tests/test_safing.cpp does for the bench's five, so that the
    // requirement is visible in the deployment that has only a few. The
    // supply's is the one that matters: it is the only instrument here that
    // sources anything, so it is the only one safing has anything to do to.
    //
    static_assert( hal::SafeableInstrument< hal::keysight_edu34450a::EDU34450A> );
    static_assert( hal::SafeableInstrument< hal::keysight_edu36311a::DirectOutput1> );
    static_assert( hal::SafeableInstrument< hal::keysight_edu36311a::DirectOutput2> );
    static_assert( hal::SafeableInstrument< hal::keysight_edu36311a::DirectOutput3> );
    static_assert( hal::SafeableInstrument< hal::keysight_dsox1202g::DSOX1202G> );
} // namespace

//
// Safing a bench with nothing to safe is a real case, not a degenerate one: it
// is what every run on this deployment ends with. It has to reach the one
// instrument (which is why this is a runtime test and not another
// static_assert) and it has to not care that the fabric is empty.
//
TEST( DevRig, SafingReachesTheOneInstrumentAndSurvivesAnEmptyFabric)
{
    EXPECT_NO_THROW( hal::safeRig());
}

//
// The other half of "no wiring" -- stated as behaviour rather than as a table,
// because this is the throw a script would actually hit if it tried to take a
// routed reading on this bench. It is the correct outcome and worth having a
// test say so: nothing here composes a route, so nothing here should be able
// to ask for one and get a plausible answer.
//
TEST( DevRig, AskingForARouteOnAFabriclessBenchThrows)
{
    EXPECT_THROW( ( void) hal::instrumentWiring.find( hal::InstrumentId::Dmm1), std::runtime_error);
}

//
// -- the pool table -------------------------------------------------------
//
// This deployment is the only one in the tree with a pool (dev/rig/pools.inc),
// so it is the only place these two facts can be asserted. They are the half
// of framework/hal/tests/topology/test_address_plan.cpp's old
// "no deployment declares a pool" tripwire that survived the desk acquiring
// one -- see that file on why the claim moved rather than being softened.
//
// Both are about the table rather than about a meter, so neither needs
// hardware: poolFor() is a table read, and the binding below stops short of
// the acquisition that would open a socket.
//
TEST( DevRig, TheDeskDeclaresItsShelfOfMetersInPreferenceOrder)
{
    const auto candidates = hal::poolFor( hal::InstrumentId::Dmm1);

    //
    // In order, because order is meaning here: acquireFromPool() takes the
    // first that answers, so a reshuffle of these rows is a change to which
    // meter a run prefers and should not pass silently.
    //
    // One candidate today, the desk's own meter over USB -- the shelf is down
    // to one, and the row is still a pool so that a second meter is one more
    // POOL row rather than a change of table.
    //
    ASSERT_EQ( candidates.size(), 1u);
    EXPECT_EQ( candidates[ 0], hal::Address{ hal::Usb{ "CN65510018" } });
}

//
// The consequence a reader of dev/rig/instrument.inc is most likely to get
// wrong: with a pool declared, the row's own third column no longer decides
// an attached run's address. bindAddresses() marks the row Pool and leaves it
// unbound, and Lan( "dev-dmm") is not where the meter is looked for.
//
// Asserted as the source rather than as a value, because there is no value
// yet at this point in a run -- which is the whole distinction.
//
TEST( DevRig, ThePooledRowIsNotBoundFromTheInstrumentTable)
{
    const auto bindings = hal::bindAddresses( hal::AddressPlan{});

    ASSERT_EQ( bindings.size(), 5u);
    EXPECT_EQ( bindings[ 0].Id,     hal::InstrumentId::Dmm1);
    EXPECT_EQ( bindings[ 0].Source, hal::AddressSource::Pool);
}

//
// And the supply is not pooled, which is the other half of the same table and
// a decision rather than an omission: preflight claims a pooled box by serial,
// and three rows that are one chassis would each want a chassis of their own
// (see dev/rig/instrument.inc). So all three outputs bind from the table, to
// the same serial -- one box, three endpoints behind it.
//
TEST( DevRig, TheSupplysThreeOutputsBindToOneBoxFromTheTable)
{
    const auto bindings = hal::bindAddresses( hal::AddressPlan{});

    ASSERT_EQ( bindings.size(), 5u);
    EXPECT_TRUE( hal::poolFor( hal::InstrumentId::DcP5).empty());

    for( std::size_t row = 1; row <= 3; ++row)
    {
        EXPECT_EQ( bindings[ row].Source, hal::AddressSource::Table) << "row " << row;
        EXPECT_EQ( bindings[ row].Value,  hal::Address{ hal::Usb{ "CN65100272" } }) << "row " << row;
    }
}
