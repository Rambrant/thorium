#include "core/criteria/predicates.hpp"
#include "core/quantities/quantity.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace core::literals;
using namespace core::quantities;

TEST( EqPredicate, ExactMatchOnIntegers)
{
    auto pred = EQ( 42);

    EXPECT_TRUE( pred( 42));
    EXPECT_FALSE( pred( 43));
}

TEST( EqPredicate, EpsilonToleranceOnFloatingPoint)
{
    auto pred = EQ( 12.0).epsilon( 0.05);

    EXPECT_TRUE( pred( 12.03));
    EXPECT_FALSE( pred( 12.10));
}

TEST( EqPredicate, EpsilonToleranceOnQuantity)
{
    auto pred = EQ( 3.3_W).epsilon( 0.05_W);

    EXPECT_TRUE( pred( 3.32_W));
    EXPECT_FALSE( pred( 3.40_W));
}

//
// EQ / NE / LT / LE / GT / GE / IN all share one naming convention for
// tolerance: a `.epsilon(...)` fluent call, one name across the whole
// family (see the comment in predicates.hpp). All default to machine
// epsilon without calling epsilon() at all -- a floating_point/Quantity<Unit>
// comparison is never really exact, so this default only guards against
// representation error, not a substitute for a real epsilon( 0.05_V).
//
TEST( EqPredicate, DefaultToleranceIsMachineEpsilonNotZero)
{
    auto pred = EQ( 5.0); // no .epsilon() at all

    EXPECT_TRUE(  pred( 5.0));
    EXPECT_TRUE(  pred( 5.0 + std::numeric_limits<double>::epsilon()));
    EXPECT_FALSE( pred( 5.001));
}

TEST( RangePredicate, DefaultToleranceIsMachineEpsilonNotZero)
{
    auto pred = IN( 5.0, 10.0); // no .epsilon() at all

    EXPECT_TRUE(  pred( 5.0 - std::numeric_limits<double>::epsilon()));
    EXPECT_FALSE( pred( 4.999));
}

//
// EQ(42).epsilon(1) must fail to compile for an int -- caught by the
// `requires Toleranced<T>` constraint, not left as a silent no-op.
// See test_static_constraints.cpp for the compile-time proof of this.
//   e.g. EQ( 42).epsilon( 1);  // does not compile
//

TEST( NePredicate, ExactMismatchOnIntegers)
{
    auto pred = NE( 42);

    EXPECT_TRUE( pred( 43));
    EXPECT_FALSE( pred( 42));
}

TEST( NePredicate, WorksOnQuantity)
{
    auto pred = NE( 3.3_W);

    EXPECT_TRUE( pred( 3.4_W));
    EXPECT_FALSE( pred( 3.3_W));
}

//
// NE is modeled as NotPredicate<EqPredicate<T>> -- literally !EQ, not a
// second, independently-written formula -- so EQ and NE can never disagree
// for the same tolerance, by construction.
//
TEST( NePredicate, IsStructurallyTheExactComplementOfEq)
{
    for( double eps : { 0.0, 0.001, 0.01, 0.1})
    {
        auto eq = EQ( 5.0).epsilon( eps);
        auto ne = NE( 5.0).epsilon( eps);

        for( double actual : { 4.9, 4.99, 4.999, 5.0, 5.001, 5.01, 5.1})
        {
            EXPECT_EQ( eq( actual), ! ne( actual));
        }
    }
}

TEST( LtPredicate, StrictlyLessThan)
{
    auto pred = LT( 10.0);

    EXPECT_TRUE( pred( 9.99));
    EXPECT_FALSE( pred( 10.0));
    EXPECT_FALSE( pred( 10.01));
}

TEST( LePredicate, LessThanOrEqual)
{
    auto pred = LE( 10.0);

    EXPECT_TRUE( pred( 9.99));
    EXPECT_TRUE( pred( 10.0));
    EXPECT_FALSE( pred( 10.01));
}

TEST( GtPredicate, StrictlyGreaterThan)
{
    auto pred = GT( 10.0);

    EXPECT_TRUE( pred( 10.01));
    EXPECT_FALSE( pred( 10.0));
    EXPECT_FALSE( pred( 9.99));
}

TEST( GePredicate, GreaterThanOrEqual)
{
    auto pred = GE( 10.0);

    EXPECT_TRUE( pred( 10.01));
    EXPECT_TRUE( pred( 10.0));
    EXPECT_FALSE( pred( 9.99));
}

TEST( RelationalPredicates, WorkOnQuantityTheSameAsOnRawTypes)
{
    EXPECT_TRUE(  LT( 5.0_V)( 4.9_V));
    EXPECT_FALSE( LT( 5.0_V)( 5.0_V));
    EXPECT_TRUE(  GE( 5.0_V)( 5.0_V));
    EXPECT_TRUE(  NE( 5.0_V)( 4.9_V));
}

//
// epsilon() on LT/LE/GT/GE/NE is boundary-safe tolerance: it only ever
// matters right at the boundary, softening a strict inequality against
// floating-point representation error. For EQ/IN (above), the same
// .epsilon(...) call instead widens a symmetric plus-or-minus band. Same
// name, same underlying idea (a tolerance around a comparison), applied to
// a strict boundary in one case and a band in the other.
//
TEST( RelationalPredicates, DefaultToleranceIsMachineEpsilon)
{
    // One ULP below 5.0 is still "less than 5.0" at the default (machine
    // epsilon) tolerance -- the default is deliberately tiny, just enough
    // to be a safety net against representation error, not a real margin.
    EXPECT_TRUE( LT( 5.0)( 4.999999999999999));
    EXPECT_FALSE( LT( 5.0)( 5.0));
}

TEST( LtPredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = LT( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 4.98));
    EXPECT_FALSE( pred( 4.995)); // within epsilon of the boundary -- not "less"
    EXPECT_FALSE( pred( 5.0));
}

TEST( LePredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = LE( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 5.005)); // within epsilon above -- still "<="
    EXPECT_FALSE( pred( 5.02));
}

TEST( GtPredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = GT( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 5.02));
    EXPECT_FALSE( pred( 5.005));
}

TEST( GePredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = GE( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 4.995));
    EXPECT_FALSE( pred( 4.98));
}

TEST( NePredicate, ExplicitEpsilonTreatsCloseValuesAsEqual)
{
    auto pred = NE( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 5.02));
    EXPECT_FALSE( pred( 5.005)); // within epsilon -- "equal enough", not NE
}

TEST( RelationalPredicates, EpsilonWorksOnQuantity)
{
    auto pred = LT( 5.0_V).epsilon( 0.05_V);

    EXPECT_TRUE(  pred( 4.9_V));
    EXPECT_FALSE( pred( 4.98_V)); // within 0.05V of the boundary
}

//
// epsilon() is deliberately restricted to Toleranced types (floating_point
// or Quantity<Unit>) -- an exact type has no meaningful notion of "close
// enough". See test_static_constraints.cpp.
//   e.g. LT( 5).epsilon( 1);  // does not compile
//

TEST( RangePredicate, IntegerRangeInclusive)
{
    auto pred = IN( 10, 20);

    EXPECT_TRUE( pred( 10));
    EXPECT_TRUE( pred( 20));
    EXPECT_FALSE( pred( 9));
    EXPECT_FALSE( pred( 21));
}

TEST( RangePredicate, EpsilonToleranceOnFloatingPoint)
{
    auto pred = IN( 10.0, 20.0).epsilon( 0.5);

    EXPECT_TRUE( pred( 9.6));
    EXPECT_TRUE( pred( 20.4));
    EXPECT_FALSE( pred( 9.0));
}

TEST( RangePredicate, EpsilonToleranceOnQuantityRange)
{
    auto pred = IN( 3.0_V, 3.6_V).epsilon( 0.05_V);

    EXPECT_TRUE( pred( 2.96_V));
    EXPECT_FALSE( pred( 2.90_V));
}

TEST( MaskPredicate, MaskedComparison)
{
    auto pred = MASK( 0x0Fu, 0x05u);

    EXPECT_TRUE( pred( 0xF5u));  // low nibble is 0x5
    EXPECT_FALSE( pred( 0xF6u)); // low nibble is 0x6
}

//
// MASK is deliberately restricted to std::integral -- masking a float or a
// Quantity<Unit> is not a meaningful operation. See test_static_constraints.cpp.
//   e.g. MASK( 0.0F, 0.0F);  // does not compile
//

TEST( AnyPredicate, MatchesAnyOptionInTheList)
{
    auto pred = ANY( 1, 3, 5, 7);

    EXPECT_TRUE( pred( 3));
    EXPECT_TRUE( pred( 7));
    EXPECT_FALSE( pred( 4));
}

TEST( AnyPredicate, WorksOnQuantity)
{
    auto pred = ANY( 3.3_V, 5.0_V, 12.0_V);

    EXPECT_TRUE( pred( 5.0_V));
    EXPECT_FALSE( pred( 9.0_V));
}

//
// ANY delegates each option to an EqPredicate carrying its tolerance, so it
// picks up the same `.epsilon(...)` as the rest of the family -- one tolerance
// for the whole list, since a list of options is one criterion with one margin.
//
TEST( AnyPredicate, EpsilonToleranceAppliesToEveryOption)
{
    auto pred = ANY( 3.3_V, 5.0_V, 12.0_V).epsilon( 0.05_V);

    EXPECT_TRUE(  pred( 3.32_V));  // within tolerance of the first option
    EXPECT_TRUE(  pred( 11.96_V)); // ...and of the last
    EXPECT_FALSE( pred( 3.40_V));
    EXPECT_FALSE( pred( 9.00_V));  // near no option at all
}

TEST( AnyPredicate, EpsilonToleranceOnFloatingPoint)
{
    auto pred = ANY( 1.0, 2.0, 3.0).epsilon( 0.1);

    EXPECT_TRUE(  pred( 2.05));
    EXPECT_FALSE( pred( 2.50));
}

//
// Without an explicit epsilon, ANY on a Quantity used to be an exact double
// comparison -- the trap the machine-epsilon default exists to close for
// EQ/IN/LT/... This pins ANY to the same default as the rest of them.
//
TEST( AnyPredicate, DefaultToleranceIsMachineEpsilonNotZero)
{
    auto pred = ANY( 3.3_V, 5.0_V); // no .epsilon() at all

    EXPECT_TRUE(  pred( 5.0_V));
    EXPECT_TRUE(  pred( Voltage{ 5.0 + std::numeric_limits<double>::epsilon() }));
    EXPECT_FALSE( pred( 5.001_V));
}

//
// ANY is a disjunction of EQs and is built as exactly that, so the two can
// never disagree about what "equal enough" means for a given tolerance.
//
TEST( AnyPredicate, AgreesWithEqOnEveryOptionForTheSameTolerance)
{
    for( double eps : { 0.0, 0.001, 0.01, 0.1})
    {
        auto any = ANY( 1.0, 5.0).epsilon( eps);

        for( double actual : { 0.9, 0.999, 1.0, 1.001, 4.99, 5.0, 5.01})
        {
            const bool eitherEq = EQ( 1.0).epsilon( eps)( actual) ||
                                  EQ( 5.0).epsilon( eps)( actual);

            EXPECT_EQ( any( actual), eitherEq);
        }
    }
}

TEST( AnyPredicate, ExactTypesStillCompareExactly)
{
    auto pred = ANY( 1, 3, 5, 7); // int -- defaultEpsilon is 0

    EXPECT_TRUE(  pred( 3));
    EXPECT_FALSE( pred( 4));
}

//
// ANY requires every option to be the exact same type as the first --
// consistent with EQ/IN/etc. never mixing units.
//   e.g. ANY( 3.3_V, 5.0_A);  // does not compile
//
// And epsilon() on an exact-typed ANY is refused for the same reason it is on
// EQ -- see test_static_constraints.cpp.
//   e.g. ANY( 1, 3).epsilon( 1);  // does not compile

TEST( NonePredicate, MatchesWhenTheValueIsInNoOption)
{
    auto pred = NONE( 1, 3, 5, 7);

    EXPECT_TRUE(  pred( 4));
    EXPECT_FALSE( pred( 3));
}

TEST( NonePredicate, WorksOnQuantity)
{
    auto pred = NONE( 3.3_V, 5.0_V, 12.0_V);

    EXPECT_TRUE(  pred( 9.0_V));
    EXPECT_FALSE( pred( 5.0_V));
}

//
// NONE is NotPredicate{ ANY( ...)}, so its tolerance is ANY's -- forwarded by
// NotPredicate rather than declared a second time (see core/criteria/predicates.hpp).
//
TEST( NonePredicate, EpsilonIsForwardedToTheWrappedAny)
{
    auto pred = NONE( 3.3_V, 5.0_V).epsilon( 0.05_V);

    EXPECT_TRUE(  pred( 3.40_V));
    EXPECT_FALSE( pred( 3.32_V)); // within epsilon of an option -- "one of", so not "none of"
}

//
// The complement property, over the same tolerances and values ANY's own test
// sweeps: NONE is !ANY by construction, so the two can never disagree.
//
TEST( NonePredicate, IsStructurallyTheExactComplementOfAny)
{
    for( double eps : { 0.0, 0.001, 0.01, 0.1})
    {
        auto any  = ANY(  1.0, 5.0).epsilon( eps);
        auto none = NONE( 1.0, 5.0).epsilon( eps);

        for( double actual : { 0.9, 0.999, 1.0, 1.001, 4.99, 5.0, 5.01})
        {
            EXPECT_EQ( any( actual), ! none( actual));
        }
    }
}

//
// ANY_OF: the disjunction ANY cannot express, because a range is not a value.
//
TEST( AnyOfPredicate, MatchesWhenAnyMemberPredicateMatches)
{
    auto pred = ANY_OF( EQ( 5.0_V), IN( 3.0_V, 3.6_V), GT( 12.0_V));

    EXPECT_TRUE(  pred( 5.0_V));  // the EQ
    EXPECT_TRUE(  pred( 3.3_V));  // the IN
    EXPECT_TRUE(  pred( 15.0_V)); // the GT
    EXPECT_FALSE( pred( 9.0_V));  // none of them
}

TEST( AnyOfPredicate, MembersKeepTheirOwnTolerances)
{
    auto pred = ANY_OF( EQ( 5.0_V).epsilon( 0.05_V), IN( 3.0_V, 3.6_V).epsilon( 0.10_V));

    EXPECT_TRUE(  pred( 5.03_V)); // inside the EQ's own epsilon
    EXPECT_FALSE( pred( 5.20_V)); // outside it
    EXPECT_TRUE(  pred( 2.95_V)); // inside the IN's own, wider epsilon
    EXPECT_FALSE( pred( 2.80_V));
}

TEST( AnyOfPredicate, ComposesRangesIntoDisjointBands)
{
    auto pred = ANY_OF( IN( 3.0_V, 3.6_V), IN( 4.75_V, 5.25_V));

    EXPECT_TRUE(  pred( 3.3_V));
    EXPECT_TRUE(  pred( 5.0_V));
    EXPECT_FALSE( pred( 4.0_V)); // between the two bands
}

TEST( AnyOfPredicate, NestsTheRestOfTheVocabulary)
{
    EXPECT_TRUE( ANY_OF( NE( 5.0_V), LT( 1.0_V))( 4.0_V));
    EXPECT_TRUE( ANY_OF( ANY( 3.3_V, 5.0_V), GT( 12.0_V))( 3.3_V));
    EXPECT_TRUE( ANY_OF( MASK( 0x0Fu, 0x05u), EQ( 0xF0u))( 0xA5u));
}

TEST( AnyOfPredicate, ASingleMemberBehavesAsThatMember)
{
    auto pred = ANY_OF( IN( 3.0_V, 3.6_V));

    EXPECT_TRUE(  pred( 3.3_V));
    EXPECT_FALSE( pred( 4.0_V));
}

//
// The fold is over || and therefore short-circuits -- a member after the first
// match is never called. Worth pinning: a member can be a rig's own predicate
// doing something less pure than comparing a number.
//
TEST( AnyOfPredicate, ShortCircuitsAfterTheFirstMatch)
{
    int calls = 0;

    auto counted = [ &calls]( const double) { ++calls; return false; };
    auto pred    = ANY_OF( EQ( 5.0), counted);

    EXPECT_TRUE( pred( 5.0));
    EXPECT_EQ( calls, 0); // the EQ matched, so the lambda was never reached

    EXPECT_FALSE( pred( 9.0));
    EXPECT_EQ( calls, 1);
}

//
// ANY_OF holds any callable, exactly as core::Verify does -- a rig's own
// predicate or a one-off lambda is a legal member.
//
TEST( AnyOfPredicate, AcceptsAnArbitraryCallableAsAMember)
{
    auto pred = ANY_OF( IN( 3.0, 3.6), []( const double value) { return value < 0.0; });

    EXPECT_TRUE(  pred( 3.3));
    EXPECT_TRUE(  pred( -1.0));
    EXPECT_FALSE( pred( 10.0));
}

//
// ANY_OF's members need not share a value type, so there is no single epsilon
// for the combinator to take -- each member carries its own. And a mixed-unit
// ANY_OF is still a compile error, at the point of use rather than of
// construction. See test_static_constraints.cpp for both, and the comment on
// ANY_OF in core/criteria/predicates.hpp for why.
//   e.g. ANY_OF( EQ( 5.0_V), IN( 3.0_V, 3.6_V)).epsilon( 0.05_V);      // does not compile
//   e.g. ANY_OF( EQ( 3.3_V), IN( 1.0_A, 2.0_A))( 3.3_V);               // does not compile
//   e.g. ANY_OF();                                                     // does not compile
//