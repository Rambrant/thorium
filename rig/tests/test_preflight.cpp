#include "hal/verbs/preflight.hpp"
#include "hal/topology/address_tables.hpp"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "core/session/bench.hpp"
#include "hal/topology/active_instruments.hpp"

//
// The resolution pass over *this* rig's instruments -- which is why it is
// here and not in framework/hal/tests/: hal::bindAddresses() reflects over
// the global namespace for InstrumentTag-derived variables, so it has nothing
// to bind until a deployment's instrument.inc has declared some. The generic
// half (reading an --address, the table lookups) is tested without a rig in
// framework/hal/tests/topology/test_address_plan.cpp.
//
// Every test here restores what it changed. The instruments are process-wide
// globals and bindAddresses() writes to them, so a test that rebound Ser1 and
// left it rebound would be handing the next test a different rig -- and,
// worse, would leave whichever test ran last in charge of what the rest of
// this binary's suites see.
//
namespace
{
    using namespace hal;

    //
    // Put every instrument back where this test found it.
    //
    // Snapshot and replay, rather than the obvious "bind an empty plan again"
    // -- and the reason is worth knowing before writing any test here.
    // bindAddresses() takes each row's *current* address as the baseline it
    // resolves from, because that is where the table's own address lives
    // once the globals are constructed (see that function's own comment).
    // Re-binding an empty plan therefore re-reads whatever the last test
    // left behind and calls it the table's, which restores nothing at all.
    //
    // So the original values are captured on the way in and pushed back as
    // overrides on the way out. Every one of them came off the driver it is
    // going back to, so every one passes that driver's back panel.
    //
    class RigAddresses
    {
        public:
            RigAddresses() : mOriginal( bindAddresses( AddressPlan{})) {}

            ~RigAddresses()
            {
                AddressPlan plan;

                for( const auto & binding : mOriginal)
                {
                    plan.Overrides.emplace_back( binding.Id, binding.Value);
                }

                ( void) bindAddresses( plan);
            }

            RigAddresses( const RigAddresses &) = delete;
            auto operator=( const RigAddresses &) -> RigAddresses & = delete;

        private:
            std::vector<Binding> mOriginal;
    };

    [[nodiscard]]
    auto bindingFor( const std::vector<Binding> & bindings, const InstrumentId id) -> Binding
    {
        const auto found = std::ranges::find( bindings, id, &Binding::Id);

        return found == bindings.end() ? Binding{} : *found;
    }

    // -- what an unconfigured run binds ------------------------------------

    TEST( Preflight, EveryInstrumentInTheTableGetsABinding)
    {
        const RigAddresses restore;

        const auto bindings = bindAddresses( AddressPlan{});

        //
        // Against the enum rather than a count: the enum is generated from
        // rig/instrument.inc (see hal/driver/instrument.hpp), so this asserts
        // "every row in the table" and stays true when a row is added, where
        // a literal 8 would be a second copy of the table's length.
        //
        EXPECT_EQ( bindings.size(), core::meta::values<InstrumentId>.size());

        for( const auto id : core::meta::values<InstrumentId>)
        {
            EXPECT_EQ( bindingFor( bindings, id).Id, id) << "no binding for " << to_string( id);
        }
    }

    TEST( Preflight, AnUnconfiguredRunTakesEveryAddressFromTheTable)
    {
        const RigAddresses restore;

        for( const auto & binding : bindAddresses( AddressPlan{}))
        {
            EXPECT_EQ( binding.Source, AddressSource::Table) << to_string( binding.Id);
        }
    }

    //
    // The address a binding reports and the address the driver holds are the
    // same one -- which is the entire job of pass one, and the thing a test
    // of its return value alone would not catch.
    //
    TEST( Preflight, ABindingsAddressIsTheOneTheDriverIsHolding)
    {
        const RigAddresses restore;

        const auto bindings = bindAddresses( AddressPlan{});

        EXPECT_EQ( bindingFor( bindings, InstrumentId::Ser1).Value, Ser1.address());
        EXPECT_EQ( bindingFor( bindings, InstrumentId::AcP1).Value, AcP1.address());
    }

    //
    // The type column comes off the reflection, and the trim it goes through
    // is worth asserting rather than eyeballing in a banner: an alias is not
    // preserved by reflection, so DcP5's row renders its underlying
    // specialisation, and untrimmed that is a hundred characters of repeated
    // namespace (see hal::detail::typeNameOf).
    //
    TEST( Preflight, TheTypeColumnNamesTheDriverWithoutRepeatingItsNamespace)
    {
        const RigAddresses restore;

        const auto bindings = bindAddresses( AddressPlan{});

        EXPECT_EQ( bindingFor( bindings, InstrumentId::Dmm1).Type, "keysight_edu34450a::EDU34450A");
        EXPECT_EQ( bindingFor( bindings, InstrumentId::DcP5).Type,
                   "keysight_edu36311a::EDU36311A<Output1, DirectWiring>");
    }

    //
    // Which rows have a session, recorded at bind time. AcP1 and Ser1 do not
    // and never did -- see hal::ContactableInstrument on why that is a fact
    // this reports rather than a build it refuses.
    //
    TEST( Preflight, TheRowsWithNoSessionAreMarkedAsSuch)
    {
        const RigAddresses restore;

        const auto bindings = bindAddresses( AddressPlan{});

        EXPECT_TRUE(  bindingFor( bindings, InstrumentId::Dmm1).CanContact);
        EXPECT_TRUE(  bindingFor( bindings, InstrumentId::Osc1).CanContact);
        EXPECT_FALSE( bindingFor( bindings, InstrumentId::AcP1).CanContact);
        EXPECT_FALSE( bindingFor( bindings, InstrumentId::Ser1).CanContact);
    }

    // -- overrides ---------------------------------------------------------

    TEST( Preflight, AnOverrideMovesTheRowAndSaysWhereItCameFrom)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=serial:/dev/ttyUSB9"));

        const auto binding = bindingFor( bindAddresses( plan), InstrumentId::Ser1);

        EXPECT_EQ( binding.Source, AddressSource::Override);
        EXPECT_EQ( std::get<Serial>( binding.Value).device, "/dev/ttyUSB9");
        EXPECT_EQ( std::get<Serial>( Ser1.address()).device, "/dev/ttyUSB9");
    }

    TEST( Preflight, AnOverrideLeavesEveryOtherRowAlone)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=serial:/dev/ttyUSB9"));

        for( const auto & binding : bindAddresses( plan))
        {
            if( binding.Id != InstrumentId::Ser1)
            {
                EXPECT_EQ( binding.Source, AddressSource::Table) << to_string( binding.Id);
            }
        }
    }

    //
    // Two overrides for one row is a caller contradicting themselves, and the
    // first wins -- which is what makes "environment first, flags appended
    // after" the wrong way round and "flags first" the right one (see
    // framework/runner/src/main.cpp, which builds the list in that order).
    //
    TEST( Preflight, TheFirstOverrideForARowWins)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=serial:/dev/first"));
        plan.Overrides.push_back( parseOverride( "Ser1=serial:/dev/second"));

        EXPECT_EQ( std::get<Serial>( bindingFor( bindAddresses( plan), InstrumentId::Ser1).Value).device,
                   "/dev/first");
    }

    // -- the rule that keeps ReachableOver alive ---------------------------

    //
    // The runtime face of hal::ReachableOver: an address is checked against
    // the driver's own back panel, which is the same list its constructor is
    // constrained by. Ser1 is a Racal1260 -- Serial or Gpib, no network
    // connector -- so a Lan address for it is refused here exactly as it
    // would have failed to compile in the table.
    //
    TEST( Preflight, AnOverrideMayNotNameABusTheInstrumentHasNoConnectorFor)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=lan:some-host"));

        EXPECT_THROW( ( void) bindAddresses( plan), AddressKindMismatch);
    }

    //
    // The other connector of a two-connector panel is allowed, which is the
    // half a same-kind rule would have got wrong: Ser1 is cabled to a PC
    // serial port today and its driver also accepts Gpib, for the
    // arrangement where a chassis module provides the port instead (see
    // hal::racal1260::Racal1260's own constructor comment).
    //
    TEST( Preflight, AnOverrideMayNameAnyBusTheInstrumentDoesHave)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=gpib:0,9"));

        const auto binding = bindingFor( bindAddresses( plan), InstrumentId::Ser1);

        EXPECT_EQ( binding.Source, AddressSource::Override);
        EXPECT_EQ( std::get<Gpib>( binding.Value).primary, 9);
    }

    TEST( Preflight, ARefusedOverrideNamesTheDriverAndTheConnectorsItHas)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "AcP1=usb:CN0001"));

        try
        {
            ( void) bindAddresses( plan);

            FAIL() << "expected an absent connector to be refused";
        }
        catch( const AddressKindMismatch & refused)
        {
            const std::string message{ refused.what() };

            EXPECT_NE( message.find( "AcP1"),         std::string::npos);
            EXPECT_NE( message.find( "Ac6834B"),      std::string::npos);
            EXPECT_NE( message.find( "Usb"),          std::string::npos);
            EXPECT_NE( message.find( "Gpib, Serial"), std::string::npos);
        }
    }

    //
    // A Simulated{} row can be pointed at a real instrument, and this is the
    // case the rule exists to allow rather than the one it exists to refuse.
    //
    // Most of this rig says Simulated{} today, because rig/instrument.inc's
    // own rule says a row must until there is hardware at it. The bring-up
    // order that follows from this test is: plug the meter in, name it on the
    // command line, confirm it answers, and only then write the row -- with
    // the serial the banner printed rather than one somebody typed hopefully
    // into the table first.
    //
    TEST( Preflight, ASimulatedRowMayBePointedAtARealInstrument)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Dmm1=usb:MY60012345"));

        const auto binding = bindingFor( bindAddresses( plan), InstrumentId::Dmm1);

        EXPECT_EQ( binding.Source, AddressSource::Override);
        EXPECT_EQ( std::get<Usb>( binding.Value).serialNumber, "MY60012345");
        EXPECT_EQ( std::get<Usb>( Dmm1.address()).serialNumber, "MY60012345");
    }

    //
    // And the check is still a check: an EDU34450A has LAN and USB, a
    // DSOX1202G has USB and no network connector at all, and being Simulated
    // today does not lend either of them a connector it does not have.
    //
    TEST( Preflight, ASimulatedRowStillOnlyAcceptsItsOwnConnectors)
    {
        const RigAddresses restore;

        AddressPlan lan;

        lan.Overrides.push_back( parseOverride( "Dmm1=lan:dev-dmm-3"));

        EXPECT_NO_THROW( ( void) bindAddresses( lan));

        AddressPlan refused;

        refused.Overrides.push_back( parseOverride( "Osc1=lan:dev-scope"));

        EXPECT_THROW( ( void) bindAddresses( refused), AddressKindMismatch);
    }

    //
    // The reverse move, which the same rule allows for free: taking one live
    // instrument out of a run without touching the table. Every panel accepts
    // Simulated (see hal::ReachableOver's deliberate hole).
    //
    TEST( Preflight, AnyRowMayBeDetachedByNamingSimulated)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=sim"));

        const auto binding = bindingFor( bindAddresses( plan), InstrumentId::Ser1);

        EXPECT_TRUE( std::holds_alternative<Simulated>( binding.Value));
    }

    // -- sites -------------------------------------------------------------

    //
    // This deployment has no site table, and asking for one is refused rather
    // than quietly resolving nothing -- a bench PC configured for a fleet it
    // is not part of is a mistake worth hearing about at startup.
    //
    TEST( Preflight, ASiteThisDeploymentDoesNotDeclareIsRefused)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Site = "Bench02";

        EXPECT_THROW( ( void) bindAddresses( plan), AddressSyntaxError);
    }

    TEST( Preflight, ADeploymentWithNoSitesSaysSoRatherThanListingNone)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Site = "Bench02";

        try
        {
            ( void) bindAddresses( plan);

            FAIL() << "expected an undeclared site to be refused";
        }
        catch( const AddressSyntaxError & refused)
        {
            EXPECT_NE( std::string( refused.what()).find( "none at all"), std::string::npos);
        }
    }

    // -- contact, and what a detached run must not do ----------------------

    //
    // The boundary hal::safeRig() draws, drawn again here: a --replay at a
    // desk must not open a socket to whatever bench this binary's table
    // names. Asserted through the bindings rather than by watching for
    // traffic, because "no identity was recorded" is exactly what "nothing
    // was asked" looks like from here.
    //
    TEST( Preflight, ADetachedRunContactsNothing)
    {
        const RigAddresses restore;

        auto bindings = bindAddresses( AddressPlan{});

        core::bench().detach();
        contactInstruments( bindings);
        core::bench().attach();

        for( const auto & binding : bindings)
        {
            EXPECT_TRUE( binding.Identity.empty()) << to_string( binding.Id);
        }
    }

    //
    // An attached run over this rig contacts nothing either, and for a
    // different reason worth keeping separate: every row that could open a
    // session says Simulated{}, and there is genuinely nothing at the other
    // end of one (see rig/instrument.inc's own rule). A preflight that tried
    // anyway would fail every run on this bench.
    //
    TEST( Preflight, ASimulatedRigIsNotContactedEither)
    {
        const RigAddresses restore;

        auto bindings = bindAddresses( AddressPlan{});

        EXPECT_NO_THROW( contactInstruments( bindings));

        for( const auto & binding : bindings)
        {
            EXPECT_TRUE( binding.Identity.empty()) << to_string( binding.Id);
        }
    }

    // -- the banner --------------------------------------------------------

    TEST( Preflight, TheBannerHasOneLinePerInstrument)
    {
        const RigAddresses restore;

        EXPECT_EQ( bannerLines( bindAddresses( AddressPlan{})).size(),
                   core::meta::values<InstrumentId>.size());
    }

    //
    // Every line carries the five columns, and the last one is the whole
    // point of the banner: it says which of the ways of having no identity
    // this row is, rather than leaving a blank that could mean any of them.
    //
    TEST( Preflight, EachLineSaysTheIdTheDriverTheAddressAndWhereItCameFrom)
    {
        const RigAddresses restore;

        const auto lines = bannerLines( bindAddresses( AddressPlan{}));

        const auto lineFor = [ & ]( const std::string_view id) -> std::string
        {
            const auto found = std::ranges::find_if( lines,
                [ & ]( const std::string & line) { return line.starts_with( id); });

            return found == lines.end() ? std::string{} : *found;
        };

        const auto dmm1 = lineFor( "Dmm1");

        EXPECT_NE( dmm1.find( "keysight_edu34450a::EDU34450A"), std::string::npos);
        EXPECT_NE( dmm1.find( "Simulated"),                     std::string::npos);
        EXPECT_NE( dmm1.find( "table"),                         std::string::npos);
        EXPECT_NE( dmm1.find( "simulated -- no instrument"),    std::string::npos);

        //
        // AcP1 has a real GPIB address and no session at all, which is a
        // different sentence from Dmm1's and has to read as one.
        //
        const auto acp1 = lineFor( "AcP1");

        EXPECT_NE( acp1.find( "Gpib 0::5"),  std::string::npos);
        EXPECT_NE( acp1.find( "no session"), std::string::npos);
    }

    TEST( Preflight, TheBannerNamesTheFlagWhenAnOverrideMovedARow)
    {
        const RigAddresses restore;

        AddressPlan plan;

        plan.Overrides.push_back( parseOverride( "Ser1=serial:/dev/ttyUSB9"));

        const auto lines = bannerLines( bindAddresses( plan));
        const auto found = std::ranges::find_if( lines,
            [ ]( const std::string & line) { return line.starts_with( "Ser1"); });

        ASSERT_NE( found, lines.end());
        EXPECT_NE( found->find( "/dev/ttyUSB9"), std::string::npos);
        EXPECT_NE( found->find( "--address"),    std::string::npos);
    }

    //
    // Columns, not just fields: the banner is read by eye down a page, and a
    // ragged one is the difference between spotting the odd serial out and
    // not. Asserted as "every line agrees where the second column starts",
    // which is the property, rather than by pinning a width that any new
    // instrument would change.
    //
    TEST( Preflight, TheColumnsLineUp)
    {
        const RigAddresses restore;

        const auto lines = bannerLines( bindAddresses( AddressPlan{}));

        ASSERT_FALSE( lines.empty());

        const auto column = lines.front().find( "keysight");

        ASSERT_NE( column, std::string::npos);

        for( const auto & line : lines)
        {
            EXPECT_EQ( line.find_first_not_of( ' ', line.find( ' ')), column) << line;
        }
    }

    // -- the two optional tables' lookups ----------------------------------

    //
    // Tested against locally built entry vectors rather than against a
    // deployment's tables -- which is the whole reason hal::detail::poolIn
    // and its two siblings take their entries as a parameter (see
    // hal/topology/address_tables.hpp). The semantics worth pinning down are
    // properties of the lookup, and neither of this repo's deployments has
    // either table.
    //
    // Here rather than beside the parser in framework/hal/tests/ because they
    // need several distinct instruments to say anything, and how many a
    // deployment has is exactly what varies between them: dev/ has one.
    //

    TEST( AddressPools, CandidatesComeBackInTableOrder)
    {
        const std::vector<PoolEntry> entries{
            { InstrumentId::Dmm1, Lan{ "dev-dmm-1" } },
            { InstrumentId::Osc1, Usb{ "CN0001"    } },
            { InstrumentId::Dmm1, Lan{ "dev-dmm-2" } }
        };

        const auto candidates = detail::poolIn( entries, InstrumentId::Dmm1);

        ASSERT_EQ( candidates.size(), 2u);
        EXPECT_EQ( std::get<Lan>( candidates[ 0]).host, "dev-dmm-1");
        EXPECT_EQ( std::get<Lan>( candidates[ 1]).host, "dev-dmm-2");
    }

    //
    // Empty rather than an error, and that is what makes a pool table
    // optional per row: every instrument it does not mention resolves from
    // its own column, and nothing has to say so.
    //
    TEST( AddressPools, AnInstrumentWithNoRowsHasNoPool)
    {
        const std::vector<PoolEntry> entries{ { InstrumentId::Dmm1, Lan{ "dev-dmm-1" } } };

        EXPECT_TRUE( detail::poolIn( entries, InstrumentId::Osc1).empty());
    }

    TEST( AddressSites, ASiteRowIsFoundByBothItsNameAndItsInstrument)
    {
        const std::vector<SiteEntry> entries{
            { "Bench01", InstrumentId::Osc1, Usb{ "CN0001" } },
            { "Bench02", InstrumentId::Osc1, Usb{ "CN0002" } }
        };

        const auto found = detail::siteAddressIn( entries, "Bench02", InstrumentId::Osc1);

        ASSERT_TRUE( found.has_value());
        EXPECT_EQ( std::get<Usb>( *found).serialNumber, "CN0002");
    }

    //
    // The fall-through that makes a site table worth having: a site names
    // only the rows that actually differ between benches, and every other row
    // keeps the address in its own instrument.inc column.
    //
    TEST( AddressSites, ASiteThatDoesNotNameARowLeavesItAlone)
    {
        const std::vector<SiteEntry> entries{ { "Bench01", InstrumentId::Osc1, Usb{ "CN0001" } } };

        EXPECT_FALSE( detail::siteAddressIn( entries, "Bench01", InstrumentId::Dmm1).has_value());
    }

    TEST( AddressSites, EachSiteIsNamedOnceHoweverManyRowsItHas)
    {
        const std::vector<SiteEntry> entries{
            { "Bench01", InstrumentId::Osc1, Usb{ "CN0001"  } },
            { "Bench01", InstrumentId::Dmm1, Lan{ "b01-dmm" } },
            { "Bench02", InstrumentId::Osc1, Usb{ "CN0002"  } }
        };

        EXPECT_EQ( detail::siteNamesIn( entries), ( std::vector<std::string_view>{ "Bench01", "Bench02" }));
    }
} // namespace
