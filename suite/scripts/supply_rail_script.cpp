#include "../scripts.hpp"

#include "core/verify.hpp"
#include "hal/adapter.hpp"

#include "core/active_criteria.hpp"

#include "libs/dut/device_x_profile.inc"

auto supplyRailScript(  std::string_view, std::string_view) -> bool
{
    bool allPassed = true;

    //
    // Reads like the test spec: check this requirement at this port. No
    // instrument, switch path, or rig detail is visible here. DeviceX_StdAdapter::Output5V
    // etc are compile-time-checked -- a typo here is "no such member", not
    // a runtime error.
    //
    const auto dc5Voltage = Measure( Dmm1.voltage(), at( DeviceX_StdAdapter::Output5V));
    const auto dc3Voltage = Measure( Dmm1.voltage(), at( DeviceX_StdAdapter::Output3V3));

    allPassed &= core::Verify( FS_Supply_1::FS_Supply_5V0, dc5Voltage);
    allPassed &= core::Verify( FS_Supply_1::FS_Supply_3V3, dc3Voltage);

    return allPassed;
}
