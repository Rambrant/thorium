#pragma once

#include "dut/adapter.hpp"

//
// Routing table for "Device X" on its standard adapter. Because there is one
// adapter type per device (the connector on the device doesn't change), this
// is a fixed table: each logical point (named to match the test spec) maps
// to where it lands on the VPC90 connector array and what kind of quantity
// is expected there. Which instrument actually measures it, and by which
// matrix/mux path, is a separate rig-level fact -- see hal::RouteTable --
// not something this adapter table knows about.
//
inline const dut::Adapter DeviceX_StdAdapter{
    "DeviceX_StdAdapter", "Device X on standard adapter",
    {
        dut::AdapterPoint{ "5VOutput",  hal::VpcLocation{ hal::VpcRack::A, 1, 3 }, core::QuantityKind::Voltage, "5Vdc supply port" },
        dut::AdapterPoint{ "3V3Output", hal::VpcLocation{ hal::VpcRack::A, 1, 6 }, core::QuantityKind::Voltage, "3.3Vdc supply port" },
        dut::AdapterPoint{ "ClkProbe",  hal::VpcLocation{ hal::VpcRack::A, 1, 2 }, core::QuantityKind::Voltage, "Config clock probe" },
    }};
