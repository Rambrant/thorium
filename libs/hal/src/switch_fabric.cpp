#include "hal/switch_fabric.hpp"

#include "core/meta.hpp"

namespace hal
{
    auto to_string( const SwitchElementId id) -> std::string
    {
        //
        // core::meta::to_string() (core/meta.hpp) reflects over
        // SwitchDeviceKind's own enumerators -- what used to be a
        // hand-written switch here (see this file's own history) replacing
        // an even older two-kind ternary. Adding a fourth SwitchDeviceKind
        // needs no matching update here at all now, unlike either of those.
        //
        return std::string( core::meta::to_string( id.kind)) + " " +
               std::string( id.device) + " channel " + std::to_string( id.channel);
    }
} // namespace hal
