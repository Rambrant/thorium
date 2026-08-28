#include "hal/topology/adapter.hpp"
#include "hal/topology/wiring.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <meta>

#include "core/topology/adapter.hpp"

//
// The dev deployment's answer to dut/tests/test_wiring_coverage.cpp.
//
// That file reflects over every POINT in the bench adapter and requires a
// matching connector-wiring row, so an unwired point fails the build. The same
// mechanism applied here checks nothing -- there are no points. Which leaves a
// gap worth closing explicitly rather than by absence: "no points because this
// bench has no fixture" and "no points because someone deleted them" compile
// identically, and only the first is true.
//
// So this asserts the emptiness instead, the same way that file works: no
// runtime assertions at all, the whole point is that it compiles.
//
// Both tables reached by a plain repo-root-relative #include, exactly as the
// bench's coverage test reaches its two.
//
#include "dev/rig/wiring.inc"
#include "dev/dut/adapter.inc"

namespace
{
    //
    // Every adapter point the `dut` struct declares, counted -- the same walk
    // dut/tests/test_wiring_coverage.cpp does before checking each one is
    // wired, reduced to the one question this deployment has: how many.
    //
    // Matched on core::AdapterPointTag being the template, not on the member's
    // name or on POINT having been used, because that is what an adapter point
    // actually is (see core/topology/adapter.hpp). SOURCE_POINT declares the same
    // template with a different second argument, so both are counted, which is
    // what this needs: neither kind is reachable here.
    //
    consteval auto adapterPointCount() -> std::size_t
    {
        std::size_t count = 0;

        for( const auto member : std::meta::members_of( ^^dut, std::meta::access_context::current()))
        {
            if( ! std::meta::is_variable( member))
            {
                continue;
            }

            const auto type = std::meta::remove_cv( std::meta::type_of( member));

            if( ! std::meta::has_template_arguments( type))
            {
                continue;
            }

            if( std::meta::template_of( type) == ^^core::AdapterPointTag)
            {
                ++count;
            }
        }

        return count;
    }

    //
    // A point declared here without the card and the wiring row that would make
    // it reachable is the mistake this guards, and the failure lands the right
    // way round: such a point compiles against the bench deployment's tables
    // and fails against this one, because this is the build with no fabric to
    // reach it through.
    //
    static_assert( adapterPointCount() == 0,
                   "dev/dut/adapter.inc declares an adapter point, but dev/rig/ has no fabric to reach "
                   "one through -- add a card to dev/rig/devices.inc and a row to dev/rig/wiring.inc "
                   "first, in that order, and then this line is the one to change");

    //
    // The adapter is this deployment's, and not the bench's reached by a wrong
    // THORIUM_DUT_DIR. Cheap, and it is the one check that would catch a build
    // configured half from one deployment and half from another -- which is a
    // real risk for a repository that now holds two.
    //
    static_assert( dut::Description.starts_with( "Dev bench"));
} // namespace
