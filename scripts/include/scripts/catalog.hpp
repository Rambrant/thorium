#pragma once

#include <array>

#include "core/test_catalog.hpp"
#include "scripts/scripts.hpp"

//
// The declarative test-script catalog: groups of tests, each pointing at a
// real scripts:: function, meant to be walked and selected from by a UI
// (console picker today, something richer later) rather than run
// unconditionally like scripts/src/main.cpp does today.
//
// The file a test writer actually edits, suite/catalog.inc, has none of
// the scaffolding below -- no #pragma once, no #includes, no namespace, no
// scripts:: qualification -- just GROUP / TEST / END_GROUP, naming each
// script directly. Every script it names must already be declared in
// scripts/scripts.hpp (the usual way, by hand) -- that's the thing a
// misspelled or renamed script fails to compile against.
//
// It lives in suite/, not here, for the same reason suite/*.inc
// files do: it's test-suite data specific to this DUT (which tests exist,
// what to call them), not framework code -- see suite/README.md.
// THORIUM_TEST_CATALOG is an absolute path (like THORIUM_ACTIVE_CRITERIA),
// set by scripts/CMakeLists.txt, so no extra include directory is needed
// to find it.
//
namespace scripts::catalog
{
    namespace detail
    {
        // Brings every scripts:: function into unqualified scope, so
        // TEST( SupplyRail, supplyRailScript, ...) can name it directly --
        // same trick as active_criteria.hpp's "using namespace
        // core::quantities;" for a criteria .inc file.
        using namespace ::scripts;

        #include THORIUM_TEST_CATALOG
    } // namespace detail

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
} // namespace scripts::catalog
