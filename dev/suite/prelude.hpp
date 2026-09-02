#pragma once

//
// What a dev script's body is written against -- the sibling of
// suite/prelude.hpp, and deliberately its own file rather than a reuse of it.
//
// A prelude names its deployment's rig and DUT by path, so it is suite content
// and not framework content: this one reaches dev/rig/ and dev/dut/ where the
// bench's reaches rig/ and dut/. Two nearly identical files is the right
// outcome here -- the alternative is one file with the deployment threaded
// through it, which puts a conditional in the header every script includes to
// save four lines.
//
// Shorter than the bench's by exactly what this bench does not have. No
// hal/verbs/acquire.hpp or hal/verbs/trace.hpp: Arm/Await/Fetch are the triggered-capture
// verbs, and there is no scope here. hal/verbs/route.hpp is included even though
// nothing can be routed -- Connect/Disconnect are what a script would reach for
// first when a card does arrive, and the include is what makes that a wiring
// question rather than a prelude question.
//
#include "dev/suite/scripts.hpp"
#include "hal/topology/active_instruments.hpp"

#include "hal/verbs/measure.hpp"
#include "hal/verbs/source.hpp"
#include "hal/verbs/route.hpp"

#include "core/criteria/verify.hpp"

using core::Verify;
using core::Fail;

#include "core/criteria/active_criteria.hpp"

//
// Included for the same reason the bench's prelude includes it, even though the
// table it opens is empty (see dev/dut/adapter.inc): at( ...) resolves through
// the `dut` struct, so a script that grows a routed reading the day this desk
// grows a card needs nothing added here.
//
#include "hal/topology/adapter.hpp"
#include "dev/dut/adapter.inc"
