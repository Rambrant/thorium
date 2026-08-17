#pragma once

#include "hal/instrument.hpp"
#include "hal/l4411a.hpp"
#include "hal/dso8064.hpp"
#include "hal/n6701a.hpp"
#include "hal/ac6834b.hpp"
#include "hal/switch_fabric.hpp"

//
// INSTRUMENTS / INSTRUMENT / END_INSTRUMENTS: declarative instrument list,
// mirroring INSTRUMENT_WIRING/WIRE_INSTRUMENT/END_INSTRUMENT_WIRING in
// hal/wiring.hpp -- see rig/instrument.inc. Unlike wiring's table (a
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
//       INSTRUMENT( L4411A, Dmm1)
//       INSTRUMENT( N6701A, DcP1, 1)
//   END_INSTRUMENTS
//
// INSTRUMENT takes the global's name and its InstrumentId as one token
// (id), not two -- there is no rig where a script-facing global is named
// anything other than the identity it addresses, so a separate name
// parameter would just be the same word written twice at every call site
// (see hal/instrument.hpp's own comment on the same token feeding its
// enum's enumerators from this exact file).
//
// This file, together with instrument.inc/wiring.inc right alongside it,
// is this rig's entire contribution to what would otherwise be a generic
// hal:: library with no instruments plugged into it at all -- see
// rig/README.md and hal/README.md for the split this directory boundary is
// drawing.
//
#define INSTRUMENTS

#define INSTRUMENT( type, id, ...) \
    inline hal::type id{ hal::InstrumentId::id __VA_OPT__(,) __VA_ARGS__ };

#define END_INSTRUMENTS

// This rig's actual instruments -- see rig/instrument.inc's own comment.
#include THORIUM_INSTRUMENT_TABLE

namespace hal
{
    //
    // The switching fabric itself: rig-hardware state, not DUT-specific, so
    // it lives here rather than alongside whichever DUT/adapter happens to
    // be under test (see dut/adapter.inc). Unlike Dmm1 etc above,
    // nothing outside of assembling the Measure object (see
    // hal/measure.cpp) needs to name this directly, so it stays properly
    // namespaced rather than joining the unqualified globals.
    //
    // The fixed wiring data itself (which channel each instrument/connector
    // pin is on) lives in rig/wiring.inc, not here -- see hal/wiring.hpp.
    //
    inline SwitchFabric fabric;
} // namespace hal
