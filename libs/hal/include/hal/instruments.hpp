#pragma once

#include "hal/instrument.hpp"
#include "hal/n6701a.hpp"
#include "hal/ac6677a.hpp"
#include "hal/switch_fabric.hpp"

//
// INSTRUMENTS / INSTRUMENT / END_INSTRUMENTS: declarative instrument list,
// mirroring INSTRUMENT_WIRING/WIRE_INSTRUMENT/END_INSTRUMENT_WIRING in
// hal/wiring.hpp -- see hal/instrument.inc. Unlike wiring's table (a
// single InstrumentWiring object accumulated one addWire() at a time),
// each INSTRUMENT here declares its own independent global -- Dmm1 IS the
// DMM plugged into the rig, addressed directly by name, no factory
// function, no lookup -- so INSTRUMENTS/END_INSTRUMENTS don't build
// anything; they're just the opening/closing bracket every other
// declarative table in this codebase uses (CRITERIA/ADAPTER/GROUP), kept
// here for the same Excel-like readability. Left unqualified (not
// hal::Dmm1) deliberately, mirroring why scripts themselves are global
// rather than namespaced (see suite/scripts.hpp) -- these are names a test
// script writes directly, not hal-internal plumbing.
//
//   INSTRUMENTS
//       INSTRUMENT( Dmm, Dmm1, Dmm1)
//       INSTRUMENT( N6701A, DcP1, DcP1, 1)
//   END_INSTRUMENTS
//
#define INSTRUMENTS

#define INSTRUMENT( type, name, id, ...) \
    inline hal::type name{ hal::InstrumentId::id __VA_OPT__(,) __VA_ARGS__ };

#define END_INSTRUMENTS

// This rig's actual instruments -- see hal/instrument.inc's own comment.
#include "libs/hal/instrument.inc"

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
