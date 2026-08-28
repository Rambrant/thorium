#include "hal/fabric/switch_fabric.hpp"

#include "core/meta.hpp"

namespace hal
{
    auto to_string( const SwitchElementId id) -> std::string
    {
        //
        // core::meta::to_string() (core/meta.hpp) reflects over
        // SwitchDeviceKind's own enumerators -- what used to be a
        // hand-written switch here (see this file's own history) replacing
        // an even older two-kind ternary. Spdt and Spst arriving as a fourth
        // and fifth SwitchDeviceKind needed no update here at all, unlike
        // either of those -- which is the property that was being bought.
        //
        // The rendering is unchanged ("Matrix Matrix1 channel 300") even
        // though both halves of it now come from the device rather than from
        // the element: the kind via hal::kindOf (which reads it off the card
        // model rig/devices.inc names), the name via reflection over
        // SwitchDeviceId, where it used to be a string the element carried
        // around.
        //
        // The channel stays a plain number, deliberately, even for the cards
        // whose numbering is composite -- "channel 300" rather than "group 0
        // row 3 column 00". Rendering the parts needs the card's own scheme,
        // which hal::detail::SwitchCardSpec does carry (see hal::crosspoint),
        // and is worth doing the day a log reader has to chase one of these
        // back to a manual. It is not worth a second formatter per model
        // while nothing but tests reads these strings.
        //
        return std::string( core::meta::to_string( kindOf( id.device))) + " " +
               std::string( to_string( id.device)) + " channel " + std::to_string( id.channel);
    }
} // namespace hal
