#pragma once

#include <array>
#include <string_view>

#include "core/test_catalog.hpp"
#include "scripts/scripts.hpp"

//
// The declarative test-script catalog: groups of tests, each pointing at a
// real scripts:: function, meant to be walked and selected from by a UI
// (console picker today, something richer later) rather than run
// unconditionally like scripts/src/main.cpp does today.
//
// Scripts take (group, test) -- both compile-time-known strings out of this
// very catalog -- and nothing else. No device/rig/crosspoint handle is
// passed in: that routing is resolved statically inside the script, the
// same way GROUP/CRIT/MATRIX/POINT already are, rather than threaded through
// as a runtime parameter. That's also what makes every catalog-registered
// script the same type, and the whole Catalog a single flat, static,
// compile-time array -- no per-script device-model split to work around.
//
namespace scripts::catalog
{
    using ScriptFn = auto (*)( std::string_view group, std::string_view test) -> bool;

    using TestCase  = core::TestCase<ScriptFn>;
    using TestGroup = core::TestGroup<ScriptFn>;

    // A GROUP needs at least one TEST in it: Tests is built via std::array's
    // CTAD deduction guide, which (like CRITERIA/CRIT) needs one element to
    // deduce the element type from.
    GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
        TEST( FuseRegister, scripts::fuseRegisterScript, "Verify fuse register and output voltage")
        TEST( SupplyRail,   scripts::supplyRailScript,   "Verify supply rail voltages via matrix")
    END_GROUP

    //
    // The whole catalog, flattened to one compile-time array of groups. A UI
    // walks this top-to-bottom to list and select tests; nothing here
    // allocates or parses a string to find a test -- ids/descriptions are
    // for display and reporting, never for internal dispatch. Dispatch (once
    // a selection is made) is a plain call through TestCase::script, exactly
    // the type-checked function pointer declared at GROUP/TEST time.
    //
    inline constexpr std::array Catalog
    {
        TestGroup{ OutputVoltage::Name, OutputVoltage::Description, OutputVoltage::Tests },
    };
} // namespace scripts::catalog
