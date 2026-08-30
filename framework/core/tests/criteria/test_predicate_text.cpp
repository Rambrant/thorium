#include "core/criteria/predicate_text.hpp"

#include <gtest/gtest.h>

#include "core/criteria/criterion.hpp"
#include "core/criteria/predicates.hpp"
#include "core/criteria/verify.hpp"

using namespace core::literals;
using namespace core::quantities;

//
// Rendering a criterion's own tolerance, so a log line can state what was
// required next to what was measured -- see core/criteria/predicate_text.hpp on why the
// criterion's *description* can't serve that purpose.
//

TEST( CorePredicateText, EqualityStatesItsExpectedValue)
{
    EXPECT_EQ( core::describeCriterion( EQ( 5.0_V)), "= 5 V");
    EXPECT_EQ( core::describeCriterion( EQ( 0xF5u)), "= 245 (0xF5)");
}

TEST( CorePredicateText, EqualityStatesItsToleranceWhenOneWasChosen)
{
    EXPECT_EQ( core::describeCriterion( EQ( 5.0_V).epsilon( 0.05_V)), "= 5 V +/-50 mV");
    EXPECT_EQ( core::describeCriterion( EQ( 3.3_V).epsilon( 0.05_V)), "= 3.3 V +/-50 mV");
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
    EXPECT_EQ( core::describeCriterion( IN( 3.0_V, 3.6_V).epsilon( 0.05_V)), "in [3 V .. 3.6 V] +/-50 mV");
}

TEST( CorePredicateText, RelationalPredicatesUseTheirOwnSymbol)
{
    EXPECT_EQ( core::describeCriterion( LT( 5.0_V)), "< 5 V");
    EXPECT_EQ( core::describeCriterion( LE( 5.0_V)), "<= 5 V");
    EXPECT_EQ( core::describeCriterion( GT( 5.0_V)), "> 5 V");
    EXPECT_EQ( core::describeCriterion( GE( 5.0_V)), ">= 5 V");

    EXPECT_EQ( core::describeCriterion( GE( 4.75_V).epsilon( 0.05_V)), ">= 4.75 V +/-50 mV");
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
// One tolerance for the whole list, reported once after it -- ANY applies the
// same epsilon to every option (see core/criteria/predicates.hpp), so stating it per
// option would be three ways to write the same number.
//
TEST( CorePredicateText, AnyStatesItsToleranceWhenOneWasChosen)
{
    EXPECT_EQ( core::describeCriterion( ANY( 3.3_V, 5.0_V).epsilon( 0.05_V)), "one of [3.3 V, 5 V] +/-50 mV");
    EXPECT_EQ( core::describeCriterion( ANY( 3.3_V, 5.0_V)),                  "one of [3.3 V, 5 V]");
}

//
// NE is NotPredicate{ EQ(...)} rather than its own formula (see
// core/criteria/predicates.hpp), but "!= 5 V" is what a reader expects -- not
// "not (= 5 V)".
//
TEST( CorePredicateText, NotEqualReadsAsAnInequalityRatherThanANegation)
{
    EXPECT_EQ( core::describeCriterion( NE( 5.0_V)),                   "!= 5 V");
    EXPECT_EQ( core::describeCriterion( NE( 5.0_V).epsilon( 0.05_V)), "!= 5 V +/-50 mV");
}

//
// NONE is NotPredicate{ ANY(...)} for the same reason, and gets the same
// treatment -- "none of [...]", not "not (one of [...])".
//
TEST( CorePredicateText, NoneReadsAsAnExcludedListRatherThanANegatedOne)
{
    EXPECT_EQ( core::describeCriterion( NONE( 3.3_V, 5.0_V)),                   "none of [3.3 V, 5 V]");
    EXPECT_EQ( core::describeCriterion( NONE( 3.3_V, 5.0_V).epsilon( 0.05_V)), "none of [3.3 V, 5 V] +/-50 mV");
}

//
// "one of" and "none of" render the same option list identically -- they share
// one helper, so a report cannot show the two criteria's options differently.
//
TEST( CorePredicateText, AnyAndNoneRenderTheirOptionListTheSameWay)
{
    const auto any  = core::describeCriterion( ANY(  3.3_V, 3.6_V, 5.0_V));
    const auto none = core::describeCriterion( NONE( 3.3_V, 3.6_V, 5.0_V));

    EXPECT_EQ( any.substr(  any.find(  '[')), "[3.3 V, 3.6 V, 5 V]");
    EXPECT_EQ( none.substr( none.find( '[')), "[3.3 V, 3.6 V, 5 V]");
}

TEST( CorePredicateText, OtherNegationsWrapTheirInnerDescription)
{
    EXPECT_EQ( core::describeCriterion( NotPredicate{ IN( 3.0_V, 3.6_V) }), "not (in [3 V .. 3.6 V])");
}

//
// Each member rendered by its own overload, so a nested predicate reads exactly
// as it would alone -- and "any of" rather than ANY's "one of", so a report says
// which of the two criteria ran.
//
TEST( CorePredicateText, AnyOfListsItsMemberPredicates)
{
    EXPECT_EQ( core::describeCriterion( ANY_OF( EQ( 5.0_V), IN( 3.0_V, 3.6_V))),
               "any of [= 5 V, in [3 V .. 3.6 V]]");

    EXPECT_EQ( core::describeCriterion( ANY_OF( GT( 12.0_V))), "any of [> 12 V]");
}

TEST( CorePredicateText, AnyOfCarriesItsMembersOwnTolerances)
{
    EXPECT_EQ( core::describeCriterion( ANY_OF( EQ( 5.0_V).epsilon( 0.05_V), IN( 3.0_V, 3.6_V))),
               "any of [= 5 V +/-50 mV, in [3 V .. 3.6 V]]");
}

//
// Members are looked up through the whole overload set, including the ones
// declared after ANY_OF's own -- so a negation, an ANY, or another ANY_OF
// nests and reads correctly, in either direction.
//
TEST( CorePredicateText, AnyOfNestsAndIsNestable)
{
    EXPECT_EQ( core::describeCriterion( ANY_OF( NE( 5.0_V), ANY( 1.0_V, 2.0_V))),
               "any of [!= 5 V, one of [1 V, 2 V]]");

    EXPECT_EQ( core::describeCriterion( ANY_OF( ANY_OF( EQ( 1.0_V), EQ( 2.0_V)), GT( 12.0_V))),
               "any of [any of [= 1 V, = 2 V], > 12 V]");

    EXPECT_EQ( core::describeCriterion( NotPredicate{ ANY_OF( EQ( 5.0_V), GT( 12.0_V)) }),
               "not (any of [= 5 V, > 12 V])");
}

//
// Half a disjunction is not a weaker description of the criterion, it is a
// different and stricter one -- so an ANY_OF holding anything undescribable
// yields nothing at all, rather than the members it happens to be able to name.
//
TEST( CorePredicateText, AnyOfWithAnUndescribableMemberDescribesNothing)
{
    const auto pred = ANY_OF( EQ( 5.0), []( const double value) { return value < 0.0; });

    static_assert( !core::DescribablePredicate<decltype( pred)>);

    EXPECT_TRUE( core::describeCriterion( pred).empty());
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
    CRIT( WithinTolerance, EQ( 5.0_V).epsilon( 0.05_V),            "5V rail")
    CRIT( RegisterPattern, MASK( 0x0Fu, 0x05u),                    "low nibble")
    CRIT( NotARailValue,   NONE( 3.3_V, 5.0_V).epsilon( 0.05_V),   "no nominal rail")
    CRIT( EitherRail,      ANY_OF( IN( 3.0_V, 3.6_V), EQ( 5.0_V)), "3V3 band or 5V")
END_CRITERIA

TEST( CorePredicateText, CritDeclaredCriteriaDescribeTheirOwnPredicate)
{
    EXPECT_EQ( core::describeCriterion( PredicateTextGroup::WithinTolerance.predicate), "= 5 V +/-50 mV");
    EXPECT_EQ( core::describeCriterion( PredicateTextGroup::RegisterPattern.predicate), "(value & 0xF) == 0x5");
}

//
// A CRIT entry is static constexpr, so a predicate that cannot be built at
// compile time cannot be declared in a criteria table at all -- which is the
// only place most predicates are ever written. ANY_OF holds a std::tuple, so
// this is the check that matters for it: constructed, described, and applied,
// all from the table rather than inline.
//
TEST( CorePredicateText, NoneAndAnyOfAreUsableFromACriteriaTable)
{
    EXPECT_EQ( core::describeCriterion( PredicateTextGroup::NotARailValue.predicate),
               "none of [3.3 V, 5 V] +/-50 mV");
    EXPECT_EQ( core::describeCriterion( PredicateTextGroup::EitherRail.predicate),
               "any of [in [3 V .. 3.6 V], = 5 V]");

    EXPECT_TRUE(  core::Verify( PredicateTextGroup::NotARailValue, 9.0_V));
    EXPECT_FALSE( core::Verify( PredicateTextGroup::NotARailValue, 3.32_V));

    EXPECT_TRUE(  core::Verify( PredicateTextGroup::EitherRail, 3.3_V));
    EXPECT_TRUE(  core::Verify( PredicateTextGroup::EitherRail, 5.0_V));
    EXPECT_FALSE( core::Verify( PredicateTextGroup::EitherRail, 9.0_V));
}
