#pragma once

#include "core/measure.hpp"
#include "core/transfer.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

//
// The concrete instantiation of core::MeasureEngine for this rig -- see
// core/measure.hpp's own comment on FabricT/InstrumentWiringT/ConnectorWiringT.
// Every piece this needs is a hal-level rig fact: this header has no
// dependency, textual or otherwise, on anything named dut:: -- unlike
// before AdapterPointTag existed, there is no DUT-specific *type* for
// MeasureEngine to be instantiated with any more, only the point values
// themselves (e.g. dut::Output5V), which are passed at
// each Measure() call site, not baked into this alias.
//
using MeasureEngine = core::MeasureEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;

//
// The single point every script measures through:
//
//   Measure( Dmm1.voltage(), dut::Output5V);
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

//
// The other verb that observes something:
//
//   const auto reply = Read( Ser1.rs232());
//
// Declared here rather than in hal/apply.hpp beside Setup/Write, and defined in
// the same translation unit as Measure, because it is constructed *from*
// Measure's session bank -- one bank, so a run that measures a rail and reads a
// console reply produces one ordered recording covering both (see
// core::SessionBank on why that is a correctness requirement).
//
// Being constructed from another global is exactly the situation where
// initialisation order matters, and across translation units there is no order
// to rely on. Defining both in hal/measure.cpp is what makes the dependency
// well-defined rather than a link-order accident.
//
// Injection reaches the same bank, so a script unit test arms both seams
// through whichever verb it is asking about:
//
//   Measure.inject( "Output5V", 5.02_V);
//   Read.inject(    "Ser1.Data", { "ACK\r", "0xF5\r" });
//
// while useLive/load/startRecording/dump stay on Measure alone -- see
// core::ReadEngine's own comment on why they are deliberately not offered
// twice.
//
extern core::ReadEngine Read;
