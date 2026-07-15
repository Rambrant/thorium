#pragma once

#include "hal/instrument.hpp"
#include "hal/route_table.hpp"
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
    // The switching fabric and its fixed wiring table: rig-hardware facts,
    // not DUT-specific, so they live here rather than alongside whichever
    // DUT/adapter happens to be under test (see dut/device_x_profile.hpp).
    // Unlike Dmm1 etc above, nothing outside of assembling the Measure
    // object (see dut/measure.cpp) needs to name these directly, so they
    // stay properly namespaced rather than joining the unqualified globals.
    //
    inline SwitchFabric fabric;

    //
    // Flat, hand-authored wiring: which matrix/mux channels to close to
    // reach each adapter point's instrument. See hal/route_table.hpp for
    // why this is a runtime lookup rather than a compile-time one today.
    //
    inline const RouteTable routes = []
    {
        RouteTable t;

        t.addRoute(
            VpcLocation{ VpcRack::A, 1, 3 }, InstrumentId::Dmm1, core::QuantityKind::Voltage,
            { SwitchElementId{ SwitchDeviceKind::Matrix, "Matrix2", 14 },
              SwitchElementId{ SwitchDeviceKind::Mux,    "Mux1",     3 } });

        t.addRoute(
            VpcLocation{ VpcRack::A, 1, 6 }, InstrumentId::Dmm1, core::QuantityKind::Voltage,
            { SwitchElementId{ SwitchDeviceKind::Matrix, "Matrix2", 15 },
              SwitchElementId{ SwitchDeviceKind::Mux,    "Mux1",     4 } });

        t.addRoute(
            VpcLocation{ VpcRack::A, 1, 2 }, InstrumentId::Osc1, core::QuantityKind::Voltage,
            { SwitchElementId{ SwitchDeviceKind::Matrix, "Matrix2", 10 },
              SwitchElementId{ SwitchDeviceKind::Mux,    "Mux2",     1 } });

        t.addRoute(
            VpcLocation{ VpcRack::A, 1, 4 }, InstrumentId::Dmm2, core::QuantityKind::Voltage,
            { SwitchElementId{ SwitchDeviceKind::Matrix, "Matrix2", 16 },
              SwitchElementId{ SwitchDeviceKind::Mux,    "Mux1",     5 } });

        return t;
    }();
} // namespace hal
