#include "hal/verbs/safing.hpp"

#include "hal/topology/active_instruments.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/keysight_edu34450a.hpp"
#include "hal/keysight_edu36311a.hpp"
#include "hal/keysight_dsox1202g.hpp"
#include "hal/keysight_33522b.hpp"
#include "hal/keysight_34980a.hpp"
#include "hal/fabric/switch_device.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/address_tables.hpp"
#include "hal/topology/wiring.hpp"
#include "hal/verbs/preflight.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "hal/io/transport.hpp"

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
    // -- The bench: a meter, a supply, a scope, a generator and a switch unit --
    //
    // hal::InstrumentId's enumerators come from dev/rig/instrument.inc, so this
    // is that file's row count stated where a reader of the tests will see it.
    // Another INSTRUMENT() row fails here, which is the intent: it is not
    // forbidden, it is a change to what this deployment is, and it should be a
    // deliberate edit to this line rather than a silent widening. This line has
    // been that edit four times: the supply's outputs, the scope, the
    // generator, the switch unit.
    //
    static_assert( core::meta::values<hal::InstrumentId>.size() == 7);
    static_assert( core::meta::values<hal::InstrumentId>[0] == hal::InstrumentId::Dmm1);
    static_assert( core::meta::values<hal::InstrumentId>[1] == hal::InstrumentId::DcP5);
    static_assert( core::meta::values<hal::InstrumentId>[2] == hal::InstrumentId::DcP6);
    static_assert( core::meta::values<hal::InstrumentId>[3] == hal::InstrumentId::DcP7);
    static_assert( core::meta::values<hal::InstrumentId>[4] == hal::InstrumentId::Osc1);
    static_assert( core::meta::values<hal::InstrumentId>[5] == hal::InstrumentId::Wfg1);
    static_assert( core::meta::values<hal::InstrumentId>[6] == hal::InstrumentId::Swu1);

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
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::Wfg1));
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::Swu1));

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
    static_assert( hal::SafeableInstrument< hal::keysight_33522b::Wfg33522B> );
    static_assert( hal::SafeableInstrument< hal::keysight_34980a::Chassis> );
    static_assert( hal::RelayHoldingInstrument< hal::keysight_34980a::Chassis> );
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
    const auto candidates = hal::poolFor( "DeskDmm");

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
// wrong: with a pool declared, the row's own address column no longer decides
// an attached run's address. bindAddresses() marks the row Pool and leaves it
// unbound, and Lan( "dev-dmm") is not where the meter is looked for.
//
// Asserted as the source rather than as a value, because there is no value
// yet at this point in a run -- which is the whole distinction.
//
TEST( DevRig, ThePooledRowIsNotBoundFromTheInstrumentTable)
{
    const auto bindings = hal::bindAddresses( hal::AddressPlan{});

    ASSERT_EQ( bindings.size(), 7u);
    EXPECT_EQ( bindings[ 0].Id,     hal::InstrumentId::Dmm1);
    EXPECT_EQ( bindings[ 0].Source, hal::AddressSource::Pool);
}

//
// And the supply's three outputs are one box, Psu1, binding from the table to
// one serial -- one unit, three endpoints behind it. Not pooled today, and no
// longer because it could not be: a pool names a box now, and a box is
// acquired once (see hal/topology/boxes.hpp).
//
TEST( DevRig, TheSupplysThreeOutputsBindToOneBoxFromTheTable)
{
    const auto bindings = hal::bindAddresses( hal::AddressPlan{});

    ASSERT_EQ( bindings.size(), 7u);
    EXPECT_TRUE( hal::poolFor( "Psu1").empty());

    for( std::size_t row = 1; row <= 3; ++row)
    {
        EXPECT_EQ( bindings[ row].Source, hal::AddressSource::Table) << "row " << row;
        EXPECT_EQ( bindings[ row].Value,  hal::Address{ hal::Usb{ "CN65100272" } }) << "row " << row;
    }
}

//
// -- Safing order: sources off, then relays open ------------------------------
//
// The one ordering rule the switch unit's joining the instrument table had to
// keep: its relays move after every source is off (see
// hal::RelayHoldingInstrument). Asserted here, on this deployment's real
// globals and the real hal::safeRig(), because this is the deployment that has
// both a supply and a chassis to get the order wrong between.
//
// Both boxes get a fake transport writing to one shared log, so the order is
// one list to read. The supply's fake stands in for the box behind all three of
// DcP5-DcP7 (they share one session, see hal::BoxConnection), so all three
// outputs' safing lands in it.
//
namespace
{
    class Recorder final : public hal::io::ITransport
    {
        public:
            Recorder( std::shared_ptr<std::vector<std::string>> log, std::string tag, std::string identity)
                : mLog( std::move( log)), mTag( std::move( tag)), mIdentity( std::move( identity)) {}

            auto send( const std::string_view command) -> void override
            {
                mLog->push_back( mTag + ": " + std::string( command));

                if( command == "*IDN?")
                {
                    mReplies.push_back( mIdentity);
                }
                else if( command == "SYST:ERR?")
                {
                    mReplies.emplace_back( "+0,\"No error\"");
                }
                else if( !command.empty() && command.back() == '?')
                {
                    mReplies.emplace_back( "1");
                }
            }

            auto receive() -> std::string override
            {
                if( mReplies.empty())
                {
                    throw hal::io::TransportTimeout( "nothing queued on " + mTag);
                }

                auto reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "recording fake " + mTag;
            }

        private:
            std::shared_ptr<std::vector<std::string>> mLog;
            std::string                               mTag;
            std::string                               mIdentity;
            std::vector<std::string>                  mReplies;
    };

    [[nodiscard]]
    auto firstIndexOf( const std::vector<std::string> & log, const std::string_view prefix) -> std::ptrdiff_t
    {
        const auto found = std::ranges::find_if( log, [ prefix]( const std::string & line) { return line.starts_with( prefix); });

        return found == log.end() ? -1 : std::distance( log.begin(), found);
    }
} // namespace

TEST( DevRig, SafingTurnsTheSupplyOffBeforeItOpensTheSwitchUnitsRelays)
{
    auto log = std::make_shared<std::vector<std::string>>();

    DcP5.useTransport( std::make_unique<Recorder>( log, "psu", "Keysight Technologies,EDU36311A,CN65100272,1.0.2"));
    Swu1.useTransport( std::make_unique<Recorder>( log, "swu", "Agilent Technologies,34980A,MY44000001,2.43"));

    // Open both sessions, so safing has something to use -- it never opens one.
    static_cast<void>( DcP5.session());
    static_cast<void>( Swu1.session());

    log->clear();

    hal::safeRig();

    const auto supplyOff  = firstIndexOf( *log, "psu: OUTP 0");
    const auto relaysOpen = firstIndexOf( *log, "swu: ROUT:OPEN:ALL");

    ASSERT_GE( supplyOff,  0) << "the supply was never turned off";
    ASSERT_GE( relaysOpen, 0) << "the switch unit's relays were never opened";
    EXPECT_LT( supplyOff, relaysOpen) << "a relay moved while a source was still on";

    //
    // And every one of the three outputs was off before any relay moved -- not
    // just the first.
    //
    for( std::ptrdiff_t line = relaysOpen; line < static_cast<std::ptrdiff_t>( log->size()); ++line)
    {
        EXPECT_FALSE( ( *log)[ line].starts_with( "psu: OUTP 0")) << "an output went off after the relays opened";
    }

    DcP5.closeSession();
    Swu1.closeSession();
}

//
// -- Boxes ---------------------------------------------------------------------
//
// The desk's rows as boxes: each instrument a face of the unit it is on, the
// supply's three outputs one unit. Then what that buys at startup, on this
// deployment's real globals -- one flag moving three faces, three faces making
// one claim, and two box names reaching one unit refused.
//
TEST( DevRig, EachInstrumentIsAFaceOfTheBoxItIsOn)
{
    EXPECT_EQ( hal::boxOf( hal::InstrumentId::Dmm1), "DeskDmm");
    EXPECT_EQ( hal::boxOf( hal::InstrumentId::Osc1), "Scope1");
    EXPECT_EQ( hal::boxOf( hal::InstrumentId::Wfg1), "Wfg1");
    EXPECT_EQ( hal::boxOf( hal::InstrumentId::Swu1), "Swu1");

    EXPECT_EQ( hal::instrumentsIn( "Psu1"),
               ( std::vector{ hal::InstrumentId::DcP5, hal::InstrumentId::DcP6, hal::InstrumentId::DcP7 }));

    EXPECT_EQ( hal::instrumentBoxNames(),
               ( std::vector<std::string_view>{ "DeskDmm", "Psu1", "Scope1", "Wfg1", "Swu1" }));
}

namespace
{
    //
    // Every row's table address, put back after a test that bound or contacted
    // with a plan of its own -- including the pooled meter, which
    // bindAddresses( AddressPlan{}) deliberately leaves unbound and so would
    // not restore.
    //
    struct RestoreAddresses
    {
        hal::Address Dmm1Address = Dmm1.address();
        hal::Address DcP5Address = DcP5.address();
        hal::Address DcP6Address = DcP6.address();
        hal::Address DcP7Address = DcP7.address();
        hal::Address Osc1Address = Osc1.address();
        hal::Address Wfg1Address = Wfg1.address();
        hal::Address Swu1Address = Swu1.address();

        ~RestoreAddresses()
        {
            Dmm1.useAddress( Dmm1Address);
            DcP5.useAddress( DcP5Address);
            DcP6.useAddress( DcP6Address);
            DcP7.useAddress( DcP7Address);
            Osc1.useAddress( Osc1Address);
            Wfg1.useAddress( Wfg1Address);
            Swu1.useAddress( Swu1Address);
        }
    };

    //
    // A plan with every box but the named ones simulated, so contacting
    // reaches only the boxes a test has handed fakes to.
    //
    auto planWithOnly( const std::vector<std::pair<std::string_view, hal::Address>> & live) -> hal::AddressPlan
    {
        hal::AddressPlan plan;

        for( const auto box : hal::instrumentBoxNames())
        {
            const auto found = std::ranges::find( live, box, &std::pair<std::string_view, hal::Address>::first);

            plan.Overrides.emplace_back( box, found == live.end() ? hal::Address{ hal::Simulated{} } : found->second);
        }

        return plan;
    }
} // namespace

//
// One flag moves every face: Psu1=sim takes all three outputs with it, each
// saying where its address came from.
//
TEST( DevRig, OneOverrideMovesEveryFaceOfABox)
{
    const RestoreAddresses restore;

    hal::AddressPlan plan;

    plan.Overrides.emplace_back( "Psu1", hal::Simulated{});

    for( const auto & binding : hal::bindAddresses( plan))
    {
        if( binding.Box != "Psu1")
        {
            continue;
        }

        EXPECT_EQ( binding.Source, hal::AddressSource::Override) << to_string( binding.Id);
        EXPECT_TRUE( std::holds_alternative<hal::Simulated>( binding.Value)) << to_string( binding.Id);
    }

    EXPECT_TRUE( std::holds_alternative<hal::Simulated>( DcP5.address()));
    EXPECT_TRUE( std::holds_alternative<hal::Simulated>( DcP6.address()));
    EXPECT_TRUE( std::holds_alternative<hal::Simulated>( DcP7.address()));
}

//
// Three faces, one unit, one claim: the supply's outputs all answer *IDN? with
// the same serial, and that is the box agreeing with itself -- not three boxes
// claiming one unit.
//
TEST( DevRig, TheFacesOfOneBoxShareItsClaim)
{
    const RestoreAddresses restore;

    auto bindings = hal::bindAddresses( planWithOnly( { { "Psu1", hal::Usb{ "CN65100272" } } }));

    auto log = std::make_shared<std::vector<std::string>>();

    DcP5.useTransport( std::make_unique<Recorder>( log, "psu", "Keysight Technologies,EDU36311A,CN65100272,1.0.2"));

    EXPECT_NO_THROW( hal::contactInstruments( bindings));

    for( const auto & binding : bindings)
    {
        if( binding.Box == "Psu1")
        {
            EXPECT_NE( binding.Identity.find( "CN65100272"), std::string::npos) << to_string( binding.Id);
        }
    }

    DcP5.closeSession();
}

//
// Two box names that reach one unit -- the runtime half of boxes.hpp's "one
// address, one box", for the case the tables cannot see: addresses that
// differ (here two USB serials in the rows) and a unit that answers both with
// one serial. Refused, naming both boxes.
//
TEST( DevRig, TwoBoxesAnsweringWithOneSerialAreRefused)
{
    const RestoreAddresses restore;

    auto bindings = hal::bindAddresses( planWithOnly( {
        { "DeskDmm", hal::Usb{ "CN65510018" } },
        { "Scope1",  hal::Usb{ "CN64504143" } } }));

    auto log = std::make_shared<std::vector<std::string>>();

    Dmm1.useTransport( std::make_unique<Recorder>( log, "dmm",   "Keysight Technologies,EDU34450A,SAME0001,1.0"));
    Osc1.useTransport( std::make_unique<Recorder>( log, "scope", "KEYSIGHT TECHNOLOGIES,DSO-X 1202G,SAME0001,1.0"));

    try
    {
        hal::contactInstruments( bindings);

        FAIL() << "expected two boxes answering with one serial to be refused";
    }
    catch( const std::runtime_error & refused)
    {
        const std::string message{ refused.what() };

        EXPECT_NE( message.find( "DeskDmm"), std::string::npos) << message;
        EXPECT_NE( message.find( "Scope1"),  std::string::npos) << message;
        EXPECT_NE( message.find( "SAME0001"), std::string::npos) << message;
    }

    Dmm1.closeSession();
    Osc1.closeSession();
}

