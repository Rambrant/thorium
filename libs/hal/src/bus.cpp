#include "hal/bus.hpp"

namespace hal
{
    Bus::Bus() :
        mLogger( "hal::Bus")
    {}

    auto Bus::writeRegister( const std::uint32_t address, const std::uint32_t value ) -> void
    {
        mRegisters[address] = value;

        mLogger.log(core::LogLevel::Debug, "wrote register");
    }

    auto Bus::readRegister( const std::uint32_t address) const -> std::uint32_t
    {
        auto it = mRegisters.find(address);

        return it != mRegisters.end() ? it->second : 0;
    }
} // namespace hal
