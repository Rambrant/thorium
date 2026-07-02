#include "hal/bus.hpp"

namespace hal {

Bus::Bus() : logger_("hal::Bus") {}

void Bus::write_register(std::uint32_t address, std::uint32_t value) {
    registers_[address] = value;
    logger_.log(core::LogLevel::Debug, "wrote register");
}

std::uint32_t Bus::read_register(std::uint32_t address) const {
    auto it = registers_.find(address);
    return it != registers_.end() ? it->second : 0;
}

}  // namespace hal
