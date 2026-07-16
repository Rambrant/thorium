#pragma once

#include "hal/instrument.hpp"
#include "hal/n6701a.hpp"
#include "hal/ac6677a.hpp"
#include "hal/switch_fabric.hpp"

//
// The concrete instruments this bench has, addressed directly by name --
// no factory function, no lookup: Dmm1 IS the DMM plugged into the rig.
// Left unqualified (not hal::Dmm1) deliberately, mirroring why scripts
// themselves are global rather than namespaced (see suite/scripts.hpp) --
// these are names a test script writes directly, not hal-internal plumbing.
//
// DcP1..DcP4 are the N6701A mainframe's four module slots, each its own
// hal::N6701A instance -- see that class's own comment in hal/n6701a.hpp
// for why the channel number is a constructor argument here, and why that's
// a different fact from which InstrumentWiring/wiring.inc entry they get.
//
inline hal::Dmm          Dmm1{ hal::InstrumentId::Dmm1 };
inline hal::Dmm          Dmm2{ hal::InstrumentId::Dmm2 };
inline hal::Oscilloscope Osc1{ hal::InstrumentId::Osc1 };
inline hal::N6701A       DcP1{ hal::InstrumentId::DcP1, 1 };
inline hal::N6701A       DcP2{ hal::InstrumentId::DcP2, 2 };
inline hal::N6701A       DcP3{ hal::InstrumentId::DcP3, 3 };
inline hal::N6701A       DcP4{ hal::InstrumentId::DcP4, 4 };
inline hal::Ac6677A      AcP1{ hal::InstrumentId::AcP1 };

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
