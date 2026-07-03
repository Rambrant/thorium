#include "dsl/predicates.hpp"

#include <gtest/gtest.h>

TEST(EqPredicate, ExactMatchOnIntegers)
{
    auto pred = dsl::EQ(42);
    EXPECT_TRUE(pred(42));
    EXPECT_FALSE(pred(43));
}

TEST(EqPredicate, WithinToleranceOnFloatingPoint)
{
    auto pred = dsl::EQ(12.0).within(0.05);
    EXPECT_TRUE(pred(12.03));
    EXPECT_FALSE(pred(12.10));
}

//
// EQ(42).within(1) must fail to compile for an int -- this is caught by the
// `requires std::floating_point<T>` constraint, not left as a silent no-op.
// static_assert(!requires { dsl::EQ(42).within(1); });
//
TEST( RangePredicate, IntegerRangeInclusive)
{
    auto pred = dsl::IN( 10, 20);

    EXPECT_TRUE( pred( 10));
    EXPECT_TRUE( pred( 20));
    EXPECT_FALSE( pred (9));
    EXPECT_FALSE( pred( 21));
}

TEST( RangePredicate, FloatingRangeWithTolerance)
{
    auto pred = dsl::IN( 10.0, 20.0).within(0.5);

    EXPECT_TRUE( pred( 9.6));
    EXPECT_TRUE( pred( 20.4));
    EXPECT_FALSE( pred( 9.0));
}

TEST( BandPredicate, MaskedComparison)
{
    auto pred = dsl::BAND( 0x0Fu, 0x05u);

    EXPECT_TRUE( pred(0xF5u));  // low nibble is 0x5
    EXPECT_FALSE( pred(0xF6u)); // low nibble is 0x6
}
