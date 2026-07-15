#pragma once

#include "core/measure.hpp"
#include "hal/adapter.hpp"
#include "hal/route_table.hpp"
#include "hal/switch_fabric.hpp"

//
// The concrete instantiation of core::MeasureEngine for this rig -- see
// core/measure.hpp's own comment on FabricT/RouteTableT/AdapterT. Every
// piece this needs -- the fabric, the route table, and the adapter type
// itself -- is a hal-level rig fact. The DUT-specific adapter *data*
// (DeviceX_StdAdapter) is only named where the one Measure object below is
// actually constructed -- see hal/measure.cpp -- via a plain #include of
// dut/device_x_profile.inc, the same "generic mechanism consumes a flat
// DUT data file" pattern THORIUM_ACTIVE_CRITERIA already uses for the
// criteria variants (see libs/dut/README.md). That is a textual, build-time
// file dependency only: this header has no C++-level dependency on
// anything named dut::.
//
using MeasureEngine = core::MeasureEngine<hal::SwitchFabric, hal::RouteTable, hal::Adapter>;

//
// The single point every script measures through:
//
//   Measure( Dmm1.voltage(), "5VOutput");
//
// Injection, recording, and playback are methods on this same object --
// Measure.inject(...), Measure.useLive(),
// Measure.startRecording()/stopRecording()/dump(...), Measure.load(...) --
// rather than a separate device/session parameter threaded through every
// call. Defined once in hal/measure.cpp, wired to this DUT's adapter data
// and this rig's fabric/RouteTable.
//
extern MeasureEngine Measure;
