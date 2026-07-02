#pragma once

#include <cstdint>

#include "hal/bus.hpp"

namespace dut {

// Models the device under test in terms of the hal::Bus primitives.
// This is where register maps / memory layout for the actual chip
// or board would be defined.
class Device {
public:
    explicit Device(hal::Bus& bus);

    void power_on();
    void power_off();
    [[nodiscard]] bool is_powered_on() const;

    void set_value(std::uint32_t value);
    [[nodiscard]] std::uint32_t get_value() const;

private:
    static constexpr std::uint32_t kPowerRegister = 0x0000;
    static constexpr std::uint32_t kValueRegister = 0x0004;

    hal::Bus& bus_;
};

}  // namespace dut
