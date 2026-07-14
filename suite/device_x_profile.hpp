#pragma once

#include "dut/matrix_map.hpp"

//
// Routing table for "Device X" on its standard adapter. Because there is one
// adapter type per device, this is a fixed compile-time table: each logical
// test point (named to match the test spec) maps to the abstract instrument
// used to measure it and the matrix crosspoint that routes that instrument
// to the point.
//
// This lives at the scripts/profile level because it pairs a specific device
// with the rig; the crosspoint coordinates themselves are the rig's fixed
// fabric (defined via core::Crosspoint / hal), and the instrument choices are
// abstract (dut::Instrument).
//
MATRIX( DeviceX_StdAdapter, "Device X on standard adapter")
    POINT( Port5Vdc,   dut::Instrument::Voltmeter,    3, 7, "5Vdc supply port")
    POINT( Port3V3,    dut::Instrument::Voltmeter,    3, 6, "3.3Vdc supply port")
    POINT( ClkProbe,   dut::Instrument::Oscilloscope, 1, 2, "Config clock probe")
END_MATRIX
