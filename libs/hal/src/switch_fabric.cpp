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
        // The rendering is unchanged ("Matrix Matrix2 channel 14") even though
        // both halves of it now come from the device rather than from the
        // element: the kind via hal::kindOf (rig/devices.inc states it once
        // per card), the name via reflection over SwitchDeviceId, where it
        // used to be a string the element carried around.
        //
        return std::string( core::meta::to_string( kindOf( id.device))) + " " +
               std::string( to_string( id.device)) + " channel " + std::to_string( id.channel);
    }
} // namespace hal
