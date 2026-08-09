#pragma once

#include <array>

#include "core/test_catalog.hpp"

#include THORIUM_TEST_SCRIPTS

//
// Pulls together the test catalog for this specific DUT, in one shot. This
// is deliberately a separate header from test_catalog.hpp: that file is the
// general, dependency-free GROUP/TEST/END_GROUP mechanism (no notion of any
// specific test suite at all), while this one is a specific consumer of it
// -- resolving THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS requires a suite's
// build configuration, so folding this into test_catalog.hpp would force
// that requirement onto every unrelated user of the general macros. Exactly
// mirrors why core/active_criteria.hpp is separate from core/criterion.hpp.
//
// THORIUM_TEST_SCRIPTS points at a suite's scripts.hpp (see
// suite/scripts/scripts.hpp) -- the declarations every TEST(...) below is
// checked against. It's included here, at file scope, deliberately -- NOT
// inside namespace detail below.
//
// Notice what's absent compared to active_criteria.hpp: no
// "using namespace" of any suite-specific namespace. Scripts are declared
// at global scope (see suite/scripts/scripts.hpp's own reasoning for why),
// so TEST(...) below can already name one directly, via ordinary
// unqualified lookup reaching all the way out to global scope -- the same
// way this file can write std::array without "using namespace std;". That's
// what lets this whole file live in core: unlike active_criteria.hpp (whose
// namespace core::quantities is core's own), there is no suite-specific
// namespace name for this file to know about at all.
//
namespace core::catalog
{
    //
    // Fallbacks for a catalog that declares no SETUP/TEARDOWN of its own --
    // which is the normal case, and must stay the case that needs no
    // placeholder written anywhere.
    //
    // These have to be declared *before* namespace detail below, because that
    // is what makes them findable from inside it. A catalog's own SETUP expands
    // to a SetupHook in detail, which hides the one here; a catalog with no
    // SETUP declares nothing, and unqualified lookup inside detail walks out to
    // this one instead. That is the whole mechanism: ordinary name lookup, not
    // a detection trick, and the reason the two resolutions below are written
    // unqualified rather than as detail::SetupHook.
    //
    inline constexpr RunHook SetupHook    = nullptr;
    inline constexpr RunHook TeardownHook = nullptr;

    namespace detail
    {
        #include THORIUM_TEST_CATALOG

        //
        // Unqualified on purpose -- see the fallbacks' comment above. Resolved
        // here, inside detail, because that is the only scope where a catalog's
        // own declaration is visible to hide the fallback.
        //
        inline constexpr RunHook ResolvedSetup    = SetupHook;
        inline constexpr RunHook ResolvedTeardown = TeardownHook;
    } // namespace detail

    //
    // The hooks that bracket a run, or nullptr where the catalog declared
    // none -- the runner checks before calling (see app/src/main.cpp).
    //
    inline constexpr RunHook Setup    = detail::ResolvedSetup;
    inline constexpr RunHook Teardown = detail::ResolvedTeardown;

    //
    // The whole catalog, flattened to one compile-time array of groups. A UI
    // walks this top-to-bottom to list and select tests; nothing here
    // allocates or parses a string to find a test -- ids/descriptions are
    // for display and reporting, never for internal dispatch. Adding a
    // wholly new GROUP means adding one line here (same role as
    // THORIUM_KNOWN_CRITERIA_VARIANTS in the top-level CMakeLists.txt for a
    // wholly new criteria variant); adding a TEST to an existing GROUP never
    // touches this file.
    //
    inline constexpr std::array Catalog
    {
        core::TestGroup{ detail::OutputVoltage::Name, detail::OutputVoltage::Description, detail::OutputVoltage::Tests },
    };
} // namespace core::catalog
