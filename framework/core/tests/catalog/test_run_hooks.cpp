//
// Hook resolution -- RUN_SETUP/RUN_TEARDOWN around a run, SETUP/TEARDOWN
// around one group -- through the real core/catalog/active_test_catalog.hpp rather than
// a re-implementation of its lookup.
//
// The catalog is a fixture rather than the shipped suite one, supplied by
// redefining THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS before the include (the
// two paths arrive as THORIUM_TEST_*_FIXTURE, see framework/core/CMakeLists.txt).
// That is also why this is the only translation unit in core_tests that includes
// active_test_catalog.hpp at all: it defines namespace core::catalog, and two
// TUs defining it from different catalogs would be an ODR violation rather
// than two independent test cases.
//
// The fixture is framework/runner's -- the same one run_scripts_hooked is built from
// and the acceptance tests drive -- not a copy of it kept here. It was a copy
// until the two files were reconciled, and the copy was worth removing for a
// sharper reason than tidiness: two statements of the same catalog can drift, so
// a change to the hook mechanism could be made to pass here against a shape the
// runner no longer had. The consequence to know about is that the descriptions
// asserted below are the ones the acceptance tests print and assert on too, so
// editing them for either reason fails the other -- which is the point.
//
// What this file does keep is the hook and script *bodies*: it defines its own
// below rather than linking the fixture's, because what these tests need from a
// hook is a flag it can check, and what run_scripts_hooked needs is a line on
// stdout. Only the declarations and the table are shared, which is all that has
// to agree.
//
// The other half of the mechanism -- an undeclared hook resolving to nullptr --
// the shipped suite covers for the group-level pair, whose groups declare no
// hooks at all, so every ordinary build of run_scripts and every acceptance
// test already exercises that resolution. It is covered here too, by the
// fixture's second group: the two resolutions differ (a namespace-scope
// fallback the catalog can shadow, against a class member that is either there
// or not, see core/catalog/active_test_catalog.hpp), and a group that declared nothing
// sitting next to one that declared both is the cheapest way to pin both at
// once.
//
// ---------------------------------------------------------------------------
// Include order matters here, and is the reason this file reads oddly.
//
// core/catalog/test_catalog.hpp's GROUP/TEST/SETUP/TEARDOWN and friends are
// object-like macros at global scope, and GoogleTest's TEST is another one. Whichever header is
// included second redefines the other's, which under -Werror is a hard error --
// so the catalog is included first, its macros are retired once the fixture
// catalog has been expanded, and only then does gtest.h arrive to define its
// own TEST.
//
// Nothing else in the tree hits this: the catalog macros are used by
// suite/test_catalog.inc and framework/runner/src/main.cpp, neither of which links gtest,
// and no other test includes active_test_catalog.hpp. This file is the one
// place both meet, and undef-ing after use is the local fix -- worth knowing
// if a suite ever wants a catalog and a gtest fixture in one translation unit.
// ---------------------------------------------------------------------------
#undef THORIUM_TEST_CATALOG
#undef THORIUM_TEST_SCRIPTS

#define THORIUM_TEST_CATALOG THORIUM_TEST_CATALOG_FIXTURE
#define THORIUM_TEST_SCRIPTS THORIUM_TEST_SCRIPTS_FIXTURE

#include "core/catalog/active_test_catalog.hpp"

#undef GROUP
#undef TEST
#undef END_GROUP
#undef SETUP
#undef TEARDOWN
#undef RUN_SETUP
#undef RUN_TEARDOWN

#include <gtest/gtest.h>

namespace
{
    bool gSetupRan         = false;
    bool gTeardownRan      = false;
    bool gGroupSetupRan    = false;
    bool gGroupTeardownRan = false;
} // namespace

auto fixtureSetup()         -> bool { gSetupRan         = true; return true; }
auto fixtureTeardown()      -> bool { gTeardownRan      = true; return true; }
auto fixtureGroupSetup()    -> bool { gGroupSetupRan    = true; return true; }
auto fixtureGroupTeardown() -> bool { gGroupTeardownRan = true; return true; }

//
// Never actually run by these tests -- what they assert is that the catalog
// resolved to them (see TheCatalogIsStillReadableAlongsideItsHooks below), so
// their bodies have nothing to do. A script that a *runner* reached would have
// to record a check to pass; these are only ever pointed at, which is why these
// bodies are empty where the fixture's own (framework/runner/tests/fixtures/
// hooked_scripts.cpp) announce themselves on stdout.
//
auto fixtureScript()      -> void {}
auto otherFixtureScript() -> void {}

TEST( CoreRunHooks, ADeclaredSetupResolvesToTheNamedFunction)
{
    ASSERT_NE( core::catalog::Setup.hook, nullptr);

    gSetupRan = false;
    EXPECT_TRUE( core::catalog::Setup.hook());
    EXPECT_TRUE( gSetupRan);
}

TEST( CoreRunHooks, ADeclaredTeardownResolvesToTheNamedFunction)
{
    ASSERT_NE( core::catalog::Teardown.hook, nullptr);

    gTeardownRan = false;
    EXPECT_TRUE( core::catalog::Teardown.hook());
    EXPECT_TRUE( gTeardownRan);
}

//
// A hook's description travels with the hook, because a log has nothing else to
// tell one "setup" from another: every hook in a catalog is called setup or
// teardown, at both levels and in every group.
//
TEST( CoreRunHooks, AHookCarriesTheProseItWasDeclaredWith)
{
    EXPECT_EQ( core::catalog::Setup.description,    "Announce the run-level setup");
    EXPECT_EQ( core::catalog::Teardown.description, "Announce the run-level teardown");
}

//
// The hooks are resolved at compile time, like everything else the catalog
// names -- there is no runtime lookup to miss.
//
TEST( CoreRunHooks, HooksAreConstantExpressions)
{
    static_assert( core::catalog::Setup.hook    == &fixtureSetup);
    static_assert( core::catalog::Teardown.hook == &fixtureTeardown);

    SUCCEED();
}

//
// Declaring hooks must not disturb the catalog itself -- RUN_SETUP and
// RUN_TEARDOWN sit beside GROUP/TEST in the same file, and SETUP/TEARDOWN sit
// inside the group table among the very rows they must not be confused with.
// That last one is the claim worth having here: a group's tests are picked out
// of its members by type (see core/catalog/active_test_catalog.hpp), so a hook member
// landing in the tests array would show up as an extra test with a garbage
// script.
//
TEST( CoreRunHooks, TheCatalogIsStillReadableAlongsideItsHooks)
{
    ASSERT_EQ( core::catalog::Catalog.size(), 2u);

    EXPECT_EQ( core::catalog::Catalog[ 0].name, "OutputVoltage");
    ASSERT_EQ( core::catalog::Catalog[ 0].tests.size(), 1u);
    EXPECT_EQ( core::catalog::Catalog[ 0].tests[ 0].id, "FixtureTest");
    EXPECT_EQ( core::catalog::Catalog[ 0].tests[ 0].script, &fixtureScript);

    EXPECT_EQ( core::catalog::Catalog[ 1].name, "Console");
    ASSERT_EQ( core::catalog::Catalog[ 1].tests.size(), 1u);
    EXPECT_EQ( core::catalog::Catalog[ 1].tests[ 0].id, "OtherFixtureTest");
    EXPECT_EQ( core::catalog::Catalog[ 1].tests[ 0].script, &otherFixtureScript);
}

//
// A group's own hooks, resolved onto the group that declared them -- and only
// onto it. Both halves matter: hooks that leaked onto every group would arm a
// rig state for tests that never asked for it, which is precisely what the
// group-level pair exists to avoid.
//
TEST( CoreRunHooks, ADeclaredGroupHookResolvesOntoItsOwnGroup)
{
    ASSERT_NE( core::catalog::Catalog[ 0].setup.hook,    nullptr);
    ASSERT_NE( core::catalog::Catalog[ 0].teardown.hook, nullptr);

    gGroupSetupRan    = false;
    gGroupTeardownRan = false;

    EXPECT_TRUE( core::catalog::Catalog[ 0].setup.hook());
    EXPECT_TRUE( core::catalog::Catalog[ 0].teardown.hook());

    EXPECT_TRUE( gGroupSetupRan);
    EXPECT_TRUE( gGroupTeardownRan);

    // And the prose beside them, which is what a log heads them with.
    EXPECT_EQ( core::catalog::Catalog[ 0].setup.description,    "Announce this group's own setup");
    EXPECT_EQ( core::catalog::Catalog[ 0].teardown.description, "Announce this group's own teardown");
}

//
// The other resolution: a group that declared no hooks gets nullptr, not the
// previous group's. The runner checks before calling (see runOnePass in
// framework/runner/src/main.cpp), so nullptr is the whole of "this group has nothing to do".
//
TEST( CoreRunHooks, AGroupThatDeclaresNoHooksResolvesToNullptr)
{
    EXPECT_EQ( core::catalog::Catalog[ 1].setup.hook,    nullptr);
    EXPECT_EQ( core::catalog::Catalog[ 1].teardown.hook, nullptr);

    // Described by nothing either -- there is nothing there to describe, and a
    // heading in a log is written only where a hook actually ran.
    EXPECT_TRUE( core::catalog::Catalog[ 1].setup.description.empty());
    EXPECT_TRUE( core::catalog::Catalog[ 1].teardown.description.empty());
}

//
// Group hooks are compile-time constants like everything else the catalog
// names -- the same claim HooksAreConstantExpressions above makes for the
// run-level pair, and the reason a misspelled hook is a compile error rather
// than a lookup that finds nothing.
//
TEST( CoreRunHooks, GroupHooksAreConstantExpressions)
{
    static_assert( core::catalog::Catalog[ 0].setup.hook    == &fixtureGroupSetup);
    static_assert( core::catalog::Catalog[ 0].teardown.hook == &fixtureGroupTeardown);
    static_assert( core::catalog::Catalog[ 1].setup.hook    == nullptr);
    static_assert( core::catalog::Catalog[ 1].teardown.hook == nullptr);

    SUCCEED();
}
