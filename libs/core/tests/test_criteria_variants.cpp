#include "core/criteria_variants.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/verify.hpp"

using namespace core::quantities;

//
// The runtime half of the tolerance-variant mechanism: which variant a run
// applies, and the dispatch that turns that choice into which of a
// MultiCriterion's tolerances actually gets evaluated.
//
// The compile-time half -- that a merged table exists at all, that a typo is
// still a compile error, that every variant declares the same ids -- is not
// testable from here by construction: it either compiled or this binary does
// not exist. suite/tests/test_criteria_variants_compile.cpp is where that half
// is asserted, and suite/tests/test_supply_rail_script.cpp is where the two
// meet on a real script.
//
namespace
{
    //
    // The selection is process-wide (see core/criteria_variants.hpp on why it
    // has to be), so every test here has to hand it back the way it found it --
    // including the lock, which a test that has verified locking would
    // otherwise leave set for every test after it, in this file and every other
    // one linked into core_tests.
    //
    struct CriteriaVariants : ::testing::Test
    {
        protected:

            void SetUp()    override { core::resetCriteriaVariantForTesting(); }
            void TearDown() override { core::resetCriteriaVariantForTesting(); }
    };

    //
    // How many variants the linking deployment actually declares, and the guard
    // the tests below that need more than one go through.
    //
    // These used to name this repository's bench tables outright --
    // selectCriteriaVariant( "stress"), activeCriteriaVariant() == 2u -- which
    // read as harmless in a repository with one deployment and was not: what
    // THORIUM_KNOWN_CRITERIA_VARIANTS holds is a property of the DUT, and a
    // deployment with one tolerance table has no "stress" to select. core_tests
    // is the framework's own target, so it must not assert the contents of a
    // list the framework does not own (the dev deployment declares exactly one
    // -- see dev/dut/criteria_production.inc).
    //
    // By index rather than by name, therefore, and skipped rather than weakened
    // where there are not enough: selection *between* variants is the mechanism
    // under test, and a single-variant deployment has nothing to select between.
    // A version of these that quietly asserted only what one variant can show
    // would keep passing while testing nothing.
    //
    constexpr std::size_t kVariantsNeededForSelection = 3;

    //
    // A MultiCriterion built by hand rather than through a merged table: the
    // dispatch under test cares only about the tuple's shape, and a hand-built
    // one lets a variant be given a deliberately distinguishable tolerance
    // (0.5 / 1.5 / 2.5) so a wrong pick is visible as a wrong verdict rather
    // than needing the tolerances of the real dut/criteria_*.inc files to
    // happen to differ in a useful place.
    //
    // Ordered to match THORIUM_KNOWN_CRITERIA_VARIANTS -- production, stress,
    // aged -- which is the same order criteriaVariantNames() reports and the
    // same order the generated tables build their tuples in.
    //
    constexpr auto WideningCriterion = core::makeMultiCriterion(
        "TestGroup", "WidensPerVariant",
        core::makeCriterion( "TestGroup", "WidensPerVariant", "production", EQ( 10.0).epsilon( 0.5)),
        core::makeCriterion( "TestGroup", "WidensPerVariant", "stress",     EQ( 10.0).epsilon( 1.5)),
        core::makeCriterion( "TestGroup", "WidensPerVariant", "aged",       EQ( 10.0).epsilon( 2.5)));
} // namespace

// ---------------------------------------------------------------------------
// The variant list
// ---------------------------------------------------------------------------

//
// Generated from THORIUM_KNOWN_CRITERIA_VARIANTS, so this is really asserting
// that the generation ran and reached this binary -- a core built without a
// criteria layer falls back to a single "unknown" entry, and silently linking
// that into a real deployment would make --criteria= reject every name a
// caller could reasonably pass.
//
TEST_F( CriteriaVariants, EveryKnownVariantIsCompiledIn)
{
    const auto names = core::criteriaVariantNames();

    ASSERT_FALSE( names.empty());

    //
    // The fallback is a single entry spelled "unknown" (see
    // core/detail/no_criteria_variants.inc), so this is the assertion that
    // distinguishes "generation ran and reached this binary" from "core was
    // built without a criteria layer". Every real deployment names its tables
    // something, and none of them names one that.
    //
    EXPECT_EQ( std::ranges::find( names, std::string_view( "unknown")), names.end())
        << "the THORIUM_KNOWN_CRITERIA_VARIANTS generation did not reach this binary";

    for( const auto name : names)
    {
        EXPECT_FALSE( name.empty());
        EXPECT_TRUE( core::selectCriteriaVariant( name)) << "a declared variant must be selectable: " << name;
    }
}

TEST_F( CriteriaVariants, TheDefaultIsOneOfThemAndIsActiveWithoutAsking)
{
    const auto names = core::criteriaVariantNames();

    EXPECT_NE( std::ranges::find( names, core::defaultCriteriaVariantName()), names.end());

    EXPECT_EQ( core::activeCriteriaVariantName(), core::defaultCriteriaVariantName());
}

// ---------------------------------------------------------------------------
// Selecting
// ---------------------------------------------------------------------------

TEST_F( CriteriaVariants, SelectingByNameMovesTheActiveVariant)
{
    const auto names = core::criteriaVariantNames();

    for( std::size_t index = 0; index < names.size(); ++index)
    {
        ASSERT_TRUE( core::selectCriteriaVariant( names[ index]));

        EXPECT_EQ( core::activeCriteriaVariantName(), names[ index]);
        EXPECT_EQ( core::activeCriteriaVariant(), index)
            << names[ index] << " is at position " << index << " in THORIUM_KNOWN_CRITERIA_VARIANTS";
    }
}

//
// False rather than an exception, and nothing changed: an unrecognised name is
// a caller error that a runner reports and refuses to run on (see main.cpp),
// not a programming fault.
//
TEST_F( CriteriaVariants, AnUnknownNameChangesNothingAndSaysSo)
{
    const auto selected = core::criteriaVariantNames().back();

    ASSERT_TRUE( core::selectCriteriaVariant( selected));

    EXPECT_FALSE( core::selectCriteriaVariant( "no-such-variant"));
    EXPECT_EQ( core::activeCriteriaVariantName(), selected) << "a rejected selection must not disturb the current one";
}

//
// The guarantee that makes a log's traceability header worth reading: once the
// run has begun, what the header says the tolerances were is what they stayed.
//
TEST_F( CriteriaVariants, TheSelectionCannotChangeOnceTheRunHasBegun)
{
    const auto names = core::criteriaVariantNames();

    if( names.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one criteria variant -- no second one to try to move to";
    }

    ASSERT_TRUE( core::selectCriteriaVariant( names[ 0]));

    core::lockCriteriaVariant();

    EXPECT_THROW( (void)core::selectCriteriaVariant( names[ 1]), std::logic_error);
    EXPECT_EQ( core::activeCriteriaVariantName(), names[ 0]);
}

// ---------------------------------------------------------------------------
// Dispatch: which tolerance a Verify actually applies
// ---------------------------------------------------------------------------

//
// 11.0 against a criterion expecting 10.0: outside production's +/-0.5 and
// stress's +/-1.5 is a different answer from aged's +/-2.5, so one value
// distinguishes all three and a verdict is enough to say which was applied.
//
TEST_F( CriteriaVariants, VerifyAppliesTheSelectedVariantsTolerance)
{
    const auto names = core::criteriaVariantNames();

    if( names.size() < kVariantsNeededForSelection)
    {
        GTEST_SKIP() << "WideningCriterion has three variants and dispatch is by index -- "
                        "this deployment declares " << names.size();
    }

    ASSERT_TRUE( core::selectCriteriaVariant( names[ 0]));
    EXPECT_FALSE( core::Verify( WideningCriterion, 11.0)) << "the first variant's +/-0.5 must reject 11.0";

    ASSERT_TRUE( core::selectCriteriaVariant( names[ 1]));
    EXPECT_TRUE( core::Verify( WideningCriterion, 11.0)) << "the second variant's +/-1.5 must accept 11.0";

    ASSERT_TRUE( core::selectCriteriaVariant( names[ 2]));
    EXPECT_TRUE( core::Verify( WideningCriterion, 11.0));

    // ...and 13.0 is outside all three, so nothing here passes by accident.
    EXPECT_FALSE( core::Verify( WideningCriterion, 13.0));
}

//
// The metadata a log keys results by comes off the MultiCriterion itself
// (identical across variants by construction), while the description comes off
// the variant that was applied -- which is what lets a report quote the
// tolerance that was really enforced. See core::MultiCriterion.
//
TEST_F( CriteriaVariants, GroupAndIdAreSharedWhileDescriptionsAreNot)
{
    static_assert( WideningCriterion.group == "TestGroup");
    static_assert( WideningCriterion.id    == "WidensPerVariant");
    static_assert( WideningCriterion.VariantCount == 3);

    static_assert( std::get<0>( WideningCriterion.variants).description == "production");
    static_assert( std::get<2>( WideningCriterion.variants).description == "aged");

    SUCCEED();
}
