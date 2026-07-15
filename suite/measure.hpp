#pragma once

#include "core/measure.hpp"
#include "dut/adapter.hpp"
#include "hal/instrument.hpp"
#include "hal/route_table.hpp"
#include "hal/switch_fabric.hpp"

//
// The concrete instruments this bench has, addressed directly by name --
// no factory function, no lookup: Dmm1 IS the DMM plugged into the rig.
//
inline hal::Dmm          Dmm1{ hal::InstrumentId::Dmm1 };
inline hal::Dmm          Dmm2{ hal::InstrumentId::Dmm2 };
inline hal::Oscilloscope Osc1{ hal::InstrumentId::Osc1 };
inline hal::PowerSupply  PowerSupply1{ hal::InstrumentId::PowerSupply1 };

//
// The concrete instantiation of core::MeasureEngine for this rig -- the
// "build stage" injection point core/measure.hpp's own comment refers to:
// core::MeasureEngine itself has no idea hal::SwitchFabric, hal::RouteTable,
// or dut::Adapter exist, only that whatever it's given satisfies the shape
// it needs. Naming that combination here, rather than baking a fixed set of
// types into core::MeasureEngine, is what keeps core/measure.hpp reusable
// as standalone, rig-agnostic machinery.
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
// call. Defined once in suite/measure.cpp, wired to this DUT's Adapter and
// this rig's fabric/RouteTable.
//
extern MeasureEngine Measure;
