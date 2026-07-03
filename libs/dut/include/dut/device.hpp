#pragma once

#include <cstdint>

#include "core/quantity.hpp"
#include "hal/bus.hpp"

namespace dut {

// Models the device under test in terms of the hal::Bus primitives.
// This is where register maps / memory layout for the actual chip
// or board would be defined.
class Device {
public:
    // Public so test code can poke these registers directly via the bus
    // to simulate fused/measured values (there's no domain-level "write"
    // operation for a fuse or a measurement -- those come from hardware).
    static constexpr std::uint32_t kFuseRegister = 0x0008;
    static constexpr std::uint32_t kVoltageRegister = 0x000C;  // millivolts, as an integer

    explicit Device(hal::Bus& bus);

    void power_on();
    void power_off();
    [[nodiscard]] bool is_powered_on() const;

    void set_value(std::uint32_t value);
    [[nodiscard]] std::uint32_t get_value() const;

    [[nodiscard]] std::uint32_t read_fuse_register() const;
    [[nodiscard]] core::Voltage measure_output_voltage() const;

private:
    static constexpr std::uint32_t kPowerRegister = 0x0000;
    static constexpr std::uint32_t kValueRegister = 0x0004;

    hal::Bus& bus_;
};

}  // namespace dut

