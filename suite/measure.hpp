#pragma once

#include "dut/measure_engine.hpp"
#include "hal/instrument.hpp"

//
// The concrete instruments this bench has, addressed directly by name --
// no factory function, no lookup: Dmm1 IS the DMM plugged into the rig.
//
inline hal::Dmm          Dmm1{ hal::InstrumentId::Dmm1 };
inline hal::Dmm          Dmm2{ hal::InstrumentId::Dmm2 };
inline hal::Oscilloscope Osc1{ hal::InstrumentId::Osc1 };
inline hal::PowerSupply  PowerSupply1{ hal::InstrumentId::PowerSupply1 };

//
// The single point every script measures through:
//
//   Measure( Dmm1.voltage(), "5VOutput");
//
// Injection, recording, and playback are methods on this same object (see
// dut::MeasureEngine) -- Measure.inject(...), Measure.useLive(),
// Measure.startRecording()/stopRecording()/dump(...), Measure.load(...) --
// rather than a separate device/session parameter threaded through every
// call. Defined once in suite/measure.cpp, wired to this DUT's Adapter and
// this rig's fabric/RouteTable.
//
extern dut::MeasureEngine Measure;
