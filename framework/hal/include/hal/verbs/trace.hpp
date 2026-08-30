#pragma once

#include "core/verbs/trace.hpp"

//
// The trace verb, as this rig spells it:
//
//   Arm(   Osc1.single().timeout( 2_s));
//   Remove( AcP1.ac());
//   const auto captured = Await( Osc1.single());
//
//   const auto trace = Fetch( Osc1.channel<3>().waveform());
//
// Takes no at(...) point and does not touch the fabric, exactly like Arm and
// Await (see hal/verbs/acquire.hpp): the record is already inside the instrument by
// the time this is called, and whatever route the signal arrived on was closed
// by a Connect or is being closed and reopened around each reading by Measure.
//
// No template argument, for the same reason neither acquisition verb takes one
// -- there is no FabricT, InstrumentWiringT or ConnectorWiringT for it to be
// instantiated with.
//
using FetchEngine = core::FetchEngine;

//
// Defined in hal/src/verbs/measure.cpp rather than in a translation unit of its own, and
// that is a requirement rather than a filing choice: it is constructed from
// core::MeasureEngine's session bank -- one run, one ordered recording covering
// every seam -- and across translation units there is no initialisation order
// to rely on. See hal/verbs/measure.hpp, which says this about Read at more length.
//
extern FetchEngine Fetch;
