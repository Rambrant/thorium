#pragma once

#include <array>
#include <cstddef>
#include <meta>
#include <vector>

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

    namespace detail
    {
        //
        // Every GROUP the catalog declared, found by reflecting over this
        // namespace rather than by being listed by hand.
        //
        // That list used to be written out here, one core::TestGroup line per
        // group, and it was the last place in this codebase where adding a
        // thing to a declarative table meant also editing framework code to
        // acknowledge it. The cost was not just the extra line. Because this
        // header is compiled against *whichever* catalog a target supplies, a
        // hardcoded list forced every catalog to declare every group name the
        // list mentioned -- so the two test fixtures
        // (app/tests/fixtures/hooked_catalog.inc,
        // libs/core/tests/fixtures/hooked_catalog.inc) each had to carry a
        // group named after one in the shipped suite, and adding a group to
        // the shipped suite meant adding a matching one to both fixtures,
        // changing what the acceptance tests observe. A suite gaining a test
        // group could not be a suite-only change.
        //
        // Reflection removes the list and the requirement together: a catalog
        // now declares what it declares, and this header reads it. Same move,
        // and the same reasoning, as core::meta::all() replacing the
        // re-#include walks in core/criterion.hpp and hal::safeRig()'s
        // replacing the third read of instrument.inc.
        //
        // A group is recognised by shape -- a type in this namespace with the
        // three static members GROUP produces -- rather than by inheriting a
        // marker the way core::AdapterBundleTag is matched. A marker would be
        // better, and is what BUNDLE does; it is not available here because
        // GROUP is the general, dependency-free mechanism in
        // core/test_catalog.hpp and the tables are already written. Matching on
        // all three members together is narrow enough that nothing else a
        // catalog could plausibly declare collides with it.
        //
        consteval auto groupTypeInfos() -> std::vector<std::meta::info>
        {
            std::vector<std::meta::info> result;

            for( const auto member : std::meta::members_of( ^^detail, std::meta::access_context::current()))
            {
                if( !std::meta::is_type( member))
                {
                    continue;
                }

                auto hasName = false, hasDescription = false, hasTests = false;

                for( const auto field : std::meta::members_of( member, std::meta::access_context::current()))
                {
                    if( !std::meta::is_variable( field))
                    {
                        continue;
                    }

                    const auto identifier = std::meta::identifier_of( field);

                    hasName        = hasName        || identifier == "Name";
                    hasDescription = hasDescription || identifier == "Description";
                    hasTests       = hasTests       || identifier == "Tests";
                }

                if( hasName && hasDescription && hasTests)
                {
                    result.push_back( member);
                }
            }

            return result;
        }

        constexpr auto groupRefs = std::define_static_array( groupTypeInfos());
    } // namespace detail

    //
    // The whole catalog, flattened to one compile-time array of groups, in the
    // order the catalog file declares them -- members_of preserves declaration
    // order, which is what a UI listing "group -> tests" top to bottom depends
    // on. A UI walks this; nothing here allocates or parses a string to find a
    // test -- ids and descriptions are for display and reporting, never for
    // internal dispatch.
    //
    // Adding a GROUP or a TEST now touches the catalog file and nothing else.
    //
    inline constexpr auto Catalog = []
    {
        std::array<core::TestGroup, detail::groupRefs.size()> groups{};

        std::size_t next = 0;

        template for( constexpr auto groupRef : detail::groupRefs)
        {
            using GroupT = typename [: groupRef :];

            groups[ next++] = core::TestGroup{ GroupT::Name, GroupT::Description, GroupT::Tests };
        }

        return groups;
    }();
} // namespace core::catalog
