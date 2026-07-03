#include "dsl/test_case.hpp"

namespace dsl
{
    TestCase::TestCase( std::string name, dut::Device & device)
        : mName(std::move(name)),
          mDevice(device),
          mLogger("dsl::TestCase") {}

    TestCase & TestCase::powerOn()
    {
        mDevice.power_on();
        record("power_on", true);

        return *this;
    }

    TestCase & TestCase::powerOff()
    {
        mDevice.power_off();
        record("power_off", true);

        return *this;
    }

    TestCase & TestCase::expectPoweredOn()
    {
        record("expectPoweredOn", mDevice.is_powered_on());

        return *this;
    }

    TestCase & TestCase::expectPoweredOff()
    {
        record("expectPoweredOff", ! mDevice.is_powered_on());

        return *this;
    }

    TestCase & TestCase::setValue( std::uint32_t value)
    {
        mDevice.set_value(value);
        record("setValue", true);

        return *this;
    }

    TestCase & TestCase::expectValue( std::uint32_t expected)
    {
        record("expectValue", mDevice.get_value() == expected);

        return *this;
    }

    void TestCase::record( std::string description, bool passed)
    {
        mSteps.push_back( StepResult{ std::move(description), passed });
    }

    bool TestCase::run()
    {
        bool all_passed = true;

        for( const auto & step : mSteps)
        {
            const auto level = step.passed ? core::LogLevel::Info : core::LogLevel::Error;

            mLogger.log(level, mName + " :: " + step.description +
                               (step.passed ? " [PASS]" : " [FAIL]"));

            all_passed = all_passed && step.passed;
        }

        return all_passed;
    }
} // namespace dsl
