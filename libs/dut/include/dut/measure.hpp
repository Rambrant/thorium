#pragma once

#include "core/measure.hpp"
#include "dut/adapter.hpp"
#include "hal/route_table.hpp"
#include "hal/switch_fabric.hpp"

//
// The concrete instantiation of core::MeasureEngine for this rig -- see
// core/measure.hpp's own comment on FabricT/RouteTableT/AdapterT. This
// lives in dut rather than hal because it's the layer that knows which
// Adapter (this DUT's concrete pin map -- see dut/device_x_profile.hpp) is
// being measured; the fabric/route wiring themselves are pure hal facts
// (see hal/instruments.hpp) with no DUT knowledge at all.
//
using MeasureEngine = core::MeasureEngine<hal::SwitchFabric, hal::RouteTable, dut::Adapter>;

//
// The single point every script measures through:
//
//   Measure( Dmm1.voltage(), "5VOutput");
//
// Injection, recording, and playback are methods on this same object --
// Measure.inject(...), Measure.useLive(),
// Measure.startRecording()/stopRecording()/dump(...), Measure.load(...) --
// rather than a separate device/session parameter threaded through every
// call. Defined once in dut/measure.cpp, wired to this DUT's Adapter and
// this rig's fabric/RouteTable.
//
extern MeasureEngine Measure;
