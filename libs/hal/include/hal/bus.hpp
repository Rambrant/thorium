#pragma once

#include <cstdint>
#include <unordered_map>

#include "core/logger.hpp"

namespace hal {

// Abstracts the physical transport (e.g. I2C/SPI/serial) used to talk to
// real hardware. This in-memory implementation stands in for real hardware
// so the layers above can be developed and unit-tested without a physical
// device attached.
class Bus {
public:
    Bus();

    void write_register(std::uint32_t address, std::uint32_t value);
    [[nodiscard]] std::uint32_t read_register(std::uint32_t address) const;

private:
    core::Logger logger_;
    std::unordered_map<std::uint32_t, std::uint32_t> registers_;
};

}  // namespace hal
