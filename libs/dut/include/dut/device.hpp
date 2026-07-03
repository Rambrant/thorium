#pragma once

#include <cstdint>

#include "core/quantity.hpp"
#include "hal/bus.hpp"

namespace dut
{
    //
    // Models the device under test in terms of the hal::Bus primitives.
    // This is where register maps / memory layout for the actual chip
    // or board would be defined.
    //
    class Device
    {
        public:
            //
            // Public so test code can poke these registers directly via the bus
            // to simulate fused/measured values (there's no domain-level "write"
            // operation for a fuse or a measurement -- those come from hardware).
            //
            static constexpr std::uint32_t kFuseRegister    = 0x0008;
            static constexpr std::uint32_t kVoltageRegister = 0x000C; // millivolts, as an integer

            explicit Device( hal::Bus & bus );

            void powerOn();

            void powerOff();

            [[nodiscard]]
            auto isPoweredOn() const -> bool;

            auto setValue( std::uint32_t value ) -> void;

            [[nodiscard]]
            auto getValue() const -> std::uint32_t;

            [[nodiscard]]
            auto readFuseRegister() const -> std::uint32_t;

            [[nodiscard]]
            auto measureOutputVoltage() const -> core::Voltage;

        private:
            static constexpr std::uint32_t kPowerRegister = 0x0000;
            static constexpr std::uint32_t kValueRegister = 0x0004;

            hal::Bus & mBus;
    };
} // namespace dut
