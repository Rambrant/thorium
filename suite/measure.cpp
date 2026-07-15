#include "suite/measure.hpp"

#include "suite/device_x_profile.hpp"

namespace
{
    hal::SwitchFabric fabric;

    //
    // Flat, hand-authored wiring: which matrix/mux channels to close to
    // reach each adapter point's instrument. See hal/route_table.hpp for
    // why this is a runtime lookup rather than a compile-time one today.
    //
    auto buildRoutes() -> hal::RouteTable
    {
        hal::RouteTable t;

        t.addRoute(
            hal::VpcLocation{ hal::VpcRack::A, 1, 3 }, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage,
            { hal::SwitchElementId{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 },
              hal::SwitchElementId{ hal::SwitchDeviceKind::Mux,    "Mux1",     3 } });

        t.addRoute(
            hal::VpcLocation{ hal::VpcRack::A, 1, 6 }, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage,
            { hal::SwitchElementId{ hal::SwitchDeviceKind::Matrix, "Matrix2", 15 },
              hal::SwitchElementId{ hal::SwitchDeviceKind::Mux,    "Mux1",     4 } });

        t.addRoute(
            hal::VpcLocation{ hal::VpcRack::A, 1, 2 }, hal::InstrumentId::Osc1, core::QuantityKind::Voltage,
            { hal::SwitchElementId{ hal::SwitchDeviceKind::Matrix, "Matrix2", 10 },
              hal::SwitchElementId{ hal::SwitchDeviceKind::Mux,    "Mux2",     1 } });

        t.addRoute(
            hal::VpcLocation{ hal::VpcRack::A, 1, 4 }, hal::InstrumentId::Dmm2, core::QuantityKind::Voltage,
            { hal::SwitchElementId{ hal::SwitchDeviceKind::Matrix, "Matrix2", 16 },
              hal::SwitchElementId{ hal::SwitchDeviceKind::Mux,    "Mux1",     5 } });

        return t;
    }

    const hal::RouteTable routes = buildRoutes();
} // namespace

MeasureEngine Measure{ fabric, routes, DeviceX_StdAdapter };
