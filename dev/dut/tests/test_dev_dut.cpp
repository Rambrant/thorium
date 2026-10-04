#include "hal/topology/adapter.hpp"
#include "hal/topology/wiring.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <meta>
#include <type_traits>

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
    // One point, dut::DeskTerminal -- a terminal on slot 1's 34932T, which the
    // 34980A's cards and dev/rig/wiring.inc make reachable (asserted there, in
    // dev/rig/tests/test_dev_rig.cpp). A second point is a deliberate edit of
    // this line, after a wiring row for it: the order this line used to name,
    // when the count was zero and the desk had no fabric to reach one through.
    //
    static_assert( adapterPointCount() == 1,
                   "dev/dut/adapter.inc's point count changed -- add a dev/rig/wiring.inc row that "
                   "reaches a new point first, then change this line");

    //
    // The one point is a source point because DcP7 is cabled onto it, and the
    // two files have to say so together -- the pairing the bench's coverage
    // test holds every point to, written out for the one point here: a
    // SOURCE_POINT with no WIRE_SOURCE row behind it claims a cable nobody
    // recorded, and a WIRE_SOURCE row under a plain POINT describes a driven
    // terminal as an ordinary one. And still routed, which a source point
    // stays: the meter reads it through the matrix.
    //
    using DeskTerminalTag = std::remove_cv_t<decltype( dut::DeskTerminal)>;

    static_assert( DeskTerminalTag::KindValue == core::PointKind::Source,
                   "dut::DeskTerminal must be a SOURCE_POINT: dev/rig/wiring.inc cables DcP7 onto it");
    static_assert( hal::isSourceWired( DeskTerminalTag::LocationValue),
                   "dut::DeskTerminal is a SOURCE_POINT with no WIRE_SOURCE row in dev/rig/wiring.inc");
    static_assert( hal::sourcesAt( DeskTerminalTag::LocationValue) == 1);
    static_assert( hal::isWired( DeskTerminalTag::LocationValue, hal::WireRole::Force));

    //
    // The adapter is this deployment's, and not the bench's reached by a wrong
    // THORIUM_DUT_DIR. Cheap, and it is the one check that would catch a build
    // configured half from one deployment and half from another -- which is a
    // real risk for a repository that now holds two.
    //
    static_assert( dut::Description.starts_with( "Dev bench"));
} // namespace
