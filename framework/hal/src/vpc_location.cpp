#include "hal/vpc_location.hpp"

namespace hal
{
    auto to_string( const VpcLocation location) -> std::string
    {
        return std::string( location.rack == VpcRack::A ? "A" : "B") +
               std::to_string( location.connector) + "/pin" + std::to_string( location.pin);
    }
} // namespace hal
