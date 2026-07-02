#include "dsl/test_case.hpp"

namespace dsl {

TestCase::TestCase(std::string name, dut::Device& device)
    : name_(std::move(name)), device_(device), logger_("dsl::TestCase") {}

TestCase& TestCase::power_on() {
    device_.power_on();
    record("power_on", true);
    return *this;
}

TestCase& TestCase::power_off() {
    device_.power_off();
    record("power_off", true);
    return *this;
}

TestCase& TestCase::expect_powered_on() {
    record("expect_powered_on", device_.is_powered_on());
    return *this;
}

TestCase& TestCase::expect_powered_off() {
    record("expect_powered_off", !device_.is_powered_on());
    return *this;
}

TestCase& TestCase::set_value(std::uint32_t value) {
    device_.set_value(value);
    record("set_value", true);
    return *this;
}

TestCase& TestCase::expect_value(std::uint32_t expected) {
    record("expect_value", device_.get_value() == expected);
    return *this;
}

void TestCase::record(std::string description, bool passed) {
    steps_.push_back(StepResult{std::move(description), passed});
}

bool TestCase::run() {
    bool all_passed = true;
    for (const auto& step : steps_) {
        const auto level = step.passed ? core::LogLevel::Info : core::LogLevel::Error;
        logger_.log(level, name_ + " :: " + step.description +
                                (step.passed ? " [PASS]" : " [FAIL]"));
        all_passed = all_passed && step.passed;
    }
    return all_passed;
}

}  // namespace dsl
