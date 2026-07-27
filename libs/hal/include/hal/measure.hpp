#pragma once

#include "core/measure.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

//
// The concrete instantiation of core::MeasureEngine for this rig -- see
// core/measure.hpp's own comment on FabricT/InstrumentWiringT/ConnectorWiringT.
// Every piece this needs is a hal-level rig fact: this header has no
// dependency, textual or otherwise, on anything named dut:: -- unlike
// before AdapterPointTag existed, there is no DUT-specific *type* for
// MeasureEngine to be instantiated with any more, only the point values
// themselves (e.g. dut::DeviceX_StdAdapter::Output5V), which are passed at
// each Measure() call site, not baked into this alias.
//
using MeasureEngine = core::MeasureEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;

//
// The single point every script measures through:
//
//   Measure( Dmm1.voltage(), DeviceX_StdAdapter::Output5V);
//
// Injection, recording, and playback are methods on this same object --
// Measure.inject(...), Measure.useLive(),
// Measure.startRecording()/stopRecording()/dump(...), Measure.load(...) --
// rather than a separate device/session parameter threaded through every
// call. Defined once in hal/measure.cpp, wired to the linking rig's fabric
// and its two static wiring tables (see that rig's wiring.inc --
// rig/wiring.inc in this repo).
//
extern MeasureEngine Measure;
