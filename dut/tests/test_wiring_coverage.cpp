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
#include "dut/device_x_profile.inc"
#include "rig/wiring.inc"

//
// Like test_criteria_variants_compile.cpp in suite/tests/: this file has no
// runtime assertions of its own -- the entire point is that it compiles.
// A DeviceX_StdAdapter POINT with no matching rig/wiring.inc entry (a typo
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
                          "a DeviceX_StdAdapter POINT has no matching WIRE_CONNECTOR entry in rig/wiring.inc "
                          "-- see hal::isWired()'s own comment in hal/wiring.hpp");
        }

        return true;
    }

    constexpr bool deviceXWiringCovered = checkCoverage<DeviceX_StdAdapter>();
} // namespace thorium_wiring_coverage_check

TEST( WiringCoverage, EveryDeviceXPointHasAConnectorWiringEntry)
{
    //
    // Nothing to run: reaching this line at all means every static_assert
    // above already held. See the file comment.
    //
    SUCCEED();
}
