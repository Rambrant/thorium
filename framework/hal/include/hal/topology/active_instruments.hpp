#pragma once

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"

//
// The mechanism behind rig/instrument.inc: the macros that turn that table
// into this rig's actual instrument globals, plus the switching fabric they
// route through. The table itself is the deployment's (see
// THORIUM_INSTRUMENT_TABLE); everything in this file is the same for every
// rig that ever links hal, which is why it lives here rather than in rig/.
//
// It sits in topology/ next to wiring.hpp and adapter.hpp for that reason:
// this directory is where the mechanism behind each of the deployment's
// declarative tables lives -- wiring.hpp reads rig/wiring.inc, adapter.hpp
// reads dut/adapter.inc, and this one reads rig/instrument.inc.
// core/criteria/active_criteria.hpp is the same shape one layer down, and the
// name is borrowed from it deliberately.
//
// Unlike topology/'s other three, this header is hal_rig's, not hal's: it
// reaches the concrete driver headers (below), which are compiled *after*
// hal. See framework/hal/CMakeLists.txt's own comment on that split.
//

//
// The driver headers this deployment's INSTRUMENT rows name -- one #include
// per distinct driver, generated at configure time from the table's own type
// column (see cmake/InstrumentDrivers.cmake). Nobody writes this list: the
// table already says which driver each row is, and the qualifier that says it
// (keysight_edu36311a in keysight_edu36311a::RelayOutput2) is the driver's package,
// header and namespace name all at once.
//
// A generated file rather than the top of instrument.inc, because the
// preprocessor cannot get there from here twice over. A macro expansion
// cannot emit an #include directive at all -- so the table could not produce
// these itself however it were written -- and instrument.inc is read a second
// time by hal/driver/instrument.hpp, to generate hal::InstrumentId, inside
// plain hal that every driver header depends on. Driver includes in the table
// would mean the enum's own definition pulling in the drivers that need it;
// the hal/hal_rig split exists to make that impossible.
//
#include THORIUM_INSTRUMENT_DRIVERS

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
// enum's enumerators from that same table).
//
// address is a fixed column rather than one more of the trailing
// constructor arguments, because it is the one thing every instrument has
// -- an instrument the PC cannot reach is not an instrument a rig has --
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
// hal::keysight_edu36311a::EDU36311A's mainframe slot is the only such argument on
// this repo's bench rig. That slot is deliberately NOT folded into the
// address, even though "GPIB 14, slot 3" is how you would say aloud where
// DcP3 is: one address is one box on the bus (DcP1..DcP4 share a mainframe
// and so share an address), the slot picks an endpoint inside it, and an
// optional slot field on a generic address type could be omitted on an
// EDU36311A or supplied to a DMM without anything objecting. A constructor
// parameter the driver itself declares cannot be either.
//
#define INSTRUMENTS

#define INSTRUMENT( type, id, address, ...) \
    inline hal::type id{ hal::InstrumentId::id, hal::address __VA_OPT__(,) __VA_ARGS__ };

#define END_INSTRUMENTS

// The linking rig's actual instruments -- see rig/instrument.inc's own comment.
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
    // Declared unconditionally, including for a deployment with no switching
    // hardware at all (dev/rig/): an empty fabric is a working fabric -- it
    // has no elements, so every close() and open() it is asked for is one
    // nobody ever asks -- and making it conditional would leave
    // hal/src/verbs/route.cpp and hal/src/verbs/measure.cpp failing to
    // compile for exactly those deployments.
    //
    // The fixed wiring data itself (which channel each instrument/connector
    // pin is on) lives in rig/wiring.inc, not here -- see hal/topology/wiring.hpp.
    //
    inline SwitchFabric fabric;
} // namespace hal
