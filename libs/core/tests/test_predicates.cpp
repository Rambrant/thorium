#include "core/predicates.hpp"
#include "core/quantity.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace core::literals;

TEST( EqPredicate, ExactMatchOnIntegers)
{
    auto pred = core::EQ( 42);

    EXPECT_TRUE( pred( 42));
    EXPECT_FALSE( pred( 43));
}

TEST( EqPredicate, EpsilonToleranceOnFloatingPoint)
{
    auto pred = core::EQ( 12.0).epsilon( 0.05);

    EXPECT_TRUE( pred( 12.03));
    EXPECT_FALSE( pred( 12.10));
}

TEST( EqPredicate, EpsilonToleranceOnQuantity)
{
    auto pred = core::EQ( 3.3_W).epsilon( 0.05_W);

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
    auto pred = core::EQ( 5.0); // no .epsilon() at all

    EXPECT_TRUE(  pred( 5.0));
    EXPECT_TRUE(  pred( 5.0 + std::numeric_limits<double>::epsilon()));
    EXPECT_FALSE( pred( 5.001));
}

TEST( RangePredicate, DefaultToleranceIsMachineEpsilonNotZero)
{
    auto pred = core::IN( 5.0, 10.0); // no .epsilon() at all

    EXPECT_TRUE(  pred( 5.0 - std::numeric_limits<double>::epsilon()));
    EXPECT_FALSE( pred( 4.999));
}

//
// EQ(42).epsilon(1) must fail to compile for an int -- caught by the
// `requires Toleranced<T>` constraint, not left as a silent no-op.
// See test_static_constraints.cpp for the compile-time proof of this.
//   e.g. core::EQ( 42).epsilon( 1);  // does not compile
//

TEST( NePredicate, ExactMismatchOnIntegers)
{
    auto pred = core::NE( 42);

    EXPECT_TRUE( pred( 43));
    EXPECT_FALSE( pred( 42));
}

TEST( NePredicate, WorksOnQuantity)
{
    auto pred = core::NE( 3.3_W);

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
        auto eq = core::EQ( 5.0).epsilon( eps);
        auto ne = core::NE( 5.0).epsilon( eps);

        for( double actual : { 4.9, 4.99, 4.999, 5.0, 5.001, 5.01, 5.1})
        {
            EXPECT_EQ( eq( actual), ! ne( actual));
        }
    }
}

TEST( LtPredicate, StrictlyLessThan)
{
    auto pred = core::LT( 10.0);

    EXPECT_TRUE( pred( 9.99));
    EXPECT_FALSE( pred( 10.0));
    EXPECT_FALSE( pred( 10.01));
}

TEST( LePredicate, LessThanOrEqual)
{
    auto pred = core::LE( 10.0);

    EXPECT_TRUE( pred( 9.99));
    EXPECT_TRUE( pred( 10.0));
    EXPECT_FALSE( pred( 10.01));
}

TEST( GtPredicate, StrictlyGreaterThan)
{
    auto pred = core::GT( 10.0);

    EXPECT_TRUE( pred( 10.01));
    EXPECT_FALSE( pred( 10.0));
    EXPECT_FALSE( pred( 9.99));
}

TEST( GePredicate, GreaterThanOrEqual)
{
    auto pred = core::GE( 10.0);

    EXPECT_TRUE( pred( 10.01));
    EXPECT_TRUE( pred( 10.0));
    EXPECT_FALSE( pred( 9.99));
}

TEST( RelationalPredicates, WorkOnQuantityTheSameAsOnRawTypes)
{
    EXPECT_TRUE(  core::LT( 5.0_V)( 4.9_V));
    EXPECT_FALSE( core::LT( 5.0_V)( 5.0_V));
    EXPECT_TRUE(  core::GE( 5.0_V)( 5.0_V));
    EXPECT_TRUE(  core::NE( 5.0_V)( 4.9_V));
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
    EXPECT_TRUE( core::LT( 5.0)( 4.999999999999999));
    EXPECT_FALSE( core::LT( 5.0)( 5.0));
}

TEST( LtPredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = core::LT( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 4.98));
    EXPECT_FALSE( pred( 4.995)); // within epsilon of the boundary -- not "less"
    EXPECT_FALSE( pred( 5.0));
}

TEST( LePredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = core::LE( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 5.005)); // within epsilon above -- still "<="
    EXPECT_FALSE( pred( 5.02));
}

TEST( GtPredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = core::GT( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 5.02));
    EXPECT_FALSE( pred( 5.005));
}

TEST( GePredicate, ExplicitEpsilonWidensTheBoundary)
{
    auto pred = core::GE( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 4.995));
    EXPECT_FALSE( pred( 4.98));
}

TEST( NePredicate, ExplicitEpsilonTreatsCloseValuesAsEqual)
{
    auto pred = core::NE( 5.0).epsilon( 0.01);

    EXPECT_TRUE(  pred( 5.02));
    EXPECT_FALSE( pred( 5.005)); // within epsilon -- "equal enough", not NE
}

TEST( RelationalPredicates, EpsilonWorksOnQuantity)
{
    auto pred = core::LT( 5.0_V).epsilon( 0.05_V);

    EXPECT_TRUE(  pred( 4.9_V));
    EXPECT_FALSE( pred( 4.98_V)); // within 0.05V of the boundary
}

//
// epsilon() is deliberately restricted to Toleranced types (floating_point
// or Quantity<Unit>) -- an exact type has no meaningful notion of "close
// enough". See test_static_constraints.cpp.
//   e.g. core::LT( 5).epsilon( 1);  // does not compile
//

TEST( RangePredicate, IntegerRangeInclusive)
{
    auto pred = core::IN( 10, 20);

    EXPECT_TRUE( pred( 10));
    EXPECT_TRUE( pred( 20));
    EXPECT_FALSE( pred( 9));
    EXPECT_FALSE( pred( 21));
}

TEST( RangePredicate, EpsilonToleranceOnFloatingPoint)
{
    auto pred = core::IN( 10.0, 20.0).epsilon( 0.5);

    EXPECT_TRUE( pred( 9.6));
    EXPECT_TRUE( pred( 20.4));
    EXPECT_FALSE( pred( 9.0));
}

TEST( RangePredicate, EpsilonToleranceOnQuantityRange)
{
    auto pred = core::IN( 3.0_V, 3.6_V).epsilon( 0.05_V);

    EXPECT_TRUE( pred( 2.96_V));
    EXPECT_FALSE( pred( 2.90_V));
}

TEST( MaskPredicate, MaskedComparison)
{
    auto pred = core::MASK( 0x0Fu, 0x05u);

    EXPECT_TRUE( pred( 0xF5u));  // low nibble is 0x5
    EXPECT_FALSE( pred( 0xF6u)); // low nibble is 0x6
}

//
// MASK is deliberately restricted to std::integral -- masking a float or a
// Quantity<Unit> is not a meaningful operation. See test_static_constraints.cpp.
//   e.g. core::MASK( 0.0F, 0.0F);  // does not compile
//

TEST( AnyPredicate, MatchesAnyOptionInTheList)
{
    auto pred = core::ANY( 1, 3, 5, 7);

    EXPECT_TRUE( pred( 3));
    EXPECT_TRUE( pred( 7));
    EXPECT_FALSE( pred( 4));
}

TEST( AnyPredicate, WorksOnQuantity)
{
    auto pred = core::ANY( 3.3_V, 5.0_V, 12.0_V);

    EXPECT_TRUE( pred( 5.0_V));
    EXPECT_FALSE( pred( 9.0_V));
}

//
// ANY requires every option to be the exact same type as the first --
// consistent with EQ/IN/etc. never mixing units.
//   e.g. core::ANY( 3.3_V, 5.0_A);  // does not compile
//