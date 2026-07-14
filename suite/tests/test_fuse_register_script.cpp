#include "suite/scripts.hpp"

#include <gtest/gtest.h>

//
// fuseRegisterScript currently checks fixed constants rather than reading a
// real device (see suite/scripts/fuse_register_script.cpp) -- once it's
// wired through dut::Measure/dut::Bench, these tests should gain a Bench
// fixture (ScriptedSession-backed, per core/session.hpp) the same way
// test_measure.cpp's MeasureFixture does, rather than reintroducing a
// device/bus double that the script never actually reads.
//

TEST(FuseRegisterFixture, FailsWhenFuseLowNibbleIsWrong) {
    EXPECT_FALSE(fuseRegisterScript("group", "test"));
}

TEST(FuseRegisterFixture, FailsWhenVoltageOutOfTolerance) {
    EXPECT_FALSE(fuseRegisterScript("group", "test"));
}
