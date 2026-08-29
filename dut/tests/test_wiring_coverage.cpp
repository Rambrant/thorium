#include "hal/topology/adapter.hpp"
#include "hal/topology/wiring.hpp"

#include <gtest/gtest.h>

#include <array>
#include <meta>
#include <optional>
#include <vector>

//
// A plain #include each: ADAPTER/POINT and CONNECTOR_WIRING/WIRE_CONNECTOR
// (see hal/topology/adapter.hpp, hal/topology/wiring.hpp) both expand fully qualified, so
// nothing else needs to be brought into scope first. This is deliberately
// the one file that includes both -- see hal/topology/wiring.hpp's own comment on
// isWired() for why the check below can't live inside core::MeasureEngine
// itself.
//
#include "dut/adapter.inc"
#include "rig/wiring.inc"

//
// Like test_criteria_variants_compile.cpp in suite/tests/: this file has no
// runtime assertions of its own -- the entire point is that it compiles.
// A dut POINT with no matching rig/wiring.inc entry (a typo
// in either file, or a POINT added without ever wiring it up) fails the
// build here, the moment anyone builds this target, rather than only when
// a script happens to exercise that exact point.
//
namespace thorium_wiring_coverage_check
{
    //
    // Every POINT in Group, as its VpcLocation -- reflects over Group's
    // static AdapterPointTag<Loc, Kind> members and pulls each one's Loc
    // straight out of the template argument, without needing to splice the
    // point object itself. Homogeneous (every point in one ADAPTER shares
    // Loc's type -- hal::VpcLocation here), unlike core::meta::all<Group>()
    // in core/criteria/criterion.hpp, which needs a tuple because CRIT members hold
    // different Predicate types; a plain array is all this needs.
    //
    // Filtered by kind, because the two kinds of point are checked against
    // opposite tables: a POINT must be reachable through the fabric, a
    // SOURCE_POINT must not be (see core::PointKind, and hal::SourceWiring
    // in hal/topology/wiring.hpp). Passing the kind as a parameter rather than
    // writing two near-identical walks keeps one definition of "what counts
    // as a point of this adapter" -- a distinction that only matters when a
    // third kind is added, which is exactly when a duplicated walk would
    // quietly cover only some of them.
    //
    //
    // True for a nested group that opted into being one physical interface
    // by inheriting core::AdapterBundleTag -- what BUNDLE builds. Checked
    // by base rather than by shape so an unrelated nested type can never be
    // mistaken for one; see that tag's own comment.
    //
    consteval auto isBundle( const std::meta::info type) -> bool
    {
        for( const auto base : std::meta::bases_of( type, std::meta::access_context::current()))
        {
            if( std::meta::type_of( base) == ^^core::AdapterBundleTag)
            {
                return true;
            }
        }

        return false;
    }

    //
    // Recursive, so a LINE inside a BUNDLE is covered by every rule a
    // top-level POINT is. This matters more than it looks: a grouping
    // feature whose points quietly escape the coverage and pin-uniqueness
    // checks would be worse than no grouping at all -- the pins would look
    // declared and be unverified.
    //
    // `visited` guards the recursion rather than assuming members_of()
    // yields no self-reference: a struct's injected-class-name is a member
    // of the struct and reflects to the very type being walked, so a bundle
    // would otherwise recurse into itself forever.
    //
    consteval auto adapterPointLocationInfos( const std::meta::info                 groupType,
                                              const std::optional<core::PointKind>  kind,
                                              std::vector<std::meta::info> &        visited) -> std::vector<std::meta::info>
    {
        std::vector<std::meta::info> result;

        for( const auto seen : visited)
        {
            if( seen == groupType)
            {
                return result;
            }
        }

        visited.push_back( groupType);

        for( auto member : std::meta::members_of( groupType, std::meta::access_context::current()))
        {
            if( std::meta::is_type( member) && isBundle( member))
            {
                const auto nested = adapterPointLocationInfos( member, kind, visited);

                result.insert( result.end(), nested.begin(), nested.end());
                continue;
            }

            if( ! std::meta::is_variable( member))
            {
                continue;
            }

            auto type = std::meta::remove_cv( std::meta::type_of( member));

            if( ! std::meta::has_template_arguments( type))
            {
                continue;
            }

            if( std::meta::template_of( type) != ^^core::AdapterPointTag)
            {
                continue;
            }

            // template_arguments_of(type) is (Loc, Kind) -- AdapterPointTag's
            // two template parameters, in order (see core/topology/adapter.hpp). Kind
            // is always present even where POINT left it defaulted, since a
            // default template argument is still part of the instantiated
            // type.
            const auto arguments = std::meta::template_arguments_of( type);

            if( kind && std::meta::extract<core::PointKind>( arguments[ 1]) != *kind)
            {
                continue;
            }

            result.push_back( arguments[ 0]);
        }

        return result;
    }

    //
    // The spelling every check below uses -- the recursion's `visited`
    // bookkeeping is an implementation detail of the walk, not something a
    // caller should have to start off correctly.
    //
    template<typename Group>
    consteval auto adapterPointLocationInfos( const std::optional<core::PointKind> kind = std::nullopt) -> std::vector<std::meta::info>
    {
        std::vector<std::meta::info> visited;

        return adapterPointLocationInfos( ^^Group, kind, visited);
    }

    // Every point, both kinds -- for the checks that apply to any pin the
    // adapter names at all (pin uniqueness below).
    template<typename Group>
    constexpr auto adapterPointLocationRefs = std::define_static_array( adapterPointLocationInfos<Group>());

    template<typename Group>
    constexpr auto signalPointLocationRefs = std::define_static_array( adapterPointLocationInfos<Group>( core::PointKind::Signal));

    template<typename Group>
    constexpr auto sourcePointLocationRefs = std::define_static_array( adapterPointLocationInfos<Group>( core::PointKind::Source));

    //
    // One static_assert per point (via template for, not a single combined
    // check) -- a failure names the exact template instantiation (and so,
    // the exact VpcLocation) that has no CONNECTOR_WIRING entry, rather
    // than just "something in this adapter is unwired".
    //
    template<typename Group>
    consteval auto checkCoverage() -> bool
    {
        template for( constexpr auto locationRef : signalPointLocationRefs<Group>)
        {
            constexpr auto location = [: locationRef :];

            //
            // Routed *or* tapped. A point is covered when this rig can reach
            // it, and there are two ways to reach a pin: through the fabric
            // (a WIRE_CONNECTOR row) or by an instrument's leads being bolted
            // to it (a WIRE_TAP row -- see hal::TapWiring).
            //
            // This used to be isWired() alone, which quietly made "covered"
            // mean "routed" and so made a POINT undeclarable on a bench with
            // no switching hardware: a WIRE_CONNECTOR row needs a HOP, a HOP
            // needs a card, and such a bench has none. What that cost was not
            // routing -- which that bench never wanted -- but the DUT
            // vocabulary: every reading on it had to be spelled point-free
            // and keyed by the instrument.
            //
            // Deliberately an `or` and not a stricter rule about which. A pin
            // can legitimately be both: a rack rig can have a mux channel on
            // a pin *and* a scope probe clipped to it, and neither row makes
            // the other wrong. What is being asserted is reachability, which
            // is exactly what a script pointing at this pin depends on.
            //
            static_assert( hal::isWired( location) || hal::isTapWired( location),
                          "a dut POINT has no matching WIRE_CONNECTOR or WIRE_TAP entry in "
                          "rig/wiring.inc -- this rig neither routes to this pin nor cables an "
                          "instrument onto it, so nothing can measure there. See hal::isWired() "
                          "and hal::isTapWired() in hal/topology/wiring.hpp");

            //
            // The adapter must not describe a driven rail as an ordinary
            // pin. Note this is the *only* direction that is forbidden: a
            // SOURCE_POINT keeping a WIRE_CONNECTOR row is fine and normal
            // (see checkSourceCoverage below). What cannot happen is the
            // rig knowing a supply is cabled to a pin while dut/adapter.inc
            // presents it as something the DUT merely offers -- anyone
            // deciding what is safe to do at a pin reads the adapter, not
            // the rig's table.
            //
            static_assert( ! hal::isSourceWired( location),
                          "a dut POINT is named by a WIRE_SOURCE entry in rig/wiring.inc -- a pin a "
                          "source is cabled onto must be declared SOURCE_POINT, so the adapter does "
                          "not describe a driven rail as an ordinary pin");
        }

        return true;
    }

    //
    // The mirror image, for the pins a source is cabled onto.
    //
    template<typename Group>
    consteval auto checkSourceCoverage() -> bool
    {
        template for( constexpr auto locationRef : sourcePointLocationRefs<Group>)
        {
            constexpr auto location = [: locationRef :];

            static_assert( hal::isSourceWired( location),
                          "a dut SOURCE_POINT has no matching WIRE_SOURCE entry in rig/wiring.inc "
                          "-- the DUT says a source is cabled onto this pin and the rig does not "
                          "say which one");

            //
            // Deliberately NOT asserted here: that a source point has no
            // WIRE_CONNECTOR row. An earlier version did, on the assumption
            // that a cabled pin is unreachable -- which banned the very
            // measurement these points are worth declaring for. A rail is
            // cabled so the fabric never carries its load current; a
            // high-impedance tap onto the same pin is a different thing and
            // an ordinary one (see core::PointKind, and
            // suite/scripts/rig_power_on.cpp). Every source point on this
            // rig is in fact tapped.
            //
            // Nor is the converse asserted -- that one must exist. A rail
            // nobody bothered to tap is a legitimate bench, just one whose
            // rail can only be checked at the instrument. Whether a tap
            // exists is a rig fact with no DUT-side counterpart to check it
            // against, which is precisely why it stays a runtime lookup
            // (see core::MeasureEngine).
            //

            //
            // Two supplies cabled onto one pin is the source-side twin of
            // two POINTs sharing a pin (checked below): both tables read as
            // complete, and the rig has its outputs tied together. Counted
            // rather than compared pairwise for the same reason that check
            // gives.
            //
            // <= 1, not == 1: "none at all" is the first assertion's to
            // report, and stating it twice would have this one claim a pin
            // is doubly-cabled when the actual mistake is that it is not
            // cabled at all.
            //
            static_assert( hal::sourcesAt( location) <= 1,
                          "two WIRE_SOURCE entries in rig/wiring.inc land on one pin -- two source "
                          "instruments cabled onto the same VPC pin are shorted together");
        }

        return true;
    }

    constexpr bool dutWiringCovered = checkCoverage<dut>();
    constexpr bool dutSourcesCovered = checkSourceCoverage<dut>();
} // namespace thorium_wiring_coverage_check

TEST( WiringCoverage, EveryDutPointHasAConnectorWiringEntry)
{
    //
    // Nothing to run: reaching this line at all means every static_assert
    // above already held. See the file comment.
    //
    SUCCEED();
}

TEST( WiringCoverage, SourcePointsAreCabledNotRouted)
{
    //
    // Nothing to run, same as above -- present so the guarantee is in the
    // test list rather than being an invisible property of the build.
    //
    static_assert( thorium_wiring_coverage_check::dutSourcesCovered);

    SUCCEED();
}

//
// ---------------------------------------------------------------------------
// One point per pin
// ---------------------------------------------------------------------------
// Two POINTs sharing a VpcLocation is always a mistake, and a uniquely quiet
// one: both names compile, both are wired (they resolve to the same connector
// entry), every existing check above passes, and a script measuring at one of
// them silently reads the other's pin. Nothing about the failure looks like a
// wiring error -- it looks like the DUT is wrong.
//
// It is also exactly what a copy-pasted POINT line produces, which is how these
// tables are actually edited: duplicate the line above, change the name and the
// description, forget the pin.
//
// Same shape as the coverage check -- consteval over the adapter's own
// declarations, one static_assert per point so a failure names the offender
// rather than the table.
//
namespace thorium_pin_uniqueness_check
{
    //
    // The locations as *values*, not reflections: the coverage check above
    // splices each one at its own use site, but counting needs them all in
    // hand at once to compare against. std::meta::extract pulls the constant
    // out of the template argument the reflection refers to.
    //
    template<typename Group>
    consteval auto pointLocationValues() -> std::vector<hal::VpcLocation>
    {
        std::vector<hal::VpcLocation> result;

        for( const auto locationRef : thorium_wiring_coverage_check::adapterPointLocationInfos<Group>())
        {
            result.push_back( std::meta::extract<hal::VpcLocation>( locationRef));
        }

        return result;
    }

    template<typename Group>
    constexpr auto pointLocations = std::define_static_array( pointLocationValues<Group>());

    template<typename Group>
    consteval auto pointsAt( const hal::VpcLocation location) -> std::size_t
    {
        std::size_t count = 0;

        for( const auto declared : pointLocations<Group>)
        {
            if( declared == location)
            {
                ++count;
            }
        }

        return count;
    }

    //
    // Counted rather than compared pairwise, so a pin declared three times
    // reports once per point rather than once per pair -- and so the assertion
    // reads as the rule it enforces: a pin belongs to exactly one point.
    //
    template<typename Group>
    consteval auto checkPinUniqueness() -> bool
    {
        template for( constexpr auto locationRef : thorium_wiring_coverage_check::adapterPointLocationRefs<Group>)
        {
            constexpr auto location = [: locationRef :];

            static_assert( pointsAt<Group>( location) == 1,
                          "two dut POINTs share one VPC pin -- each POINT must name a pin of its "
                          "own, or a measurement at one silently reads the other (see "
                          "dut/adapter.inc)");
        }

        return true;
    }

    constexpr bool dutPinsUnique = checkPinUniqueness<dut>();
} // namespace thorium_pin_uniqueness_check

TEST( WiringCoverage, NoTwoDutPointsShareAPin)
{
    //
    // Nothing to run -- see the file comment. Present so the guarantee is in
    // the test list rather than being an invisible property of the build.
    //
    static_assert( thorium_pin_uniqueness_check::dutPinsUnique);

    SUCCEED();
}

//
// ---------------------------------------------------------------------------
// Sense-lead consistency, both sides of the route
// ---------------------------------------------------------------------------
// A sense entry with no matching force entry is always a wiring mistake: sense
// leads measure across a connection the force leads make, so a rig that wires
// one without the other has described half a route. Cheap to state, and like
// the point coverage above it costs nothing at runtime -- these are consteval
// predicates over the rig's own tables (hal::isInstrumentWired,
// hal::isWired), so a mismatch fails this build rather than a run.
//
// Note what is deliberately NOT asserted: that every 4-wire-capable instrument
// has sense wiring. Whether a rig routes an instrument's sense leads is its
// own choice -- a bench may own a DMM whose 4-wire mode it never uses -- and
// demanding it would repeat exactly the over-constraint that binding a quantity
// to a DUT point turned out to be (see core::AdapterPointTag). What a rig
// cannot sensibly do is wire sense *and not* force, and that is what this
// checks.
//
namespace thorium_sense_consistency_check
{
    constexpr auto instrumentIds = std::define_static_array( std::meta::enumerators_of( ^^hal::InstrumentId));

    consteval auto checkInstrumentSenseConsistency() -> bool
    {
        template for( constexpr auto enumerator : instrumentIds)
        {
            constexpr auto id = [: enumerator :];

            if constexpr( hal::isInstrumentWired( id, hal::WireRole::Sense))
            {
                static_assert( hal::isInstrumentWired( id, hal::WireRole::Force),
                              "an instrument has WIRE_INSTRUMENT_SENSE but no WIRE_INSTRUMENT in "
                              "rig/wiring.inc -- sense leads measure across the connection the "
                              "force leads make, so half a route is always a mistake");
            }
        }

        return true;
    }

    constexpr bool instrumentSenseConsistent = checkInstrumentSenseConsistency();

    //
    // The connector side of the same rule, over the DUT's declared points --
    // the only locations this rig has any reason to wire at all.
    //
    template<typename Group>
    consteval auto checkConnectorSenseConsistency() -> bool
    {
        template for( constexpr auto locationRef : thorium_wiring_coverage_check::adapterPointLocationRefs<Group>)
        {
            constexpr auto location = [: locationRef :];

            if constexpr( hal::isWired( location, hal::WireRole::Sense))
            {
                static_assert( hal::isWired( location, hal::WireRole::Force),
                              "a dut POINT has WIRE_CONNECTOR_SENSE but no WIRE_CONNECTOR in "
                              "rig/wiring.inc -- see above");
            }
        }

        return true;
    }

    constexpr bool connectorSenseConsistent = checkConnectorSenseConsistency<dut>();
} // namespace thorium_sense_consistency_check


TEST( WiringCoverage, SenseWiringAlwaysHasMatchingForceWiring)
{
    //
    // Nothing to run, same as above: the static_asserts have already held by
    // the time this compiles. Present so the guarantee appears in the test
    // list rather than being an invisible property of the build.
    //
    static_assert( thorium_sense_consistency_check::instrumentSenseConsistent);
    static_assert( thorium_sense_consistency_check::connectorSenseConsistent);

    SUCCEED();
}
