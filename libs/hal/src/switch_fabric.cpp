#include "hal/switch_fabric.hpp"

namespace hal
{
    auto to_string( const SwitchElementId id) -> std::string
    {
        return std::string( id.kind == SwitchDeviceKind::Matrix ? "Matrix " : "Mux ") +
               std::string( id.device) + " channel " + std::to_string( id.channel);
    }
} // namespace hal
