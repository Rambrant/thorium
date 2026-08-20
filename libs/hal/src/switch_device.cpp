#include "hal/switch_device.hpp"

#include "core/meta.hpp"

namespace hal
{
    //
    // Reflects over SwitchDeviceId's own enumerators rather than reading
    // devices.inc a third time with SWITCH_DEVICE redefined to build a switch
    // case -- the same replacement hal::to_string(InstrumentId) got (see
    // hal/src/instrument.cpp), and available for the same reason: the enum
    // already exists by the time this runs, and an enumerator's name is
    // exactly what reflection can recover. Contrast the second read in
    // hal/switch_device.hpp, which carries kind and address and so has
    // nothing to reflect out of.
    //
    auto to_string( const SwitchDeviceId device) -> std::string_view
    {
        return core::meta::to_string( device);
    }
} // namespace hal
