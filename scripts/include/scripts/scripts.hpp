#pragma once

#include "dut/device.hpp"
#include "dut/rig_device.hpp"

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

// Verifies fuse register content and output voltage against expected
// criteria. Demonstrates the declarative Criterion/Verify DSL style,
// as opposed to the fluent TestCase style used above.
[[nodiscard]] bool fuse_register_script(dut::Device& device);

// Verifies supply rail voltages by measuring at named test points. This is
// the instrument/matrix path: the script names *what* to check (a criterion)
// and *where* (a logical test point) -- the dsl::Measure verb hides the
// select-instrument / route-matrix / fetch sequence entirely.
[[nodiscard]] bool supply_rail_script(dut::DeviceView& device);

}  // namespace scripts

