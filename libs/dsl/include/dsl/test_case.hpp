#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <core/logger.hpp>
#include "dut/device.hpp"

namespace dsl
{
    // Result of a single step within a TestCase.
    struct StepResult
    {
        std::string description;
        bool        passed;
    };

    //
    // A small fluent DSL for writing test scripts against a dut::Device.
    // Each method appends a StepResult and returns *this so calls can be chained:
    //
    //   dsl::TestCase("power cycles correctly", device)
    //       .powerOn()
    //       .expectPoweredOn()
    //       .powerOff()
    //       .expectPoweredOff()
    //       .run();
    //
    // This is the layer test *scripts* (in scripts/) are written against —
    // scripts should not need to touch hal:: or dut:: directly.
    //
    class TestCase
    {
        public:
            TestCase( std::string name, dut::Device & device);

            TestCase & powerOn();

            TestCase & powerOff();

            TestCase & expectPoweredOn();

            TestCase & expectPoweredOff();

            TestCase & setValue( std::uint32_t value);

            TestCase & expectValue( std::uint32_t expected);

            // Finalizes the test case: logs a summary and returns true only if
            // every step passed.
            bool run();

            [[nodiscard]] const std::vector< StepResult> & steps() const { return mSteps; }

        private:
            void record( std::string description, bool passed );

            std::string             mName;
            dut::Device &           mDevice;
            core::Logger            mLogger;
            std::vector<StepResult> mSteps;
    };
} // namespace dsl
