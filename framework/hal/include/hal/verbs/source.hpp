#pragma once

#include "core/verbs/source.hpp"
#include "core/verbs/transfer.hpp"

//
// The concrete instantiation of the sourcing verbs for this rig -- the ones
// that are pure instrument I/O and need no rig facts injected at all (see
// core/verbs/source.hpp's own comment for why routing moved out from under Apply),
// so unlike hal/verbs/route.hpp's Connect/Disconnect and hal/verbs/measure.hpp's
// MeasureEngine alias, none of these aliases takes a template argument.
//
// That is also why this header includes neither hal/fabric/switch_fabric.hpp nor
// hal/topology/wiring.hpp: nothing declared here has any idea the switching fabric
// exists.
//
using ApplyEngine  = core::ApplyEngine;
using RemoveEngine = core::RemoveEngine;

//
// Setup and Write are here for that same reason: both are pure instrument I/O
// too. Setup configures without energising (see core::SetupEngine on why that
// is a verb of its own rather than a flavour of Apply), and Write puts a
// payload out of an already-connected port.
//
// Read is NOT here, and that is not an oversight -- it is declared in
// hal/verbs/measure.hpp, alongside Measure, because it reads through the same
// core::SessionBank and the two globals have to be constructed together. See
// that header.
//
using SetupEngine = core::SetupEngine;
using WriteEngine = core::WriteEngine;

//
// The verbs every script sources through -- none of them takes an at(...)
// point, because none of them touches the fabric: what a source instrument's
// output is cabled onto is Connect's business (hal/verbs/route.hpp), and a routed,
// measuring instrument like hal::DSO8064A takes at(...) on Measure() instead
// (see hal/verbs/measure.hpp).
//
//   Apply( DcP1.dc().voltage( 24_V).currentLimit( 7_A));
//   Setup( Ser1.rs232().baudRate( 9600).wordLength( 8).parity( Parity::None));
//   Write( Ser1.rs232(), "RD 30\r");
//   Remove( DcP1.dc());
//
// Each of those belongs inside a Connect/Disconnect pair, and in that order --
// see hal/verbs/route.hpp for the framing, and core/verbs/source.hpp for why the relay
// should move while the path is dead.
//
// Defined once in hal/verbs/source.cpp, which needs nothing from the linking rig --
// see that file, and hal/verbs/route.hpp for the two verbs that do.
//
extern ApplyEngine  Apply;
extern RemoveEngine Remove;
extern SetupEngine  Setup;
extern WriteEngine  Write;
