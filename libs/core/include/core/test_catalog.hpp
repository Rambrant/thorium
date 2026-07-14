#pragma once

#include <array>
#include <span>
#include <string_view>

namespace core
{
    //
    // The one signature every catalog-registered test script has:
    // (group, test) -- both compile-time-known strings straight out of the
    // catalog -- and nothing else. No device/rig/crosspoint handle is
    // passed in: that routing is resolved statically inside the script, the
    // same way GROUP/CRIT/MATRIX/POINT already are. Because every script
    // shares this one signature, TestCase/TestGroup need no Script template
    // parameter -- unlike Criterion's predicate, there's only one shape
    // here to support.
    //
    using TestScript = auto (*)( std::string_view group, std::string_view test) -> bool;

    //
    // A single named, traceable test case: an id + description (matching a
    // test-plan entry) plus the script it runs.
    //
    struct TestCase
    {
        std::string_view id;
        std::string_view description;

        TestScript script;
    };

    //
    // Runtime-traversable view of one GROUP: its own name/description plus a
    // span over its (compile-time-built) Tests array. A UI walks a list of
    // these to present "group -> tests" and to collect a selection, without
    // knowing anything about how the tests are actually invoked.
    //
    struct TestGroup
    {
        std::string_view name;
        std::string_view description;

        std::span<const TestCase> tests;
    };
} // namespace core

//
// GROUP / TEST / END_GROUP: declarative, Excel-readable test-script tables,
// the same shape as CRITERIA / CRIT / END_CRITERIA (see core/criterion.hpp,
// which reserves this name for exactly this mechanism). Each TEST names the
// actual script function directly -- not a string -- so a renamed or
// misspelled script is a compile error against its scripts.hpp declaration,
// not a runtime lookup miss.
//
// A GROUP's tests collapse into a single `static constexpr std::array
// Tests`, which needs at least one TEST in it (like CRITERIA/CRIT) for
// std::array's CTAD deduction guide to have an element to deduce from.
//
//   GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
//       TEST( SupplyRail, supplyRailScript, "Verify supply rail voltages via matrix")
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
