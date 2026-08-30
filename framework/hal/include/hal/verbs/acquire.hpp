#pragma once

#include "core/verbs/acquire.hpp"

//
// The triggered-acquisition verbs, as this rig spells them:
//
//   Arm(   Osc1.single().timeout( 2_s));   // returns once the scope is armed
//   ...whatever the capture is supposed to catch...
//   const auto captured = Await( Osc1.single());
//
// Neither takes an at(...) point and neither touches the fabric, exactly like
// Setup and Write (see hal/verbs/source.hpp): arming a capture and asking whether it
// landed are pure instrument I/O, and whatever route the captured signal
// arrives on was closed by a Connect, or is being closed and reopened around
// each reading by Measure.
//
// Unlike MeasureEngine and the two routing engines, neither alias takes a
// template argument, for that same reason -- there is no FabricT,
// InstrumentWiringT or ConnectorWiringT for either of them to be instantiated
// with.
//
using ArmEngine   = core::ArmEngine;
using AwaitEngine = core::AwaitEngine;

//
// Both are defined in hal/src/verbs/measure.cpp, not in a translation unit of their own,
// and that is a requirement rather than a filing choice -- for Await, at least.
// It is constructed from core::MeasureEngine's session bank (the same
// construction core::ReadEngine has, and for the same reason: one run, one
// ordered recording covering every seam), and across translation units there
// is no initialisation order to rely on. See hal/verbs/measure.hpp, which says this
// about Read at more length.
//
// Arm needs no bank and could have lived beside Apply and Setup in
// hal/src/verbs/source.cpp. It is here instead because the pair is one operation split in
// half by the event it captures, and a reader looking for the other half should
// not have to know which of the two happens to carry state. That reasoning is
// this header's whole justification for existing at all.
//
extern ArmEngine   Arm;
extern AwaitEngine Await;
