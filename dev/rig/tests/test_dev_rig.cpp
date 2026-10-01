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
// the deployment is: what is worth asserting about a desk is precisely what it
// has -- its instruments, its boxes, its one switch unit's cards and the one
// route over them -- so that the day somebody adds a card or a meter, the file
// saying what this desk is fails and gets read.
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

#include "hal/topology/adapter.hpp"
#include "hal/verbs/measure.hpp"
#include "core/verbs/at.hpp"
#include "dev/dut/adapter.inc"

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
    // been that edit five times: the supply's outputs, the scope, the
    // generator, the switch unit, and the switch unit's own meter.
    //
    static_assert( core::meta::values<hal::InstrumentId>.size() == 8);
    static_assert( core::meta::values<hal::InstrumentId>[0] == hal::InstrumentId::Dmm1);
    static_assert( core::meta::values<hal::InstrumentId>[1] == hal::InstrumentId::DcP5);
    static_assert( core::meta::values<hal::InstrumentId>[2] == hal::InstrumentId::DcP6);
    static_assert( core::meta::values<hal::InstrumentId>[3] == hal::InstrumentId::DcP7);
    static_assert( core::meta::values<hal::InstrumentId>[4] == hal::InstrumentId::Osc1);
    static_assert( core::meta::values<hal::InstrumentId>[5] == hal::InstrumentId::Wfg1);
    static_assert( core::meta::values<hal::InstrumentId>[6] == hal::InstrumentId::Swu1);
    static_assert( core::meta::values<hal::InstrumentId>[7] == hal::InstrumentId::Dmm2);

    //
    // -- The switch unit's five cards ------------------------------------------
    //
    // dev/rig/devices.inc's rows: four 34932A matrices and a 34941A RF mux, all
    // on box Swu1 -- the 34980A's switching faces, in its slots 1-5. The desk
    // had no switching at all until the fabric could drive this box; these
    // are what changed that.
    //
    static_assert( core::meta::values<hal::SwitchDeviceId>.size() == 5);
    static_assert( hal::modelOf( hal::SwitchDeviceId::Matrix1) == hal::SwitchDeviceModel::Keysight34932A);
    static_assert( hal::modelOf( hal::SwitchDeviceId::Matrix4) == hal::SwitchDeviceModel::Keysight34932A);
    static_assert( hal::modelOf( hal::SwitchDeviceId::RfMux1)  == hal::SwitchDeviceModel::Keysight34941A);
    static_assert( hal::detail::switchDevices[ 0].Card == hal::Card( 1));
    static_assert( hal::detail::switchDevices[ 4].Card == hal::Card( 5));
    static_assert( hal::detail::deviceBoxRows[ 0].Box == "Swu1");
    static_assert( hal::detail::deviceBoxRows[ 4].Box == "Swu1");

    //
    // -- And one route over them ---------------------------------------------------
    //
    // The desk's one terminal, dut::DeskTerminal at A/1/1, is reachable --
    // through slot 1's Matrix 2 -- and nothing else is, and no source is
    // hard-wired onto it: a cable from the supply is the script's to make.
    //
    static_assert(   hal::isWired( hal::VpcLocation{ hal::VpcRack::A, 1, 1 }, hal::WireRole::Force));
    static_assert( ! hal::isWired( hal::VpcLocation{ hal::VpcRack::A, 1, 3 }, hal::WireRole::Force));
    static_assert( ! hal::isSourceWired( hal::VpcLocation{ hal::VpcRack::A, 1, 1 }));

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
    static_assert( ! hal::isTapWiredInstrument( hal::InstrumentId::Dmm2));

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
    static_assert( hal::CardIdentifyingInstrument< hal::keysight_34980a::Chassis> );
    static_assert( hal::SafeableInstrument< hal::keysight_34980a::InternalDmm> );
    static_assert( ! hal::RelayHoldingInstrument< hal::keysight_34980a::InternalDmm> );
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
// The routes this desk has, stated as behaviour: Dmm2's own path is its bus
// relay, the terminal's is its crosspoint -- so a routed reading at the
// terminal closes 921 and 501 -- and an instrument with no wiring has no route
// to ask for, which is the throw a script would hit trying one.
//
TEST( DevRig, TheMetersRouteToTheTerminalIsItsBusRelayAndACrosspoint)
{
    EXPECT_EQ( hal::instrumentWiring.find( hal::InstrumentId::Dmm2), ( hal::Path{ HOP( Matrix1, 921) }));
    EXPECT_EQ( hal::connectorWiring.find( hal::VpcLocation{ hal::VpcRack::A, 1, 1 }), ( hal::Path{ ROW_COLUMN( Matrix1, 5, 1) }));

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

    ASSERT_EQ( bindings.size(), 8u);
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

    ASSERT_EQ( bindings.size(), 8u);
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

                if( !Silent.empty() && command.starts_with( Silent))
                {
                    return;   // a query this fake will not answer -- see Silent
                }

                if( command == "*IDN?")
                {
                    mReplies.push_back( mIdentity);
                }
                else if( command == "SYST:ERR?")
                {
                    mReplies.emplace_back( "+0,\"No error\"");
                }
                else if( command.starts_with( "SYST:CTYP? "))
                {
                    const auto slot = static_cast<std::size_t>( std::stoi( std::string( command.substr( 11))));
                    const auto card = slot >= 1 && slot <= Cards.size() ? Cards[ slot - 1] : std::string{};

                    mReplies.push_back( card.empty()
                        ? std::string( "\"Agilent Technologies,0,0,0\"")
                        : "\"Agilent Technologies," + card + ",MY0000000" + std::to_string( slot) + ",1.00\"");
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

            //
            // Commands starting with this get no reply -- a query the
            // instrument refused, which the driver then times out on.
            //
            std::string Silent;

            //
            // What SYST:CTYP? reports for slots 1, 2, ... -- the dev rack's
            // cards (see dev/rig/devices.inc) unless a test says otherwise;
            // "" is an empty slot, as is every slot past the end.
            //
            std::vector<std::string> Cards{ "34932A", "34932A", "34932A", "34932A", "34941A" };

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
    EXPECT_EQ( hal::boxOf( hal::InstrumentId::Dmm2), "Swu1");

    EXPECT_EQ( hal::instrumentsIn( "Swu1"),
               ( std::vector{ hal::InstrumentId::Swu1, hal::InstrumentId::Dmm2 }));

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
        hal::Address Dmm2Address = Dmm2.address();

        ~RestoreAddresses()
        {
            Dmm1.useAddress( Dmm1Address);
            DcP5.useAddress( DcP5Address);
            DcP6.useAddress( DcP6Address);
            DcP7.useAddress( DcP7Address);
            Osc1.useAddress( Osc1Address);
            Wfg1.useAddress( Wfg1Address);
            Swu1.useAddress( Swu1Address);
            Dmm2.useAddress( Dmm2Address);
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


//
// The switch unit's two faces, Swu1 and Dmm2, on one box: one session, two
// families each preparing it for themselves -- the chassis's identity check,
// and the meter's identity check plus its fitted-and-enabled checks. Asserted
// through startup's own contact pass, on this deployment's real globals, with
// one fake standing in for the mainframe.
//
TEST( DevRig, TheSwitchUnitsTwoFacesShareOneSessionAndPrepareItEach)
{
    const RestoreAddresses restore;

    auto bindings = hal::bindAddresses( planWithOnly( { { "Swu1", hal::Usb{ "MY53154781" } } }));

    auto log = std::make_shared<std::vector<std::string>>();

    Swu1.useTransport( std::make_unique<Recorder>( log, "swu", "Agilent Technologies,34980A,MY53154781,2.43-2.42-1.19"));

    ASSERT_NO_THROW( hal::contactInstruments( bindings));

    //
    // Every command went down the one fake -- Dmm2 never opened a session of
    // its own -- and the meter's checks were made, once.
    //
    EXPECT_FALSE( log->empty());
    EXPECT_EQ( std::ranges::count( *log, std::string( "swu: INST:DMM:INST?")), 1);
    EXPECT_EQ( std::ranges::count( *log, std::string( "swu: INST:DMM?")), 1);

    EXPECT_TRUE( Dmm2.address() == Swu1.address());
    EXPECT_FALSE( Dmm2.isSimulated());

    Swu1.closeSession();
}

//
// -- The cards, checked against the rack ---------------------------------------
//
// The contact pass asks each card row's slot what is in it, and a table that
// describes a different rack fails before the first script: the row's name,
// what it says, and what the slot says.
//
namespace
{
    //
    // The contact pass over Swu1 alone, on a fake whose slots hold these cards.
    // Returns what it threw, or "" if it did not.
    //
    auto contactTheSwitchUnitHolding( std::vector<std::string> cards,
                                      std::shared_ptr<std::vector<std::string>> log = std::make_shared<std::vector<std::string>>()) -> std::string
    {
        const RestoreAddresses restore;

        auto bindings = hal::bindAddresses( planWithOnly( { { "Swu1", hal::Usb{ "MY53154781" } } }));
        auto fake     = std::make_unique<Recorder>( log, "swu", "Agilent Technologies,34980A,MY53154781,2.43-2.42-1.19");

        fake->Cards = std::move( cards);

        Swu1.useTransport( std::move( fake));

        std::string refused;

        try
        {
            hal::contactInstruments( bindings);
        }
        catch( const std::runtime_error & failure)
        {
            refused = failure.what();
        }

        Swu1.closeSession();

        return refused;
    }
} // namespace

TEST( DevRig, ContactAsksEachCardRowsSlotOnce)
{
    auto log = std::make_shared<std::vector<std::string>>();

    EXPECT_EQ( contactTheSwitchUnitHolding( { "34932A", "34932A", "34932A", "34932A", "34941A" }, log), "");

    for( int slot = 1; slot <= 5; ++slot)
    {
        EXPECT_EQ( std::ranges::count( *log, "swu: SYST:CTYP? " + std::to_string( slot)), 1) << "slot " << slot;
    }

    //
    // Slots no row names are not asked about: an extra card in the rack is
    // not a table describing a different one.
    //
    EXPECT_EQ( firstIndexOf( *log, "swu: SYST:CTYP? 6"), -1);
}

TEST( DevRig, ASlotHoldingADifferentCardIsRefused)
{
    const auto refused = contactTheSwitchUnitHolding( { "34932A", "34921A", "34932A", "34932A", "34941A" });

    EXPECT_NE( refused.find( "Matrix2 is declared as a Keysight 34932A in slot 2 of box Swu1"), std::string::npos) << refused;
    EXPECT_NE( refused.find( "holds a 34921A"), std::string::npos) << refused;
}

TEST( DevRig, AnEmptySlotWithACardRowIsRefused)
{
    const auto refused = contactTheSwitchUnitHolding( { "34932A", "34932A", "34932A", "34932A", "" });

    EXPECT_NE( refused.find( "RfMux1 is declared as a Keysight 34941A in slot 5"), std::string::npos) << refused;
    EXPECT_NE( refused.find( "that slot is empty"), std::string::npos) << refused;
}

//
// -- The fabric, driving the switch unit -----------------------------------------
//
// hal::fabric with the rig's driver behind it, on this deployment's real cards
// and the real Chassis, with one fake standing in for the mainframe -- so the
// relay moves the fabric decides on are read off what reached the wire.
//
namespace
{
    //
    // Swu1 on a fake for the length of a test, and the fabric's books cleared
    // after it, so a test that fails half-way leaves nothing held for the next.
    //
    struct FabricOnAFake
    {
        std::shared_ptr<std::vector<std::string>> Log = std::make_shared<std::vector<std::string>>();
        Recorder *                                Wire{};

        FabricOnAFake()
        {
            auto fake = std::make_unique<Recorder>( Log, "swu", "Agilent Technologies,34980A,MY53154781,2.43-2.42-1.19");

            Wire = fake.get();

            Swu1.useTransport( std::move( fake));
        }

        ~FabricOnAFake()
        {
            hal::fabric.openAll();
            Swu1.closeSession();
        }

        //
        // The relay moves that reached the wire, in order.
        //
        [[nodiscard]]
        auto moves() const -> std::vector<std::string>
        {
            std::vector<std::string> found;

            for( const auto & line : *Log)
            {
                if( line.starts_with( "swu: ROUT:CLOS (") || line.starts_with( "swu: ROUT:OPEN ("))
                {
                    found.push_back( line.substr( 5));
                }
            }

            return found;
        }
    };
} // namespace

//
// A crosspoint on Matrix1 is slot 1 of the box: ROUT:CLOS (@1501). Its second
// use moves nothing, nor does its first release; its last release opens it.
//
TEST( DevRig, TheFabricMovesARelayAtItsFirstUseAndItsLastRelease)
{
    FabricOnAFake bench;

    hal::fabric.close( ROW_COLUMN( Matrix1, 5, 1));
    hal::fabric.close( ROW_COLUMN( Matrix1, 5, 1));
    hal::fabric.open( ROW_COLUMN( Matrix1, 5, 1));

    EXPECT_EQ( bench.moves(), ( std::vector<std::string>{ "ROUT:CLOS (@1501)" }));

    hal::fabric.open( ROW_COLUMN( Matrix1, 5, 1));

    EXPECT_EQ( bench.moves(), ( std::vector<std::string>{ "ROUT:CLOS (@1501)", "ROUT:OPEN (@1501)" }));
}

//
// The card's slot is the row's Card( n): Matrix4 is slot 4.
//
TEST( DevRig, EachCardIsTheSlotItsRowSays)
{
    FabricOnAFake bench;

    hal::fabric.close( ROW_COLUMN( Matrix4, 8, 16));

    EXPECT_EQ( bench.moves(), ( std::vector<std::string>{ "ROUT:CLOS (@4816)" }));
}

//
// An RF bank is a 1-of-4 with no open state, and a 34941A refuses ROUT:OPEN --
// so releasing one moves nothing, and the bank stays where it was put.
//
TEST( DevRig, ReleasingAnRfBankSendsNothing)
{
    FabricOnAFake bench;

    hal::fabric.close( BANK( RfMux1, 1, 4));
    hal::fabric.open( BANK( RfMux1, 1, 4));

    EXPECT_EQ( bench.moves(), ( std::vector<std::string>{ "ROUT:CLOS (@5104)" }));
    EXPECT_FALSE( hal::fabric.isClosed( BANK( RfMux1, 1, 4)));
}

//
// The point of all of it: a routed reading at the desk's terminal. Dmm2's path
// is its bus relay, the terminal's is its crosspoint -- closed, the bus read,
// both opened -- with the meter and the switching on the box's one session.
//
TEST( DevRig, ARoutedReadingClosesThePathReadsTheBusAndOpensIt)
{
    FabricOnAFake bench;

    const auto reading = Measure( Dmm2.voltage(), at( dut::DeskTerminal));

    EXPECT_EQ( reading, core::quantities::Voltage{ 1.0 });   // what the fake answers any query with

    EXPECT_EQ( bench.moves(), ( std::vector<std::string>{
        "ROUT:CLOS (@1921)", "ROUT:CLOS (@1501)", "ROUT:OPEN (@1921)", "ROUT:OPEN (@1501)" }));

    const auto measured = std::ranges::find( *bench.Log, std::string( "swu: MEAS:VOLT:DC?"));
    const auto closed   = std::ranges::find( *bench.Log, std::string( "swu: ROUT:CLOS (@1501)"));
    const auto opened   = std::ranges::find( *bench.Log, std::string( "swu: ROUT:OPEN (@1921)"));

    ASSERT_NE( measured, bench.Log->end());
    EXPECT_LT( closed, measured) << "the path is closed before the reading";
    EXPECT_LT( measured, opened) << "and opened after it";

    EXPECT_FALSE( hal::fabric.isClosed( HOP( Matrix1, 921)));
    EXPECT_FALSE( hal::fabric.isClosed( ROW_COLUMN( Matrix1, 5, 1)));
}

//
// And a reading that fails still releases its path -- real relays left closed
// onto a pin with nobody holding them is the failure a driven fabric could not
// afford.
//
TEST( DevRig, ARoutedReadingThatFailsStillOpensItsPath)
{
    FabricOnAFake bench;

    bench.Wire->Silent = "MEAS:";

    EXPECT_ANY_THROW( static_cast<void>( Measure( Dmm2.voltage(), at( dut::DeskTerminal))));

    const auto moves = bench.moves();

    EXPECT_NE( std::ranges::find( moves, std::string( "ROUT:OPEN (@1921)")), moves.end());
    EXPECT_NE( std::ranges::find( moves, std::string( "ROUT:OPEN (@1501)")), moves.end());
    EXPECT_FALSE( hal::fabric.isClosed( HOP( Matrix1, 921)));
}

