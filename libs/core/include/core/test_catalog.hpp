#pragma once

#include <array>
#include <span>
#include <string_view>

namespace core
{
    //
    // A single named, traceable test case: an id + description (matching a
    // test-plan entry) plus the script it runs. Templated on the script's
    // exact function-pointer type so the compiler sees straight through it --
    // the same trick Criterion plays with its predicate. In practice every
    // TestCase in a given catalog shares one Script alias (see
    // scripts/catalog.hpp), which is what lets the whole catalog collapse
    // into a single, homogeneous, compile-time array further down -- not
    // just style, a real precondition for that to type-check at all.
    //
    template<typename Script>
    struct TestCase
    {
        std::string_view id;
        std::string_view description;

        Script script;
    };

    template<typename Script>
    TestCase( std::string_view, std::string_view, Script) -> TestCase<Script>;

    //
    // Runtime-traversable view of one GROUP: its own name/description plus a
    // span over its (compile-time-built) Tests array. A UI walks a list of
    // these to present "group -> tests" and to collect a selection, without
    // knowing anything about how the tests are actually invoked.
    //
    template<typename Script>
    struct TestGroup
    {
        std::string_view name;
        std::string_view description;

        std::span<const TestCase<Script>> tests;
    };
} // namespace core

//
// GROUP / TEST / END_GROUP: declarative, Excel-readable test-script tables,
// the same shape as CRITERIA / CRIT / END_CRITERIA (see core/criterion.hpp,
// which reserves this name for exactly this mechanism). Each TEST names the
// actual script function directly -- not a string -- so a renamed or
// misspelled script is a compile error, not a runtime lookup miss.
//
// Unlike a CRITERIA group (named static members, one per CRIT, looked up by
// C++ name), a GROUP's tests collapse into a single
// `static constexpr std::array Tests`. That requires every TEST in the group
// to share one Script type -- see scripts/catalog.hpp for the concrete
// Script alias and why unifying script signatures is what makes this array
// (and the top-level catalog built from several groups) possible at all.
//
//   GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
//       TEST( SupplyRail, scripts::supplyRailScript, "Verify supply rail voltages via matrix")
//   END_GROUP
//
// `id` doubles as both the C++-checked macro token and the display string
// (via #id), same as CRIT's id -- there's no separate "name" field to keep
// in sync.
//
#define GROUP( groupName, desc)                                       \
    struct groupName                                                   \
    {                                                                   \
        static constexpr std::string_view Name        = #groupName;     \
        static constexpr std::string_view Description = desc;           \
        static constexpr std::array Tests                                \
        {

#define TEST( id, script, desc)                                        \
            ::core::TestCase{ std::string_view{ #id }, std::string_view{ desc }, script },

#define END_GROUP                                                      \
        };                                                               \
    };
