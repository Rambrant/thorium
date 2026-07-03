#include "dut/device.hpp"

namespace dut
{
    Device::Device( hal::Bus & bus ) : mBus(bus) {}

    void Device::powerOn()
    {
        mBus.writeRegister(kPowerRegister, 1);
    }

    void Device::powerOff()
    {
        mBus.writeRegister(kPowerRegister, 0);
    }

    bool Device::isPoweredOn() const
    {
        return mBus.readRegister(kPowerRegister) == 1;
    }

    void Device::setValue( std::uint32_t value )
    {
        mBus.writeRegister(kValueRegister, value);
    }

    std::uint32_t Device::getValue() const
    {
        return mBus.readRegister(kValueRegister);
    }

    std::uint32_t Device::readFuseRegister() const
    {
        return mBus.readRegister(kFuseRegister);
    }

    core::Voltage Device::measureOutputVoltage() const
    {
        const auto millivolts = mBus.readRegister( kVoltageRegister);

        return core::Voltage{ static_cast<double>(millivolts) / 1000.0 };
    }
} // namespace dut
