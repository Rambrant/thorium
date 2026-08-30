#pragma once

#include "core/verbs/route.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"

//
// The concrete instantiation of core::ConnectEngine/core::DisconnectEngine
// for this rig -- mirrors hal/verbs/measure.hpp's MeasureEngine alias exactly,
// and for the same reason: FabricT/InstrumentWiringT/ConnectorWiringT are
// rig facts, not DUT facts, so this header has no dependency on anything
// named dut::.
//
using ConnectEngine    = core::ConnectEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;
using DisconnectEngine = core::DisconnectEngine<hal::SwitchFabric, hal::InstrumentWiring, hal::ConnectorWiring>;

//
// The two routing calls every script frames its sourcing with -- DcP1/AcP1
// take no at(...) point: they're fixed-wired straight to one VPC pin (or four,
// for AcP1's three phases plus ground -- see hal::Ac6834B's own comment), so
// there's nothing left to choose. A routed, measuring instrument like
// hal::DSO8064A still takes at(...) -- see hal/verbs/measure.hpp -- since it
// genuinely can reach more than one point through the mux, and a console
// interface takes the bundle form (see core::ConnectEngine's own comment).
//
//   Connect(    DcP1.dc());
//   Apply(      DcP1.dc().voltage( 24_V).currentLimit( 7_A));
//   ...
//   Remove(     DcP1.dc());
//   Disconnect( DcP1.dc());
//
// Note that the sequence nests rather than mirrors: the relay closes before
// the output comes up and opens after it goes down, so it never moves under
// load. Nothing here refuses to break that -- but a Connect or Disconnect that
// moved a contact on a live path records it in the run journal. See
// core/verbs/source.hpp's own comment for the reasoning and for when breaking it is
// the right call, and core/verbs/interlock.hpp for why this one is recorded rather
// than refused.
//
// Defined once in hal/src/verbs/route.cpp, wired to the linking rig's fabric and its two
// static wiring tables (see that rig's wiring.inc -- rig/wiring.inc in this
// repo) -- see hal/verbs/measure.hpp's own comment, which this mirrors exactly.
//
extern ConnectEngine    Connect;
extern DisconnectEngine Disconnect;
