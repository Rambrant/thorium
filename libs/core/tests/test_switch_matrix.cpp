#include "core/switch_matrix.hpp"

#include <gtest/gtest.h>

TEST( CoreCrosspoint, EqualityComparesRowAndCol)
{
    core::Crosspoint a{ 3, 7 };
    core::Crosspoint b{ 3, 7 };
    core::Crosspoint c{ 3, 8 };

    EXPECT_EQ( a, b);
    EXPECT_NE( a, c);
}
