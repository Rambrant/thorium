#include "dut/device.hpp"

namespace dut {

Device::Device(hal::Bus& bus) : bus_(bus) {}

void Device::power_on() {
    bus_.write_register(kPowerRegister, 1);
}

void Device::power_off() {
    bus_.write_register(kPowerRegister, 0);
}

bool Device::is_powered_on() const {
    return bus_.read_register(kPowerRegister) == 1;
}

void Device::set_value(std::uint32_t value) {
    bus_.write_register(kValueRegister, value);
}

std::uint32_t Device::get_value() const {
    return bus_.read_register(kValueRegister);
}

}  // namespace dut
