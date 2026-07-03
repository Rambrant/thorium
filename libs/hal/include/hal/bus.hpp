#pragma once

#include <cstdint>
#include <unordered_map>

#include "core/logger.hpp"

namespace hal
{
    // Abstracts the physical transport (e.g. I2C/SPI/serial) used to talk to
    // real hardware. This in-memory implementation stands in for real hardware
    // so the layers above can be developed and unit-tested without a physical
    // device attached.
    class Bus
    {
        public:
            Bus();

            auto writeRegister( std::uint32_t address, std::uint32_t value ) -> void;

            [[nodiscard]]
            auto readRegister( std::uint32_t address ) const -> std::uint32_t;

        private:
            core::Logger                                      mLogger;
            std::unordered_map< std::uint32_t, std::uint32_t> mRegisters;
    };
} // namespace hal
