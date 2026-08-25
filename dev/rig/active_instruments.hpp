#pragma once

//
// The dev bench's instrument globals -- the sibling of
// rig/active_instruments.hpp, and read by hal through
// THORIUM_ACTIVE_INSTRUMENTS in exactly the same way. See that file's own
// comment for the INSTRUMENTS/INSTRUMENT/END_INSTRUMENTS mechanism and why
// this list and hal::InstrumentId's enumerators cannot desync.
//
// One driver header, not five. That is the part worth noticing: this file is
// what pulls drivers into a build, so a deployment that names one instrument
// compiles against one driver. hal_rig links whatever instruments/ registered,
// but a driver no INSTRUMENT() row names contributes no global and no object
// code -- so a dev build is also the fastest way to work on one driver without
// rebuilding the other four.
//
#include "hal/address.hpp"
#include "hal/instrument.hpp"
#include "hal/l4411a.hpp"
#include "hal/switch_fabric.hpp"

#define INSTRUMENTS
#define INSTRUMENT( type, id, address, ...) \
    inline hal::type id{ hal::InstrumentId::id, hal::address __VA_OPT__(,) __VA_ARGS__ };
#define END_INSTRUMENTS

#include THORIUM_INSTRUMENT_TABLE

namespace hal
{
    //
    // Declared even though this bench has no switching hardware for it to
    // track, and qualified for the same reason the bench rig's is: nothing but
    // the measure/apply assembly names it, and those are compiled once against
    // whichever rig is selected. An empty fabric is a working fabric -- it has
    // no elements, so every close() and open() it is asked for is one nobody
    // ever asks -- and leaving it out would make hal/src/route.cpp and
    // hal/src/measure.cpp fail to compile for this deployment alone.
    //
    inline SwitchFabric fabric;
} // namespace hal
