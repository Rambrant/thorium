#pragma once

#include "dut/device.hpp"

// Each function here is a "test script": a scenario expressed using the
// dsl layer, exercising the dut through hal. Scripts should never reach
// down into hal:: or dut:: directly for behavior -- only to construct the
// device handle they're given.
namespace scripts {

// Verifies the device can be powered on and off, and reports the correct
// state at each step.
[[nodiscard]] bool power_cycle_script(dut::Device& device);

// Verifies a value written to the device round-trips correctly.
[[nodiscard]] bool value_script(dut::Device& device);

}  // namespace scripts
