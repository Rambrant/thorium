#pragma once

#include "core/apply.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

//
// The concrete instantiation of core::ApplyEngine/core::RemoveEngine for
// this rig -- mirrors hal/measure.hpp's MeasureEngine alias exactly, and for
// the same reason: FabricT/InstrumentWiringT/ConnectorWiringT are rig facts,
// not DUT facts, so this header has no dependency on anything named dut::.
//
using ApplyEngine  = core::ApplyEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;
using RemoveEngine = core::RemoveEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;

//
// The two points every script sources through:
//
//   Apply(  DcP1.dc( at( Input24V)).voltage( 24_V).currentLimit( 7_A));
//   Remove( DcP1.dc( at( Input24V)));
//
// Defined once in hal/apply.cpp, wired to this rig's fabric and its two
// static wiring tables (see hal/wiring.inc) -- see hal/measure.hpp's own
// comment, which this mirrors exactly.
//
extern ApplyEngine  Apply;
extern RemoveEngine Remove;
