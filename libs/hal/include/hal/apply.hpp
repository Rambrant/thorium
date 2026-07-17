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
// The four points every script sources through:
//
//   Apply(      DcP1.dc( at( Input24V)).voltage( 24_V).currentLimit( 7_A));
//   Connect(    DcP1.dc( at( Input24V)));
//   ...
//   Disconnect( DcP1.dc( at( Input24V)));
//   Remove(     DcP1.dc( at( Input24V)));
//
// Apply/Remove and Connect/Disconnect can be called in either relative
// order -- see core/apply.hpp's own comment. Defined once in hal/apply.cpp,
// Connect/Disconnect wired to this rig's fabric and its two static wiring
// tables (see hal/wiring.inc) -- see hal/measure.hpp's own comment, which
// this mirrors exactly.
//
extern ApplyEngine      Apply;
extern RemoveEngine     Remove;
extern ConnectEngine    Connect;
extern DisconnectEngine Disconnect;
