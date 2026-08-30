#pragma once

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/l4411a.hpp"
#include "hal/dso8064a.hpp"
#include "hal/n6701a.hpp"
#include "hal/ac6834b.hpp"
#include "hal/racal1260.hpp"
#include "hal/fabric/switch_fabric.hpp"

//
// INSTRUMENTS / INSTRUMENT / END_INSTRUMENTS: declarative instrument list,
// mirroring INSTRUMENT_WIRING/WIRE_INSTRUMENT/END_INSTRUMENT_WIRING in
// hal/topology/wiring.hpp -- see rig/instrument.inc. Unlike wiring's table (a
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
//       INSTRUMENT( L4411A, Dmm1, Lan( "bench-dmm1"))
//       INSTRUMENT( N6701A, DcP1, Gpib( 0, 14), 1)
//   END_INSTRUMENTS
//
// INSTRUMENT takes the global's name and its InstrumentId as one token
// (id), not two -- there is no rig where a script-facing global is named
// anything other than the identity it addresses, so a separate name
// parameter would just be the same word written twice at every call site
// (see hal/driver/instrument.hpp's own comment on the same token feeding its
// enum's enumerators from this exact file).
//
// address is a fixed column rather than one more of the trailing
// constructor arguments, because it is the one thing every instrument has
// -- an instrument the PC cannot reach is not an instrument this rig has --
// and a mandatory parameter is what makes leaving it out a compile error
// rather than a default nobody notices, the same choice hal::safeRig makes
// by calling safe() unconditionally instead of only where a driver offers
// it. It is written unqualified in the table (Gpib( 0, 14), not
// hal::Gpib( 0, 14)) and qualified here, exactly as type is, so the table
// stays free of namespace noise. Parentheses rather than braces: the
// preprocessor splits macro arguments on top-level commas and does not
// treat braces as grouping, so Gpib{ 0, 14 } would arrive here as two
// arguments -- C++20's parenthesized aggregate initialization is what lets
// an address with more than one field be one macro argument at all.
//
// Anything after address is handed to the driver's constructor untouched --
// hal::N6701A's mainframe slot is the only such argument on this rig. That
// slot is deliberately NOT folded into the address, even though "GPIB 14,
// slot 3" is how you would say aloud where DcP3 is: one address is one box
// on the bus (DcP1..DcP4 share a mainframe and so share an address), the
// slot picks an endpoint inside it, and an optional slot field on a generic
// address type could be omitted on an N6701A or supplied to a DMM without
// anything objecting. A constructor parameter the driver itself declares
// cannot be either.
//
// This file, together with instrument.inc/wiring.inc right alongside it,
// is this rig's entire contribution to what would otherwise be a generic
// hal:: library with no instruments plugged into it at all -- see
// rig/README.md and hal/README.md for the split this directory boundary is
// drawing.
//
#define INSTRUMENTS

#define INSTRUMENT( type, id, address, ...) \
    inline hal::type id{ hal::InstrumentId::id, hal::address __VA_OPT__(,) __VA_ARGS__ };

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
    // hal/src/verbs/measure.cpp) needs to name this directly, so it stays
    // properly namespaced rather than joining the unqualified globals.
    //
    // The fixed wiring data itself (which channel each instrument/connector
    // pin is on) lives in rig/wiring.inc, not here -- see hal/topology/wiring.hpp.
    //
    inline SwitchFabric fabric;
} // namespace hal
