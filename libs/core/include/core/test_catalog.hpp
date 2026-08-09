#pragma once

#include <array>
#include <span>
#include <string_view>

namespace core
{
    //
    // The one signature every catalog-registered test script has: no
    // parameters at all. No device/rig/crosspoint handle is passed in --
    // that routing is resolved statically inside the script, the same way
    // GROUP/CRIT/MATRIX/POINT already are -- and no group/test name either.
    // The runner brackets every script with the journal's own group/test
    // boundaries (see app/src/main.cpp), and each criterion already carries
    // its group name into the log on its own (CRIT stamps it, core::Verify
    // posts it), so naming a script's own test to it would be a second
    // source for a fact the log already has from the first -- with the usual
    // consequence that the two can disagree.
    //
    // Because every script shares this one signature, TestCase/TestGroup
    // need no Script template parameter -- unlike Criterion's predicate,
    // there's only one shape here to support.
    //
    // Taking no parameters does not stop one script body from serving
    // several catalog entries. Make the script a template and let TEST name
    // an instantiation -- that is an ordinary identifier like any other, so
    // the macro needs no change:
    //
    //   template<typename Criteria>              // suite/scripts.hpp
    //   auto supplyRailScript() -> bool;
    //
    //   GROUP( OutputVoltage, "...")             // suite/test_catalog.inc
    //       TEST( SupplyRailA, supplyRailScript<FS_Supply_1>, "...")
    //       TEST( SupplyRailB, supplyRailScript<FS_Supply_2>, "...")
    //   END_GROUP
    //
    // One preprocessor wrinkle, since TEST is a macro: a template-id with
    // more than one parameter contains a comma, so supplyRailScript<A, B>
    // arrives as four macro arguments rather than three. Name it first (a
    // using-alias) and pass the alias. One parameter, as above, is fine.
    //
    // What is stored is still an auto (*)() -> bool, so nothing here has to
    // change: the signature stays uniform, Tests stays a homogeneous array,
    // and a misspelled group is still a compile error rather than a runtime
    // lookup miss. Note the limit -- the body still writes
    // Criteria::FS_Supply_5V0, so this reaches only groups that differ in
    // their criteria's *values*, not in which criteria they hold. That is
    // the case worth having: it is the same code doing the checking either
    // way. A group that differs in shape needs a different script, not a
    // template parameter.
    //
    // Deliberately not the answer to build-wide tolerance variants: swapping
    // production/stress/aged is already THORIUM_CRITERIA_VARIANT's job (see
    // the top-level CMakeLists.txt), and no script or catalog entry changes
    // for it. This is for the case where one run has to check two groups
    // side by side.
    //
    using TestScript = auto (*)() -> bool;

    //
    // A run hook: code that brackets the selected scripts, declared by the
    // catalog via SETUP/TEARDOWN below. Powering the rig on before the first
    // script and off after the last is the case it exists for.
    //
    // Same shape as a TestScript, and deliberately a distinct name rather than
    // a reuse of it: a hook is not a test, contributes no verdict of its own to
    // the report, and appears in no group -- what its bool means is "did the
    // bracketing work", not "did the DUT pass".
    //
    // What it brackets is the *selection*, once, however many times that
    // selection is then repeated (see --repeat in app/src/main.cpp). A hook
    // that should instead run per repetition would be a different hook, not a
    // different meaning for this one -- there is nothing here to add it yet
    // because nothing has needed it.
    //
    // Both hooks are optional. A catalog declaring neither is the normal case
    // and needs no placeholder; see core/active_test_catalog.hpp for how their
    // absence resolves.
    //
    using RunHook = auto (*)() -> bool;

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

//
// SETUP / TEARDOWN: the code that brackets the selected scripts, named the
// same way TEST names a script -- as an identifier checked against its
// declaration, not a string looked up at runtime.
//
//   SETUP(    rigPowerOn)
//   TEARDOWN( rigPowerOff)
//
// Both are optional and independent: declare one, both, or neither. A catalog
// with no SETUP line needs no placeholder for one, which is what the fallback
// declarations in core/active_test_catalog.hpp are for.
//
// At most one of each per catalog -- a second SETUP is a redefinition error at
// the point it is written, which is the right outcome: two setup routines have
// an order between them that nothing in the file states.
//
#define SETUP( hook)                                                   \
    inline constexpr ::core::RunHook SetupHook = hook;

#define TEARDOWN( hook)                                                \
    inline constexpr ::core::RunHook TeardownHook = hook;
