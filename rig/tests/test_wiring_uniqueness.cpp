#include "hal/topology/wiring.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

//
// A plain #include, repo-root-relative, exactly as
// dut/tests/test_wiring_coverage.cpp reaches this same file: the tables
// expand fully qualified, so nothing needs bringing into scope first.
//
#include "rig/wiring.inc"

//
// ---------------------------------------------------------------------------
// One endpoint per fabric element
// ---------------------------------------------------------------------------
// The fabric-side twin of dut/tests/test_wiring_coverage.cpp's
// NoTwoDutPointsShareAPin, and the same kind of quiet mistake: two
// WIRE_CONNECTOR rows on one mux channel compile, both pins read as wired,
// every existing check passes, and a measurement at one of them silently
// reads the other. Nothing about the failure looks like a wiring error. It is
// also exactly what a copy-pasted row produces -- duplicate the line above,
// change the pin, forget the channel -- which is how these tables are
// actually edited.
//
// Like every other check over this rig's own data, it has no runtime
// assertions: the entire point is that it compiles.
//
// This file lives here rather than beside that one because what it checks
// involves no DUT at all -- it is a claim about rig/wiring.inc against
// itself, the way test_wiring_coverage.cpp is a claim about dut/adapter.inc
// against rig/wiring.inc. It is the one file in rig/tests/ that expands the
// wiring tables rather than building a small local one, which is what
// checking the real table requires.
//
// -- What the rule actually is ----------------------------------------------
//
// Not "no element appears twice": this rig shares relays on purpose, and has
// to. All ten routed pins pass through the one crosspoint that puts Mux1's
// common on the measurement bus, and hal::SwitchFabric use-counts exactly so
// that several routes can hold one relay at once (see its own comment).
// Banning that would ban the topology.
//
// What cannot be shared is the relay at the *endpoint* end of a path -- hop
// zero, the one that says "this pin, not its neighbour" (see hal::WiringHop
// on why a Path is written endpoint-first). So: an element that identifies
// one entry must not appear anywhere in another entry. That catches three
// distinct mistakes with one rule:
//
//   - two pins, or two instruments, given the same channel
//   - a pin and an instrument given the same channel
//   - a shared trunk hop written as some other endpoint's own relay -- a
//     connector path reaching the bus through Osc1's crosspoint instead of
//     the mux common's, say, which no coverage check would notice
//
// Compared by *entry*, not by owner, which matters for the one instrument
// that legitimately holds several rows: hal::Ac6834B's AcP1 has four, one per
// conductor, and two of them on one relay is as much a mistake as any other
// collision. Sharing a later hop between rows of one owner stays legal -- a
// bundle whose conductors run through a common trunk is an ordinary bench.
//
namespace thorium_channel_ownership_check
{
    //
    // Both tables, end to end. Concatenated here rather than by the macros
    // because the macros each see only their own rows -- and because the rule
    // is about the two tables together: an instrument and a connector pin
    // claiming one relay is a collision like any other.
    //
    consteval auto allHops() -> std::vector<hal::WiringHop>
    {
        std::vector<hal::WiringHop> hops;

        for( const auto hop : hal::instrumentWiringHops)
        {
            hops.push_back( hop);
        }

        for( const auto hop : hal::connectorWiringHops)
        {
            hops.push_back( hop);
        }

        return hops;
    }

    constexpr auto hops = std::define_static_array( allHops());

    //
    // Same row of the same table. Side matters: the two tables number their
    // rows independently, so entry 0 of one is not entry 0 of the other.
    //
    consteval auto sameEntry( const hal::WiringHop left, const hal::WiringHop right) -> bool
    {
        return left.owner.side == right.owner.side && left.entry == right.entry;
    }

    consteval auto claimedElsewhere( const hal::WiringHop hop) -> bool
    {
        if( ! hop.identifies)
        {
            return false;
        }

        for( const auto other : hops)
        {
            if( ! sameEntry( hop, other) && other.element == hop.element)
            {
                return true;
            }
        }

        return false;
    }

    //
    // Message-building, all of it consteval -- a static_assert message is a
    // constant expression like any other (C++26), and a failure that names the
    // relay and the endpoint claiming it is the difference between fixing the
    // row and hunting for it. std::to_string is not constexpr, hence the
    // digits by hand.
    //
    consteval auto digits( std::size_t value) -> std::string
    {
        if( value == 0)
        {
            return "0";
        }

        std::string text;

        while( value > 0)
        {
            text.insert( text.begin(), static_cast<char>( '0' + ( value % 10)));
            value /= 10;
        }

        return text;
    }

    consteval auto describe( const hal::SwitchElementId element) -> std::string
    {
        return std::string( core::meta::to_string( element.device)) + " channel " + digits( element.channel);
    }

    consteval auto describe( const hal::WiringOwner owner) -> std::string
    {
        const auto role = owner.role == hal::WireRole::Sense ? std::string( " sense") : std::string();

        if( owner.side == hal::WiringSide::Instrument)
        {
            return std::string( core::meta::to_string( owner.instrument)) + role;
        }

        return std::string( core::meta::to_string( owner.location.rack)) + "-" +
               digits( owner.location.connector) + "-" + digits( owner.location.pin) + role;
    }

    //
    // One static_assert per hop rather than one for the table, so a failure
    // names the offending row -- the same reason the coverage check next door
    // walks its points with a template for.
    //
    consteval auto checkChannelOwnership() -> bool
    {
        template for( constexpr auto hop : hops)
        {
            static_assert( ! claimedElsewhere( hop),
                           std::string( "two endpoints claim one fabric element in rig/wiring.inc -- ") +
                           describe( hop.element) + " is " + describe( hop.owner) +
                           "'s own relay (hop zero of its entry) and also appears in another entry's path. "
                           "A shared trunk hop is fine and this rig has one; an endpoint's own relay is not. "
                           "See hal::WiringHop, and note that a Path is written endpoint-first.");
        }

        return true;
    }

    constexpr bool rigChannelsOwnedOnce = checkChannelOwnership();
} // namespace thorium_channel_ownership_check

TEST( RigWiringUniqueness, NoTwoEndpointsClaimOneFabricElement)
{
    //
    // Nothing to run: reaching this line at all means every static_assert
    // above already held. See the file comment.
    //
    static_assert( thorium_channel_ownership_check::rigChannelsOwnedOnce);

    SUCCEED();
}

//
// The other half of the same question, and the one the rule above deliberately
// does not answer: which elements ARE shared, and by how many entries. Left as
// a runtime assertion rather than a static_assert because it is a description
// of this rig rather than a rule -- a rig with two mux commons would have two
// shared crosspoints, or none if every instrument had its own column, and
// neither would be wrong.
//
// It is here so that the sharing the rule permits is stated somewhere rather
// than merely tolerated: if this number moves, the topology moved.
//
TEST( RigWiringUniqueness, TheOnlySharedElementIsTheMuxCommonsCrosspoint)
{
    constexpr auto shared = hal::SwitchElementId{ hal::SwitchDeviceId::Matrix1, 300 };

    std::size_t sharing = 0;
    std::size_t others  = 0;

    for( const auto hop : thorium_channel_ownership_check::hops)
    {
        if( hop.identifies)
        {
            continue;
        }

        ( hop.element == shared ? sharing : others) += 1;
    }

    // Every routed VPC pin's second hop, and nothing else anywhere is a
    // non-identifying hop at all -- every other entry on this rig is one hop
    // long (see rig/wiring.inc).
    EXPECT_EQ( sharing, 10u);
    EXPECT_EQ( others,  0u);
}
