#pragma once

#include "core/apply.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

//
// The concrete instantiation of core::ApplyEngine/core::RemoveEngine for
// this rig. Apply/Remove no longer need any rig facts injected (they're
// pure instrument I/O -- see core/apply.hpp's own comment for why), so
// unlike ConnectEngine/DisconnectEngine and hal/measure.hpp's MeasureEngine
// alias, these two aliases take no template arguments.
//
using ApplyEngine  = core::ApplyEngine;
using RemoveEngine = core::RemoveEngine;

//
// The concrete instantiation of core::ConnectEngine/core::DisconnectEngine
// for this rig -- mirrors hal/measure.hpp's MeasureEngine alias exactly,
// and for the same reason: FabricT/InstrumentWiringT/ConnectorWiringT are
// rig facts, not DUT facts, so this header has no dependency on anything
// named dut::.
//
using ConnectEngine    = core::ConnectEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;
using DisconnectEngine = core::DisconnectEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;

//
// The four calls every script sources through -- DcP1/AcP1 take no at(...)
// point: they're fixed-wired straight to one VPC pin (or four, for AcP1's
// three phases plus ground -- see hal::Ac6677A's own comment), so there's
// nothing left to choose. A routed, measuring instrument like hal::DSO8064
// still takes at(...) on Measure() -- see hal/measure.hpp -- since it
// genuinely can reach more than one point through the mux.
//
//   Apply(      DcP1.dc().voltage( 24_V).currentLimit( 7_A));
//   Connect(    DcP1.dc());
//   ...
//   Disconnect( DcP1.dc());
//   Remove(     DcP1.dc());
//
// Apply/Remove and Connect/Disconnect can be called in either relative
// order -- see core/apply.hpp's own comment. Defined once in hal/apply.cpp,
// Connect/Disconnect wired to the linking rig's fabric and its two static
// wiring tables (see that rig's wiring.inc -- rig/wiring.inc in this repo)
// -- see hal/measure.hpp's own comment, which this mirrors exactly.
//
extern ApplyEngine      Apply;
extern RemoveEngine     Remove;
extern ConnectEngine    Connect;
extern DisconnectEngine Disconnect;
