#include "suite/rig.hpp"

#include "hal/route_table.hpp"
#include "hal/switch_fabric.hpp"
#include "suite/device_x_profile.hpp"

namespace rig
{
    namespace
    {
        auto fabric() -> hal::SwitchFabric &
        {
            static hal::SwitchFabric instance;
            return instance;
        }

        //
        // Flat, hand-authored wiring: which matrix/mux channels to close to
        // reach each adapter point's instrument. See hal/route_table.hpp for
        // why this is a runtime lookup rather than a compile-time one today.
        //
        auto routes() -> const hal::RouteTable &
        {
            static const hal::RouteTable table = []
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
            }();

            return table;
        }
    } // namespace

    auto dmm1() -> hal::Dmm &
    {
        static hal::Dmm instance{ hal::InstrumentId::Dmm1 };
        return instance;
    }

    auto dmm2() -> hal::Dmm &
    {
        static hal::Dmm instance{ hal::InstrumentId::Dmm2 };
        return instance;
    }

    auto osc1() -> hal::Oscilloscope &
    {
        static hal::Oscilloscope instance{ hal::InstrumentId::Osc1 };
        return instance;
    }

    auto powerSupply1() -> hal::PowerSupply &
    {
        static hal::PowerSupply instance{ hal::InstrumentId::PowerSupply1 };
        return instance;
    }

    auto testSession() -> core::SwitchableSession &
    {
        static core::LiveSession       live;
        static core::SwitchableSession instance{ live };
        return instance;
    }

    auto bench() -> dut::Bench &
    {
        static dut::Bench instance{ fabric(), routes(), DeviceX_StdAdapter, testSession() };
        return instance;
    }
} // namespace rig
