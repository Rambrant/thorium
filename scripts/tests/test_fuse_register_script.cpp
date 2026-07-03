#include "scripts/scripts.hpp"

#include <gtest/gtest.h>

#include "dut/device.hpp"
#include "hal/bus.hpp"

namespace {

struct FuseRegisterFixture : public ::testing::Test {
    hal::Bus bus;
    dut::Device device{bus};
};

}  // namespace

TEST_F(FuseRegisterFixture, PassesWhenFuseAndVoltageAreWithinCriteria) {
    bus.write_register(dut::Device::kFuseRegister, 0xF5);
    bus.write_register(dut::Device::kVoltageRegister, 12030);
    EXPECT_TRUE(scripts::fuse_register_script(device));
}

TEST_F(FuseRegisterFixture, FailsWhenFuseLowNibbleIsWrong) {
    bus.write_register(dut::Device::kFuseRegister, 0xF6);  // low nibble should be 0x5
    bus.write_register(dut::Device::kVoltageRegister, 12030);
    EXPECT_FALSE(scripts::fuse_register_script(device));
}

TEST_F(FuseRegisterFixture, FailsWhenVoltageOutOfTolerance) {
    bus.write_register(dut::Device::kFuseRegister, 0xF5);
    bus.write_register(dut::Device::kVoltageRegister, 12500);  // 12.5V, outside 12.0 +/- 0.05
    EXPECT_FALSE(scripts::fuse_register_script(device));
}
