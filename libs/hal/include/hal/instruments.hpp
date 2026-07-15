#pragma once

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"

//
// The concrete instruments this bench has, addressed directly by name --
// no factory function, no lookup: Dmm1 IS the DMM plugged into the rig.
// Left unqualified (not hal::Dmm1) deliberately, mirroring why scripts
// themselves are global rather than namespaced (see suite/scripts.hpp) --
// these are names a test script writes directly, not hal-internal plumbing.
//
inline hal::Dmm          Dmm1{ hal::InstrumentId::Dmm1 };
inline hal::Dmm          Dmm2{ hal::InstrumentId::Dmm2 };
inline hal::Oscilloscope Osc1{ hal::InstrumentId::Osc1 };
inline hal::PowerSupply  PowerSupply1{ hal::InstrumentId::PowerSupply1 };

namespace hal
{
    //
    // The switching fabric itself: rig-hardware state, not DUT-specific, so
    // it lives here rather than alongside whichever DUT/adapter happens to
    // be under test (see dut/device_x_profile.inc). Unlike Dmm1 etc above,
    // nothing outside of assembling the Measure object (see
    // hal/measure.cpp) needs to name this directly, so it stays properly
    // namespaced rather than joining the unqualified globals.
    //
    // The fixed wiring data itself (which channel each instrument/connector
    // pin is on) lives in hal/wiring.inc, not here -- see hal/wiring.hpp.
    //
    inline SwitchFabric fabric;
} // namespace hal
