#pragma once

#include <span>
#include <string_view>

namespace core
{
    //
    // The one signature every catalog-registered test script has: no
    // parameters at all, and no return either. No device/rig/crosspoint handle
    // is passed in -- that routing is resolved statically inside the script,
    // the same way GROUP/CRIT/MATRIX/POINT already are -- and no group/test
    // name either. The runner brackets every script with the journal's own
    // group/test boundaries (see framework/runner/src/main.cpp), and each criterion already
    // carries its group name into the log on its own (CRIT stamps it,
    // core::Verify posts it), so naming a script's own test to it would be a
    // second source for a fact the log already has from the first -- with the
    // usual consequence that the two can disagree.
    //
    // The verdict was the last thing left in that shape. A script used to
    // return bool, folded by hand from each of its own checks:
    //
    //     bool allPassed = true;
    //     allPassed &= Verify( FS_Supply_1::FS_Supply_5V0, rail);
    //     ...
    //     return allPassed;
    //
    // ... which is the same second source, arrived at from the other end: every
    // one of those checks was already in the journal with its own outcome, and
    // core::SarifSink was already deriving its per-test results from the events
    // rather than from the returned bool. The two could disagree -- one missing
    // `allPassed &=` produced a human log reading PASS around a SARIF result
    // reading fail, for the same test in the same run -- and the fold was
    // bookkeeping in a file that is meant to read like a test specification.
    //
    // So the verdict is derived where the checks already are (see
    // Journal::endTest in core/journal/journal.hpp), and a script's job is to make
    // checks, not to tally them. What a script cannot do any more is pass
    // while having checked nothing: that is a failure now, and a stated one.
    //
    // A script that needs to record a failure it did not measure -- a capture
    // that never triggered, a reply too short to hold the byte in question --
    // says so with core::Fail (see core/criteria/verify.hpp) rather than by returning
    // false.
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
    //   auto supplyRailScript() -> void;
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
    // What is stored is still an auto (*)() -> void, so nothing here has to
    // change: the signature stays uniform, a group's assembled test array
    // stays homogeneous, and a misspelled group is still a compile error
    // rather than a runtime
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
    using TestScript = auto (*)() -> void;

    //
    // A run hook: code that brackets the selected scripts, declared by the
    // catalog via RUN_SETUP/RUN_TEARDOWN below, or by one GROUP via the
    // SETUP/TEARDOWN pair beside its tests. Powering the rig on before the
    // first script and off after the last is the case it exists for.
    //
    // Not the same shape as a TestScript, and the difference is the point of
    // its being a distinct type rather than a reuse. A hook is not a test: it
    // contributes no verdict of its own to the report, and -- since it runs
    // outside any beginTest/endTest bracket -- has no per-test event stream for
    // a verdict to be derived from. So it keeps a bool, and what that bool
    // means is "did the bracketing work", not "did the DUT pass".
    //
    // It is still bracketed, just not as a test: the runner opens a journal
    // phase around it (see core::Journal::beginPhase), which is what puts the
    // id "setup"/"teardown" and the readings a hook took into the machine log
    // as something attributable rather than as events belonging to nothing.
    //
    // rigPowerOn is the one place a hand-folded verdict is still the right
    // shape (see suite/scripts/rig_power_on.cpp): it checks each source as it
    // brings it up, and the run is not allowed to start if any of them did not
    // come up.
    //
    // What RUN_SETUP/RUN_TEARDOWN bracket is the *selection*, once, however
    // many times that selection is then repeated (see --repeat in
    // framework/runner/src/main.cpp). A hook that should instead run per repetition would be
    // a different hook, not a different meaning for this one -- there is
    // nothing here to add it yet because nothing has needed it.
    //
    // The same type serves the group-level pair, SETUP/TEARDOWN
    // below, because it is the same kind of thing: bracketing code with a
    // verdict about the bracketing rather than about the DUT. What differs is
    // *when* it runs, and that is the runner's business, not the type's -- a
    // group's hooks run only if something in that group was selected, and once
    // per pass over the selection rather than once per run (see runOnePass in
    // framework/runner/src/main.cpp).
    //
    // All four hooks are optional. A catalog declaring none is the normal case
    // and needs no placeholder; see core/catalog/active_test_catalog.hpp for how their
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
    // What GROUP makes every group inherit, so a catalog's groups can be found
    // by asking what they *are* rather than by matching the members they
    // happen to have -- see core/catalog/active_test_catalog.hpp, which does the
    // finding, and core::AdapterBundleTag, which BUNDLE uses the same way.
    //
    // This replaced a shape match on the three members GROUP used to produce
    // (Name, Description, Tests). Shape matching was already the weaker test
    // -- an unrelated type with those three members would have been read as a
    // group -- and it stopped being viable at all once SETUP/
    // TEARDOWN made a group's member list vary from group to group.
    //
    struct TestGroupTag {};

    //
    // A hook and the prose that says what it does -- what SETUP/TEARDOWN and
    // RUN_SETUP/RUN_TEARDOWN each declare, and what the runner brackets a hook
    // with.
    //
    // One struct rather than two members side by side wherever a hook is
    // carried, because the two facts have to travel together: a hook's
    // description is the only thing in a log that says which bracket a reader
    // is looking at. Every hook spells its id "setup" or "teardown" -- both
    // levels of them, in every group -- so a run with three bracketed groups
    // has four things called "setup" in it, and the description is what tells
    // them apart. Carried separately, the pair would be two things to keep in
    // step at every hand-off, with the failure mode being a log that describes
    // one group's setup and runs another's.
    //
    // An absent hook is a default-constructed one: null, described by nothing.
    // The runner checks the pointer before calling, exactly as it did when this
    // was a bare RunHook (see framework/runner/src/main.cpp).
    //
    struct Bracket
    {
        RunHook           hook{ nullptr };
        std::string_view  description{};
    };

    //
    // Runtime-traversable view of one GROUP: its own name/description, a span
    // over its (compile-time-built) test cases, and the hooks that bracket
    // them. A UI walks a list of these to present "group -> tests" and to
    // collect a selection, without knowing anything about how the tests are
    // actually invoked.
    //
    // setup/teardown hold a null hook for a group that declared neither, which
    // is the normal case -- the runner checks before calling, exactly as it
    // does for the run-level pair.
    //
    struct TestGroup
    {
        std::string_view name;
        std::string_view description;

        std::span<const TestCase> tests;

        Bracket setup;
        Bracket teardown;
    };
} // namespace core

//
// GROUP / TEST / END_GROUP: declarative, Excel-readable test-script tables,
// the same shape as CRITERIA / CRIT / END_CRITERIA (see core/criteria/criterion.hpp,
// which reserves this name for exactly this mechanism). Each TEST names the
// actual script function directly -- not a string -- so a renamed or
// misspelled script is a compile error against its scripts.hpp declaration,
// not a runtime lookup miss.
//
//   GROUP( OutputVoltage, "Tests validating DUT output voltage rails")
//       TEST( SupplyRail, supplyRailScript, "Verify supply rail voltages via matrix")
//   END_GROUP
//
// `id` doubles as both the C++-checked macro token and the display string
// (via #id), same as CRIT's id -- there's no separate "name" field to keep
// in sync.
//
// Each TEST is a static member of the group in its own right, and the array
// the runner walks is assembled from them by reflection (see
// core/catalog/active_test_catalog.hpp). It used to be the other way round: GROUP
// opened a `static constexpr std::array Tests {` and each TEST wrote one
// element into it. That worked, and cost nothing, right up until a group
// needed to say something *other* than a test -- everything between GROUP and
// END_GROUP was inside an array initializer, so there was nowhere for a SETUP
// line to go. Reflection puts the group body back to being an ordinary
// sequence of declarations, which is what lets the hooks below sit in the table
// beside the tests they bracket.
//
// The member is named id##Case rather than id, so that a TEST whose id matches
// its own script's name -- TEST( fixtureScript, fixtureScript, "...") -- is
// still a well-formed declaration rather than a member initialized from
// itself. The suffix is invisible: nothing names the member, and the display
// string still comes from #id.
//
#define GROUP( groupName, desc)                                        \
    struct groupName : ::core::TestGroupTag                            \
    {                                                                  \
        static constexpr std::string_view Name        = #groupName;    \
        static constexpr std::string_view Description = desc;          \
        static constexpr bool SetupAndTeardownBelongInsideAGroup = true;

#define TEST( id, script, desc)                                        \
        static constexpr ::core::TestCase id##Case                     \
            { std::string_view{ #id }, std::string_view{ desc }, script };

#define END_GROUP                                                      \
    };

//
// SETUP / TEARDOWN: the code that brackets one group's tests, written as rows
// of that group's own table -- anywhere between GROUP and END_GROUP:
//
//   GROUP( Transient, "Tests validating DUT behaviour while a supply is disturbed")
//       SETUP(    armTransient,    "Arm the scope on the 5V rail")
//       TEST( AcDropout, acDropoutScript, "...")
//       TEARDOWN( disarmTransient, "Disarm the scope and restore the AC input")
//   END_GROUP
//
// Named as identifiers checked against a declaration, not strings looked up at
// runtime, exactly the way TEST names a script -- and described in prose beside
// that, exactly the way TEST and GROUP are.
//
// The description is required rather than optional, and it earns the keystrokes
// twice over. Every hook in a catalog is called "setup" or "teardown" in the
// logs, at both levels and in every group, so a run bracketing three groups
// writes four headings reading "setup" -- and the description is the entire
// difference between them. It is also the only place a reader is told what a
// bracket was *for*: a group's tests say what they check, and the state
// somebody had to establish before them is otherwise recoverable only by
// opening the hook's source.
//
// They run only if something in the group was actually selected -- a --select
// naming one test in one group leaves every other group's hooks untouched,
// which is the property they exist for: establishing a rig state that only
// these tests need is wasted, and sometimes wrong, for a run that is not going
// to use it.
//
// Per pass over the selection, not per run: --repeat=3 arms and disarms the
// group three times. RUN_SETUP/RUN_TEARDOWN below are the pair that brackets
// the whole selection once (see core::RunHook above).
//
// Both optional and independent, per group -- a group declaring neither is the
// normal case and needs no placeholder. At most one of each per group, a second
// being a redefinition error where it is written.
//
// The static_assert is a scope check, and the reason it is worth a line: these
// two are the *inner* pair, so a SETUP written at file scope -- where the outer
// pair goes, and where it went before this pair existed -- is a mistake with no
// symptom. It would expand to a perfectly legal namespace-scope constant that
// nothing reads, leaving a catalog whose rig is never powered up and whose
// build said nothing about it. The condition is a member GROUP declares, so at
// file scope it is not the assert that fires but the name that fails to
// resolve, which is why that name reads as the advice it is.
//
#define SETUP( hook, desc)                                             \
        static_assert( SetupAndTeardownBelongInsideAGroup,              \
            "SETUP( ...) belongs inside a GROUP -- the pair that "      \
            "brackets the whole run is RUN_SETUP/RUN_TEARDOWN");        \
        static constexpr ::core::Bracket GroupSetup { hook, desc };

#define TEARDOWN( hook, desc)                                          \
        static_assert( SetupAndTeardownBelongInsideAGroup,              \
            "TEARDOWN( ...) belongs inside a GROUP -- the pair that "   \
            "brackets the whole run is RUN_SETUP/RUN_TEARDOWN");        \
        static constexpr ::core::Bracket GroupTeardown { hook, desc };

//
// RUN_SETUP / RUN_TEARDOWN: the code that brackets the whole selection, named
// the same way -- and the outer pair to the two above. Written at the catalog's
// file scope, outside every GROUP:
//
//   RUN_SETUP(    rigPowerOn,  "Bring this rig's sources up, in order")
//   RUN_TEARDOWN( rigPowerOff, "Take them back down, in the inverse order")
//
// Both are optional and independent: declare one, both, or neither. A catalog
// with no RUN_SETUP line needs no placeholder for one, which is what the
// fallback declarations in core/catalog/active_test_catalog.hpp are for.
//
// At most one of each per catalog -- a second RUN_SETUP is a redefinition error
// at the point it is written, which is the right outcome: two setup routines
// have an order between them that nothing in the file states.
//
// The qualifier is on this pair rather than on the group-level one because of
// where each is written. A group's hooks sit inside the group they bracket, so
// the table says which group they belong to and the name needs only to say what
// they are; this pair sits on its own at file scope, where nothing around it
// says what it brackets.
//
#define RUN_SETUP( hook, desc)                                         \
    inline constexpr ::core::Bracket SetupHook { hook, desc };

#define RUN_TEARDOWN( hook, desc)                                      \
    inline constexpr ::core::Bracket TeardownHook { hook, desc };
