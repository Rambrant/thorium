#include "hal/adapter.hpp"
#include "hal/wiring.hpp"

#include <gtest/gtest.h>

#include <array>
#include <meta>
#include <vector>

//
// A plain #include each: ADAPTER/POINT and CONNECTOR_WIRING/WIRE_CONNECTOR
// (see hal/adapter.hpp, hal/wiring.hpp) both expand fully qualified, so
// nothing else needs to be brought into scope first. This is deliberately
// the one file that includes both -- see hal/wiring.hpp's own comment on
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
    // static AdapterPointTag<Loc> members and pulls each one's Loc
    // straight out of the template argument, without needing to splice the
    // point object itself. Homogeneous (every point in one ADAPTER shares
    // Loc's type -- hal::VpcLocation here), unlike core::meta::all<Group>()
    // in core/criterion.hpp, which needs a tuple because CRIT members hold
    // different Predicate types; a plain array is all this needs.
    //
    template<typename Group>
    consteval auto adapterPointLocationInfos() -> std::vector<std::meta::info>
    {
        std::vector<std::meta::info> result;

        for( auto member : std::meta::members_of( ^^Group, std::meta::access_context::current()))
        {
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

            // template_arguments_of(type)[0] is Loc -- AdapterPointTag's
            // first template parameter (see core/adapter.hpp).
            result.push_back( std::meta::template_arguments_of( type)[ 0]);
        }

        return result;
    }

    template<typename Group>
    constexpr auto adapterPointLocationRefs = std::define_static_array( adapterPointLocationInfos<Group>());

    //
    // One static_assert per point (via template for, not a single combined
    // check) -- a failure names the exact template instantiation (and so,
    // the exact VpcLocation) that has no CONNECTOR_WIRING entry, rather
    // than just "something in this adapter is unwired".
    //
    template<typename Group>
    consteval auto checkCoverage() -> bool
    {
        template for( constexpr auto locationRef : adapterPointLocationRefs<Group>)
        {
            constexpr auto location = [: locationRef :];

            static_assert( hal::isWired( location),
                          "a dut POINT has no matching WIRE_CONNECTOR entry in rig/wiring.inc "
                          "-- see hal::isWired()'s own comment in hal/wiring.hpp");
        }

        return true;
    }

    constexpr bool dutWiringCovered = checkCoverage<dut>();
} // namespace thorium_wiring_coverage_check

TEST( WiringCoverage, EveryDutPointHasAConnectorWiringEntry)
{
    //
    // Nothing to run: reaching this line at all means every static_assert
    // above already held. See the file comment.
    //
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
