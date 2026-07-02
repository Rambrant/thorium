#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/logger.hpp"
#include "dut/device.hpp"

namespace dsl {

// Result of a single step within a TestCase.
struct StepResult {
    std::string description;
    bool passed;
};

// A small fluent DSL for writing test scripts against a dut::Device.
// Each method appends a StepResult and returns *this so calls can be chained:
//
//   dsl::TestCase("power cycles correctly", device)
//       .power_on()
//       .expect_powered_on()
//       .power_off()
//       .expect_powered_off()
//       .run();
//
// This is the layer test *scripts* (in scripts/) are written against —
// scripts should not need to touch hal:: or dut:: directly.
class TestCase {
public:
    TestCase(std::string name, dut::Device& device);

    TestCase& power_on();
    TestCase& power_off();
    TestCase& expect_powered_on();
    TestCase& expect_powered_off();

    TestCase& set_value(std::uint32_t value);
    TestCase& expect_value(std::uint32_t expected);

    // Finalizes the test case: logs a summary and returns true only if
    // every step passed.
    bool run();

    [[nodiscard]] const std::vector<StepResult>& steps() const { return steps_; }

private:
    void record(std::string description, bool passed);

    std::string name_;
    dut::Device& device_;
    core::Logger logger_;
    std::vector<StepResult> steps_;
};

}  // namespace dsl
