#include "core/criteria/verify.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "core/criteria/criteria_variants.hpp"
#include "core/criteria/criterion.hpp"
#include "core/journal/journal.hpp"
#include "core/criteria/predicate_text.hpp"
#include "core/criteria/predicates.hpp"
#include "core/testing/capturing_sink.hpp"

using namespace core::quantities;

//
// Exercise the CRITERIA/CRIT macros directly, the same way a real test script would.
//
CRITERIA( VerifyTestGroup, "A group used purely for unit-testing Verify")
    CRIT( SomeRegisterCheck, MASK(0x0Fu, 0x05u), "Low nibble must be 0x5")
END_CRITERIA

TEST( CoreVerify, CriterionOverloadReturnsTrueOnPass)
{
    EXPECT_TRUE( core::Verify( VerifyTestGroup::SomeRegisterCheck, 0xF5u));
}

TEST( CoreVerify, CriterionOverloadReturnsFalseOnFail)
{
    EXPECT_FALSE( core::Verify( VerifyTestGroup::SomeRegisterCheck, 0xF6u));
}

TEST( CoreVerify, AdHocOverloadWithAnExplicitGroupAndIdWorksWithoutCritMacro)
{
    EXPECT_TRUE(  core::Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0).epsilon( 0.05), 12.50));
}

//
// The three-argument form: one line of prose, the predicate, the value -- no
// group and no id, because an ad-hoc check belongs to no criteria table and has
// no identity to track between runs. See the overload's own comment in
// core/criteria/verify.hpp, and CoreSarifRuleId below for what its results index under.
//
TEST( CoreVerify, AdHocOverloadNeedsNothingButProse)
{
    EXPECT_TRUE(  core::Verify( "Supply voltage at Vout", EQ( 12.0).epsilon( 0.05), 12.03));
    EXPECT_FALSE( core::Verify( "Supply voltage at Vout", EQ( 12.0).epsilon( 0.05), 12.50));
}

//
// Unit safety is undiminished by dropping the identifiers -- the three-argument
// form forwards to the very same Criterion overload, so a predicate in the
// wrong unit is still a compile error rather than a check that quietly compares
// numbers. (The negative half of that is in test_static_constraints.cpp; what
// this asserts is that the positive half still routes through.)
//
TEST( CoreVerify, AdHocOverloadKeepsQuantityChecking)
{
    using namespace core::literals;

    EXPECT_TRUE(  core::Verify( "Primary AC at nominal", EQ( 115_V).epsilon( 2_V), 114.2_V));
    EXPECT_FALSE( core::Verify( "Primary AC at nominal", EQ( 115_V).epsilon( 2_V), 111.0_V));
}

// ---------------------------------------------------------------------------
// Fail: a check that could not be made
// ---------------------------------------------------------------------------
//
// Unlike the Verify tests above, these assert on what reached the journal --
// which is all there is to assert: Fail returns nothing, because it evaluates
// nothing (see core/criteria/verify.hpp on that, and on why it posts as a Verb::Verify
// rather than as a verb of its own). The event it posts is the whole of its
// behaviour, and the verdict of the test around it is derived from that same
// event by core::Journal::endTest.
//
namespace
{
    //
    // The journal and the criteria selection are both process-wide (see
    // core/journal/journal.hpp and core/criteria/criteria_variants.hpp on why), so this hands
    // both back the way it found them -- a leaked sink would collect events
    // from every test after it in this binary, and a leaked variant selection
    // would quietly change which tolerance the tests after it are held to.
    //
    struct CoreFail : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::journal().clearSinks();
                core::journal().add( Sink);
                core::resetCriteriaVariantForTesting();
            }

            void TearDown() override
            {
                core::journal().clearSinks();
                core::resetCriteriaVariantForTesting();
            }

            core::CapturingSink Sink;
    };

    //
    // Ordered to match THORIUM_KNOWN_CRITERIA_VARIANTS -- production, stress,
    // aged -- with a deliberately distinguishable tolerance per variant, the
    // same way test_criteria_variants.cpp builds one: a wrong pick then shows
    // up as the wrong tolerance in the log rather than needing the real
    // dut/criteria_*.inc files to differ in a useful place.
    //
    constexpr auto WidensPerVariant = core::makeMultiCriterion(
        "TestGroup", "WidensPerVariant",
        core::makeCriterion( "TestGroup", "WidensPerVariant", "production", EQ( 10.0).epsilon( 0.5)),
        core::makeCriterion( "TestGroup", "WidensPerVariant", "stress",     EQ( 10.0).epsilon( 1.5)),
        core::makeCriterion( "TestGroup", "WidensPerVariant", "aged",       EQ( 10.0).epsilon( 2.5)));
} // namespace

//
// The prose form: one failed check in the log, attributed to no criterion at
// all. Every field is asserted, because each one is a decision -- the empty
// subject (an ad-hoc check has no identity to track), the placeholder in the
// value column, the absent number and unit, and the empty tolerance.
//
TEST_F( CoreFail, ProseFormPostsOneUnattributedFailedCheck)
{
    core::Fail( "the capture did not complete");

    ASSERT_EQ( Sink.Events.size(), 1u);

    const auto & event = Sink.Events.front();

    EXPECT_EQ( event.Method, core::Verb::Verify);
    EXPECT_EQ( event.Passed, std::optional<bool>{ false });

    EXPECT_EQ( event.Subject,       "");
    EXPECT_EQ( event.SubjectGroup,  "");
    EXPECT_EQ( event.Detail,        "the capture did not complete");
    EXPECT_EQ( event.Value,         "<unchecked>");
    EXPECT_EQ( event.CriterionText, "");
    EXPECT_EQ( event.Unit,          "");

    EXPECT_FALSE( event.Numeric.has_value()) << "nothing was measured, so there is no number to compare";
}

//
// The named form keeps the criterion's group and id -- which is what a machine
// consumer indexes a result by (see core::SarifSink::ruleIdFor) -- and still
// states the tolerance that was in force, so the row says what was required
// even though nothing arrived to check against it.
//
TEST_F( CoreFail, NamedFormKeepsTheCriterionsIdentityAndItsTolerance)
{
    core::Fail( VerifyTestGroup::SomeRegisterCheck, "the DUT never answered");

    ASSERT_EQ( Sink.Events.size(), 1u);

    const auto & event = Sink.Events.front();

    EXPECT_EQ( event.Subject,      "SomeRegisterCheck");
    EXPECT_EQ( event.SubjectGroup, "VerifyTestGroup");
    EXPECT_EQ( event.Value,        "<unchecked>");
    EXPECT_EQ( event.Passed,       std::optional<bool>{ false });

    EXPECT_EQ( event.CriterionText, core::describeCriterion( MASK( 0x0Fu, 0x05u)));
    EXPECT_FALSE( event.CriterionText.empty());
}

//
// The reason displaces the criterion's own prose, deliberately -- see the
// overload's comment in core/criteria/verify.hpp. Asserted rather than left implicit
// because it is the one field where a reader could reasonably expect the other
// choice.
//
TEST_F( CoreFail, NamedFormReportsTheReasonRatherThanTheCriterionsDescription)
{
    core::Fail( VerifyTestGroup::SomeRegisterCheck, "the DUT never answered");

    ASSERT_EQ( Sink.Events.size(), 1u);

    EXPECT_EQ( Sink.Events.front().Detail, "the DUT never answered");
    EXPECT_NE( Sink.Events.front().Detail, "Low nibble must be 0x5");
}

//
// A MultiCriterion is what a CRIT in a merged table actually is, so this is the
// overload a real script reaches. The tolerance quoted has to be the selected
// variant's, for the same reason a verdict would be: a log states the limit
// that was actually in force.
//
TEST_F( CoreFail, NamedFormOnAMultiCriterionQuotesTheSelectedVariantsTolerance)
{
    //
    // The second variant, by index rather than by the bench's name for it --
    // see test_criteria_variants.cpp's own note on why core_tests must not
    // assert what THORIUM_KNOWN_CRITERIA_VARIANTS contains. Skipped where there
    // is no second variant: what this checks is that the *selected* one's
    // tolerance is quoted, and with one variant there is no selection to get
    // wrong.
    //
    const auto names = core::criteriaVariantNames();

    if( names.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one criteria variant -- nothing to select between";
    }

    ASSERT_TRUE( core::selectCriteriaVariant( names[ 1]));

    core::Fail( WidensPerVariant, "no reading arrived");

    ASSERT_EQ( Sink.Events.size(), 1u);

    const auto & event = Sink.Events.front();

    EXPECT_EQ( event.Subject,      "WidensPerVariant");
    EXPECT_EQ( event.SubjectGroup, "TestGroup");

    EXPECT_EQ( event.CriterionText, core::describeCriterion( EQ( 10.0).epsilon( 1.5)))
        << "the second variant is selected, so its tolerance is the one in force";

    EXPECT_NE( event.CriterionText, core::describeCriterion( EQ( 10.0).epsilon( 0.5)));
}
