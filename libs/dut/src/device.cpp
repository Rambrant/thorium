#include "dut/device.hpp"

namespace dut
{
    Device::Device( hal::Bus & bus ) : mBus(bus) {}

    void Device::powerOn()
    {
        mBus.write_register(kPowerRegister, 1);
    }

    void Device::powerOff()
    {
        mBus.write_register(kPowerRegister, 0);
    }

    bool Device::isPoweredOn() const
    {
        return mBus.read_register(kPowerRegister) == 1;
    }

    void Device::setValue( std::uint32_t value )
    {
        mBus.write_register(kValueRegister, value);
    }

    std::uint32_t Device::getValue() const
    {
        return mBus.read_register(kValueRegister);
    }

    std::uint32_t Device::readFuseRegister() const
    {
        return mBus.read_register(kFuseRegister);
    }

    core::Voltage Device::measureOutputVoltage() const
    {
        const auto millivolts = mBus.read_register( kVoltageRegister);

        return core::Voltage{ static_cast<double>(millivolts) / 1000.0 };
    }
} // namespace dut
