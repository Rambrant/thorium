#include "hal/rig.hpp"

#include <gtest/gtest.h>

#include "core/rig.hpp"

TEST(HalRig, ExposesInstrumentsThroughCoreInterface) {
    hal::SimRig rig;
    core::IRig& as_interface = rig;
    EXPECT_NE(as_interface.oscilloscope(), nullptr);
    EXPECT_NE(as_interface.voltmeter(), nullptr);
    EXPECT_NE(as_interface.power_supply(), nullptr);
}

TEST(HalRig, ProgrammedScopeReadingIsVisibleThroughInterface) {
    hal::SimRig rig;
    rig.sim_scope().set_level(core::Voltage{5.0});

    core::IRig& as_interface = rig;
    EXPECT_DOUBLE_EQ(as_interface.oscilloscope()->measure_level().value(), 5.0);
}

TEST(HalRig, PowerSupplyRoundTripsThroughInterface) {
    hal::SimRig rig;
    core::IRig& as_interface = rig;
    auto* supply = as_interface.power_supply();
    supply->set_output(core::Voltage{3.3});
    supply->enable();

    EXPECT_DOUBLE_EQ(rig.sim_supply().output().value(), 3.3);
    EXPECT_TRUE(rig.sim_supply().enabled());
}

TEST(HalRig, MatrixAccessibleThroughInterface) {
    hal::SimRig rig;
    core::IRig& as_interface = rig;
    as_interface.matrix().close({2, 3});
    EXPECT_TRUE(as_interface.matrix().is_closed({2, 3}));
}

TEST(HalRig, PerCrosspointVoltmeterReadingFollowsMatrixRouting) {
    hal::SimRig rig;
    rig.sim_voltmeter().set_reading_at({3, 7}, core::Voltage{5.0});
    rig.sim_voltmeter().set_reading_at({3, 6}, core::Voltage{3.3});

    core::IRig& as_interface = rig;

    as_interface.matrix().close({3, 7});
    EXPECT_DOUBLE_EQ(as_interface.voltmeter()->measure_voltage().value(), 5.0);

    as_interface.matrix().open_all();
    as_interface.matrix().close({3, 6});
    EXPECT_DOUBLE_EQ(as_interface.voltmeter()->measure_voltage().value(), 3.3);
}
