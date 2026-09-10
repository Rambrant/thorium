#include "hal/topology/address_plan.hpp"
#include "hal/topology/address_tables.hpp"

#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "core/meta.hpp"

//
// Reading an --address, and the rule that keeps hal::ReachableOver alive
// through a runtime resolution. The mechanism only -- what this deliberately
// does not touch is any particular bench.
//
// Which is why nothing here writes an instrument's name. hal::InstrumentId's
// enumerators come from the linking deployment's instrument.inc, so "Dmm1" is
// a fact about one rig and not about this parser: a test spelling it would
// build for this repo's bench and fail for dev/, which has one instrument
// with a different set of neighbours. The tests that genuinely need several
// distinct instruments -- the pool and site lookups, the whole resolution
// pass -- are in rig/tests/test_preflight.cpp for exactly that reason.
//
namespace
{
    using namespace hal;

    //
    // Whatever this deployment's first instrument is. Every deployment has at
    // least one, and which one it is never matters below: these tests are
    // about the half of an override after the '='.
    //
    [[nodiscard]]
    auto anInstrument() -> InstrumentId
    {
        return core::meta::values<InstrumentId>[ 0];
    }

    //
    // One parsed --address, with the text it was parsed from kept alive
    // beside it.
    //
    // Not a function returning the pair, which is the obvious shape and the
    // wrong one: parseOverride() hands back an address that *borrows* from
    // its input (see its own comment on why -- hal::Lan holds a
    // std::string_view, and the real caller's input is argv, which outlives
    // the process). A helper that built the text in a temporary would return
    // views into a string destroyed at the end of the full expression, and
    // the reads would mostly still look right.
    //
    // So this class is the contract, written down where it can be got wrong:
    // the text is a member, declared before the parse that reads it, and the
    // whole thing is immovable so that nothing can relocate the storage the
    // views point into.
    //
    class Override
    {
        public:
            explicit Override( const std::string_view address)
                : mText(   std::string( to_string( anInstrument())) + "=" + std::string( address)),
                  mParsed( parseOverride( mText)) {}

            Override( const Override &) = delete;
            auto operator=( const Override &) -> Override & = delete;

            [[nodiscard]] auto id()      const -> InstrumentId    { return mParsed.first;  }
            [[nodiscard]] auto address() const -> const Address & { return mParsed.second; }

        private:
            std::string                      mText;
            std::pair<InstrumentId, Address> mParsed;
    };

    // -- parseOverride: the four bus kinds, and the one with no payload -----

    TEST( AddressPlan, AnOverrideNamesTheInstrumentItIsFor)
    {
        const Override parsed{ "sim" };

        EXPECT_EQ( parsed.id(), anInstrument());
    }

    TEST( AddressPlan, ALanOverrideTakesTheDefaultPortWhenNoneIsGiven)
    {
        const Override parsed{ "lan:dev-dmm-3" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Lan>( address));
        EXPECT_EQ( std::get<Lan>( address).host, "dev-dmm-3");
        EXPECT_EQ( std::get<Lan>( address).port, 5025);
    }

    TEST( AddressPlan, ALanOverrideTakesAPortWhenOneIsGiven)
    {
        const Override parsed{ "lan:dev-dmm-3:5024" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Lan>( address));
        EXPECT_EQ( std::get<Lan>( address).host, "dev-dmm-3");
        EXPECT_EQ( std::get<Lan>( address).port, 5024);
    }

    //
    // The ambiguous spelling, and the reason the split is on the colon
    // *count* rather than on the last colon: a bare IPv6 literal is nothing
    // but colons, and "fe80::1" splits into host "fe80:" on port 1 under the
    // tempting rule -- both plausible, neither what anybody typed.
    //
    TEST( AddressPlan, AnIpv6LiteralIsNotSplitIntoAHostAndAPort)
    {
        const Override parsed{ "lan:fe80::1" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Lan>( address));
        EXPECT_EQ( std::get<Lan>( address).host, "fe80::1");
        EXPECT_EQ( std::get<Lan>( address).port, 5025);
    }

    TEST( AddressPlan, AUsbOverrideIsASerialNumber)
    {
        const Override parsed{ "usb:CN59176621" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Usb>( address));
        EXPECT_EQ( std::get<Usb>( address).serialNumber, "CN59176621");
    }

    TEST( AddressPlan, ASerialOverrideIsADevicePath)
    {
        const Override parsed{ "serial:/dev/ttyUSB1" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Serial>( address));
        EXPECT_EQ( std::get<Serial>( address).device, "/dev/ttyUSB1");
    }

    //
    // The one address with a comma in it, which is why --address is a
    // repeatable flag rather than a comma-separated list (see cli::Repeatable
    // in framework/runner/src/cli.hpp).
    //
    TEST( AddressPlan, AGpibOverrideIsABoardAndAPrimaryAddress)
    {
        const Override parsed{ "gpib:0,5" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Gpib>( address));
        EXPECT_EQ( std::get<Gpib>( address).board,   0);
        EXPECT_EQ( std::get<Gpib>( address).primary, 5);
        EXPECT_FALSE( std::get<Gpib>( address).secondary.has_value());
    }

    TEST( AddressPlan, AGpibOverrideMayCarryASecondaryAddress)
    {
        const Override parsed{ "gpib:0,5,3" };

        const auto & address = parsed.address();

        ASSERT_TRUE( std::holds_alternative<Gpib>( address));
        EXPECT_EQ( std::get<Gpib>( address).secondary, 3);
    }

    TEST( AddressPlan, SimulatedIsWrittenWithoutAPayload)
    {
        EXPECT_TRUE( std::holds_alternative<Simulated>( Override{ "sim" }.address()));
        EXPECT_TRUE( std::holds_alternative<Simulated>( Override{ "simulated" }.address()));
    }

    // -- parseOverride: what it refuses ------------------------------------

    TEST( AddressPlan, AnOverrideWithoutAnEqualsIsRefused)
    {
        EXPECT_THROW( ( void) parseOverride( "Dmm1 lan:host"), AddressSyntaxError);
    }

    //
    // The diagnostic lists the rig's own instruments rather than only saying
    // the given one is wrong: the person who mistyped it is at a bench, and
    // the list is short enough that the message can simply be the answer.
    //
    TEST( AddressPlan, AnUnknownInstrumentIsRefusedAndTheKnownOnesAreNamed)
    {
        try
        {
            ( void) parseOverride( "NotAnInstrument=lan:host");

            FAIL() << "expected an unknown instrument to be refused";
        }
        catch( const AddressSyntaxError & refused)
        {
            const std::string message{ refused.what() };

            EXPECT_NE( message.find( "NotAnInstrument"),          std::string::npos);
            EXPECT_NE( message.find( to_string( anInstrument())), std::string::npos);
        }
    }

    TEST( AddressPlan, AnUnknownBusKindIsRefused)
    {
        EXPECT_THROW( ( void) Override{ "vxi:host" }, AddressSyntaxError);
    }

    TEST( AddressPlan, AnAddressWithNoKindPrefixIsRefused)
    {
        EXPECT_THROW( ( void) Override{ "dev-dmm-3" }, AddressSyntaxError);
    }

    TEST( AddressPlan, AGpibAddressThatIsNotNumbersIsRefused)
    {
        EXPECT_THROW( ( void) Override{ "gpib:zero,five" }, AddressSyntaxError);
        EXPECT_THROW( ( void) Override{ "gpib:0" },         AddressSyntaxError);
        EXPECT_THROW( ( void) Override{ "gpib:0,5,3,1" },   AddressSyntaxError);
    }

    TEST( AddressPlan, ALanPortThatIsNotANumberIsRefused)
    {
        EXPECT_THROW( ( void) Override{ "lan:host:scpi" }, AddressSyntaxError);
    }

    TEST( AddressPlan, ABracketedIpv6AddressIsRefusedRatherThanMisread)
    {
        EXPECT_THROW( ( void) Override{ "lan:[fe80::1]:5025" }, AddressSyntaxError);
    }

    // -- the provenance an operator reads in the banner --------------------

    TEST( AddressPlan, EachSourceIsNamedTheWayAReaderWouldFindIt)
    {
        EXPECT_EQ( to_string( AddressSource::Table),    "table");
        EXPECT_EQ( to_string( AddressSource::Site),     "site");
        EXPECT_EQ( to_string( AddressSource::Pool),     "pool");
        EXPECT_EQ( to_string( AddressSource::Override), "--address");
    }

    // -- what a deployment with no site table answers ----------------------

    //
    // The state hal/topology/detail/no_sites.inc produces, asserted so that a
    // deployment acquiring a site table by accident is a failed test rather
    // than a surprise on a bench. Neither of this repo's deployments has one.
    //
    // This used to assert the same thing about pools, for both tables at once,
    // and could not keep doing it once one deployment had a real pool table
    // (dev/rig/pools.inc). The claim was never a framework claim in the first
    // place: these tests link whichever deployment the build selected, so
    // "there are no pools" is a fact about that deployment and belongs with
    // its own rig tests. Both halves of it are covered there and more
    // precisely than here --
    //
    //   rig/tests/test_preflight.cpp asserts every binding on the bench comes
    //   from AddressSource::Table, which a bench pool would break by turning
    //   one of them into AddressSource::Pool.
    //
    //   dev/rig/tests/test_dev_rig.cpp asserts the dev desk's pool is exactly
    //   the three candidates it declares, in order.
    //
    // What stays here is the half that is still true of every deployment in
    // this repo, and the reason it stays is that nothing else says it.
    //
    TEST( AddressTables, ADeploymentWithNoSiteTableDeclaresNoSites)
    {
        EXPECT_TRUE( siteNames().empty());
    }
} // namespace
