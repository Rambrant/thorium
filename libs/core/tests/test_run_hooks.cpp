//
// SETUP/TEARDOWN resolution, through the real core/active_test_catalog.hpp
// rather than a re-implementation of its lookup.
//
// This file supplies its own catalog (fixtures/hooked_catalog.inc, which
// declares both hooks) instead of the shipped suite one, by redefining
// THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS before the include. That is also
// why this is the only translation unit in core_tests that includes
// active_test_catalog.hpp at all: it defines namespace core::catalog, and two
// TUs defining it from different catalogs would be an ODR violation rather
// than two independent test cases.
//
// The other half of the mechanism -- an undeclared hook resolving to nullptr --
// is what the shipped suite/test_catalog.inc's absent SETUP does (it declares
// TEARDOWN alone), so every ordinary build of run_scripts and every acceptance
// test already exercises that resolution. There is deliberately no second
// fixture here restating it.
//
// ---------------------------------------------------------------------------
// Include order matters here, and is the reason this file reads oddly.
//
// core/test_catalog.hpp's GROUP/TEST/SETUP/TEARDOWN are object-like macros at
// global scope, and GoogleTest's TEST is another one. Whichever header is
// included second redefines the other's, which under -Werror is a hard error --
// so the catalog is included first, its macros are retired once the fixture
// catalog has been expanded, and only then does gtest.h arrive to define its
// own TEST.
//
// Nothing else in the tree hits this: the catalog macros are used by
// suite/test_catalog.inc and app/src/main.cpp, neither of which links gtest,
// and no other test includes active_test_catalog.hpp. This file is the one
// place both meet, and undef-ing after use is the local fix -- worth knowing
// if a suite ever wants a catalog and a gtest fixture in one translation unit.
// ---------------------------------------------------------------------------
#undef THORIUM_TEST_CATALOG
#undef THORIUM_TEST_SCRIPTS

#define THORIUM_TEST_CATALOG THORIUM_TEST_CATALOG_FIXTURE
#define THORIUM_TEST_SCRIPTS THORIUM_TEST_SCRIPTS_FIXTURE

#include "core/active_test_catalog.hpp"

#undef GROUP
#undef TEST
#undef END_GROUP
#undef SETUP
#undef TEARDOWN

#include <gtest/gtest.h>

namespace
{
    bool gSetupRan    = false;
    bool gTeardownRan = false;
} // namespace

auto fixtureSetup()    -> bool { gSetupRan    = true; return true; }
auto fixtureTeardown() -> bool { gTeardownRan = true; return true; }
auto fixtureScript()   -> bool { return true; }

TEST( CoreRunHooks, ADeclaredSetupResolvesToTheNamedFunction)
{
    ASSERT_NE( core::catalog::Setup, nullptr);

    gSetupRan = false;
    EXPECT_TRUE( core::catalog::Setup());
    EXPECT_TRUE( gSetupRan);
}

TEST( CoreRunHooks, ADeclaredTeardownResolvesToTheNamedFunction)
{
    ASSERT_NE( core::catalog::Teardown, nullptr);

    gTeardownRan = false;
    EXPECT_TRUE( core::catalog::Teardown());
    EXPECT_TRUE( gTeardownRan);
}

//
// The hooks are resolved at compile time, like everything else the catalog
// names -- there is no runtime lookup to miss.
//
TEST( CoreRunHooks, HooksAreConstantExpressions)
{
    static_assert( core::catalog::Setup    == &fixtureSetup);
    static_assert( core::catalog::Teardown == &fixtureTeardown);

    SUCCEED();
}

//
// Declaring hooks must not disturb the catalog itself -- SETUP/TEARDOWN sit
// beside GROUP/TEST in the same file and have to stay independent of it.
//
TEST( CoreRunHooks, TheCatalogIsStillReadableAlongsideItsHooks)
{
    ASSERT_EQ( core::catalog::Catalog.size(), 1u);
    EXPECT_EQ( core::catalog::Catalog[ 0].name, "OutputVoltage");
    ASSERT_EQ( core::catalog::Catalog[ 0].tests.size(), 1u);
    EXPECT_EQ( core::catalog::Catalog[ 0].tests[ 0].id, "FixtureTest");
    EXPECT_EQ( core::catalog::Catalog[ 0].tests[ 0].script, &fixtureScript);
}
