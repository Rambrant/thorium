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

    EXPECT_EQ( names.size(), 3u);

    EXPECT_NE( std::ranges::find( names, std::string_view( "production")), names.end());
    EXPECT_NE( std::ranges::find( names, std::string_view( "stress")),     names.end());
    EXPECT_NE( std::ranges::find( names, std::string_view( "aged")),       names.end());
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
    ASSERT_TRUE( core::selectCriteriaVariant( "aged"));

    EXPECT_EQ( core::activeCriteriaVariantName(), "aged");
    EXPECT_EQ( core::activeCriteriaVariant(), 2u) << "aged is third in THORIUM_KNOWN_CRITERIA_VARIANTS";
}

//
// False rather than an exception, and nothing changed: an unrecognised name is
// a caller error that a runner reports and refuses to run on (see main.cpp),
// not a programming fault.
//
TEST_F( CriteriaVariants, AnUnknownNameChangesNothingAndSaysSo)
{
    ASSERT_TRUE( core::selectCriteriaVariant( "stress"));

    EXPECT_FALSE( core::selectCriteriaVariant( "no-such-variant"));
    EXPECT_EQ( core::activeCriteriaVariantName(), "stress") << "a rejected selection must not disturb the current one";
}

//
// The guarantee that makes a log's traceability header worth reading: once the
// run has begun, what the header says the tolerances were is what they stayed.
//
TEST_F( CriteriaVariants, TheSelectionCannotChangeOnceTheRunHasBegun)
{
    ASSERT_TRUE( core::selectCriteriaVariant( "stress"));

    core::lockCriteriaVariant();

    EXPECT_THROW( (void)core::selectCriteriaVariant( "aged"), std::logic_error);
    EXPECT_EQ( core::activeCriteriaVariantName(), "stress");
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
    ASSERT_TRUE( core::selectCriteriaVariant( "production"));
    EXPECT_FALSE( core::Verify( WideningCriterion, 11.0)) << "production's +/-0.5 must reject 11.0";

    ASSERT_TRUE( core::selectCriteriaVariant( "stress"));
    EXPECT_TRUE( core::Verify( WideningCriterion, 11.0)) << "stress's +/-1.5 must accept 11.0";

    ASSERT_TRUE( core::selectCriteriaVariant( "aged"));
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
