#include "core/predicate_text.hpp"

#include <gtest/gtest.h>

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/verify.hpp"

using namespace core::literals;
using namespace core::quantities;

//
// Rendering a criterion's own tolerance, so a log line can state what was
// required next to what was measured -- see core/predicate_text.hpp on why the
// criterion's *description* can't serve that purpose.
//

TEST( CorePredicateText, EqualityStatesItsExpectedValue)
{
    EXPECT_EQ( core::describeCriterion( EQ( 5.0_V)), "= 5 V");
    EXPECT_EQ( core::describeCriterion( EQ( 0xF5u)), "= 245 (0xF5)");
}

TEST( CorePredicateText, EqualityStatesItsToleranceWhenOneWasChosen)
{
    EXPECT_EQ( core::describeCriterion( EQ( 5.0_V).epsilon( 0.05_V)), "= 5 V +/-0.05 V");
    EXPECT_EQ( core::describeCriterion( EQ( 3.3_V).epsilon( 0.05_V)), "= 3.3 V +/-0.05 V");
}

//
// The machine-epsilon default absorbs representation error; it is not a physical
// margin anybody chose. Printing it would put "+/-2.22045e-16 V" in a test
// report and imply somebody did.
//
TEST( CorePredicateText, DefaultEpsilonIsNotReportedAsATolerance)
{
    const auto text = core::describeCriterion( EQ( 5.0_V));

    EXPECT_EQ( text, "= 5 V");
    EXPECT_EQ( text.find( "+/-"), std::string::npos);
}

TEST( CorePredicateText, RangeStatesBothBounds)
{
    EXPECT_EQ( core::describeCriterion( IN( 3.0_V, 3.6_V)),                   "in [3 V .. 3.6 V]");
    EXPECT_EQ( core::describeCriterion( IN( 3.0_V, 3.6_V).epsilon( 0.05_V)), "in [3 V .. 3.6 V] +/-0.05 V");
}

TEST( CorePredicateText, RelationalPredicatesUseTheirOwnSymbol)
{
    EXPECT_EQ( core::describeCriterion( LT( 5.0_V)), "< 5 V");
    EXPECT_EQ( core::describeCriterion( LE( 5.0_V)), "<= 5 V");
    EXPECT_EQ( core::describeCriterion( GT( 5.0_V)), "> 5 V");
    EXPECT_EQ( core::describeCriterion( GE( 5.0_V)), ">= 5 V");

    EXPECT_EQ( core::describeCriterion( GE( 4.75_V).epsilon( 0.05_V)), ">= 4.75 V +/-0.05 V");
}

//
// A mask is a bit-pattern expression, so it gets hex only -- core::describeValue
// gives an integral in both bases ("15 (0x0F)"), which is right for a reading
// and noise inside this.
//
TEST( CorePredicateText, MaskSpellsOutTheBitwiseExpressionInHex)
{
    EXPECT_EQ( core::describeCriterion( MASK( 0x0Fu, 0x05u)), "(value & 0xF) == 0x5");
    EXPECT_EQ( core::describeCriterion( MASK( 0xFFu, 0xF5u)), "(value & 0xFF) == 0xF5");
}

TEST( CorePredicateText, AnyListsItsOptions)
{
    EXPECT_EQ( core::describeCriterion( ANY( 3.3_V, 3.6_V, 5.0_V)), "one of [3.3 V, 3.6 V, 5 V]");
}

//
// NE is NotPredicate{ EQ(...)} rather than its own formula (see
// core/predicates.hpp), but "!= 5 V" is what a reader expects -- not
// "not (= 5 V)".
//
TEST( CorePredicateText, NotEqualReadsAsAnInequalityRatherThanANegation)
{
    EXPECT_EQ( core::describeCriterion( NE( 5.0_V)),                   "!= 5 V");
    EXPECT_EQ( core::describeCriterion( NE( 5.0_V).epsilon( 0.05_V)), "!= 5 V +/-0.05 V");
}

TEST( CorePredicateText, OtherNegationsWrapTheirInnerDescription)
{
    EXPECT_EQ( core::describeCriterion( NotPredicate{ IN( 3.0_V, 3.6_V) }), "not (in [3 V .. 3.6 V])");
}

//
// The empty-string contract, and the reason it exists: core::Verify accepts any
// callable, so a rig checking something with a one-off lambda has to keep
// working. It logs without a required-column entry rather than failing to build.
//
TEST( CorePredicateText, APredicateThatCannotDescribeItselfYieldsNothing)
{
    const auto lambda = []( const double value) { return value > 0.0; };

    static_assert( !core::DescribablePredicate<decltype( lambda)>);
    static_assert(  core::DescribablePredicate<decltype( EQ( 5.0_V))>);

    EXPECT_TRUE( core::describeCriterion( lambda).empty());
}

//
// And it still verifies -- the point of the degradation is that an
// undescribable predicate costs a log column, not a run.
//
TEST( CorePredicateText, AnUndescribablePredicateStillVerifies)
{
    const auto criterion = core::makeCriterion( "Ad", "Hoc", "positive",
        []( const double value) { return value > 0.0; });

    EXPECT_TRUE(  core::Verify( criterion,  1.0));
    EXPECT_FALSE( core::Verify( criterion, -1.0));
}

//
// The text reaches the log through the criterion, so a CRIT-declared criterion
// (the normal case) has to carry it too -- not just a predicate built inline.
//
CRITERIA( PredicateTextGroup, "A group used purely for unit-testing criterion text")
    CRIT( WithinTolerance, EQ( 5.0_V).epsilon( 0.05_V), "5V rail")
    CRIT( RegisterPattern, MASK( 0x0Fu, 0x05u),         "low nibble")
END_CRITERIA

TEST( CorePredicateText, CritDeclaredCriteriaDescribeTheirOwnPredicate)
{
    EXPECT_EQ( core::describeCriterion( PredicateTextGroup::WithinTolerance.predicate), "= 5 V +/-0.05 V");
    EXPECT_EQ( core::describeCriterion( PredicateTextGroup::RegisterPattern.predicate), "(value & 0xF) == 0x5");
}
