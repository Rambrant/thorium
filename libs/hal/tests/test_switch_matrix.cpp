#include "hal/switch_matrix.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

TEST(HalSwitchMatrix, StartsFullyOpen) {
    hal::SwitchMatrix matrix(4, 4);
    EXPECT_FALSE(matrix.is_closed({0, 0}));
    EXPECT_FALSE(matrix.is_closed({3, 3}));
}

TEST(HalSwitchMatrix, CloseThenIsClosed) {
    hal::SwitchMatrix matrix(4, 4);
    matrix.close({1, 2});
    EXPECT_TRUE(matrix.is_closed({1, 2}));
    EXPECT_FALSE(matrix.is_closed({2, 1}));
}

TEST(HalSwitchMatrix, OpenClearsSingleCrosspoint) {
    hal::SwitchMatrix matrix(4, 4);
    matrix.close({1, 2});
    matrix.open({1, 2});
    EXPECT_FALSE(matrix.is_closed({1, 2}));
}

TEST(HalSwitchMatrix, OpenAllClearsEverything) {
    hal::SwitchMatrix matrix(4, 4);
    matrix.close({0, 0});
    matrix.close({3, 3});
    matrix.open_all();
    EXPECT_FALSE(matrix.is_closed({0, 0}));
    EXPECT_FALSE(matrix.is_closed({3, 3}));
}

TEST(HalSwitchMatrix, OutOfBoundsThrows) {
    hal::SwitchMatrix matrix(4, 4);
    EXPECT_THROW(matrix.close({4, 0}), std::out_of_range);
    EXPECT_THROW(matrix.close({0, 4}), std::out_of_range);
}
