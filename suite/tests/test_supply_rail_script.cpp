#include "suite/scripts.hpp"

#include <gtest/gtest.h>

//
// supplyRailScript currently checks fixed constants rather than measuring a
// real device (see suite/scripts/supply_rail_script.cpp). Once it's wired
// through dut::Measure/dut::Bench, these tests should gain a Bench fixture
// (core::ScriptedSession-backed) so each test can program its own readings
// per point name -- the direct replacement for the old FakeDevice map this
// file used to hand-roll; see libs/dut/tests/test_measure.cpp for what that
// looks like against the new dut::Measure/dut::Bench seam.
//

TEST(SupplyRailScript, PassesWhenBothRailsInTolerance)
{
    EXPECT_TRUE(supplyRailScript("group", "test"));
}
