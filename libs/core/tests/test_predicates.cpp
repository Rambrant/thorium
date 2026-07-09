#include "core/predicates.hpp"
#include "core/quantity.hpp"

#include <gtest/gtest.h>

using namespace core::literals;

TEST( EqPredicate, ExactMatchOnIntegers)
{
    auto pred = core::EQ( 42);

    EXPECT_TRUE( pred( 42));
    EXPECT_FALSE( pred( 43));
}

TEST( EqPredicate, WithinToleranceOnFloatingPoint)
{
    auto pred = core::EQ( 12.0).within( 0.05);

    EXPECT_TRUE( pred( 12.03));
    EXPECT_FALSE( pred( 12.10));
}

TEST( EqPredicate, WithinToleranceOnQuantity)
{
    auto pred = core::EQ( 3.3_W).within( 0.05_W);

    EXPECT_TRUE( pred( 3.32_W));
    EXPECT_FALSE( pred( 3.40_W));
}

//
// EQ(42).within(1) must fail to compile for an int -- caught by the
// `requires Toleranced<T>` constraint, not left as a silent no-op.
// See test_static_constraints.cpp for the compile-time proof of this.
//   e.g. core::EQ( 42).within( 1);  // does not compile
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

TEST( RangePredicate, IntegerRangeInclusive)
{
    auto pred = core::IN( 10, 20);

    EXPECT_TRUE( pred( 10));
    EXPECT_TRUE( pred( 20));
    EXPECT_FALSE( pred( 9));
    EXPECT_FALSE( pred( 21));
}

TEST( RangePredicate, FloatingRangeWithTolerance)
{
    auto pred = core::IN( 10.0, 20.0).within( 0.5);

    EXPECT_TRUE( pred( 9.6));
    EXPECT_TRUE( pred( 20.4));
    EXPECT_FALSE( pred( 9.0));
}

TEST( RangePredicate, WithinToleranceOnQuantityRange)
{
    auto pred = core::IN( 3.0_V, 3.6_V).within( 0.05_V);

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