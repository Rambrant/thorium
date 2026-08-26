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

    //
    // The same reflection over the model enum, and worth keeping distinct
    // from hal::partOf(): this is the enumerator's own spelling
    // ("Racal1260_45"), that is the part number the datasheet uses ("Racal
    // Instruments 1260-45A"). Neither can be derived from the other -- a
    // C++ identifier cannot hold a hyphen or a space, and a part number is
    // not this codebase's to choose -- which is exactly why the spec table
    // carries the second one as data.
    //
    auto to_string( const SwitchDeviceModel model) -> std::string_view
    {
        return core::meta::to_string( model);
    }
} // namespace hal
