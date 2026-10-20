#pragma once
//
// What a dev script's body is written against -- the sibling of
// suite/prelude.hpp, and like it now only the two things this deployment can
// name: its scripts.hpp and its DUT's adapter. Everything else, the verbs
// included, is the framework's runner/prelude.hpp, shared with the bench's.
//
// Which is why the desk has the triggered-capture verbs
// (hal/verbs/acquire.hpp and trace.hpp: Arm/Await/Fetch) and
// hal/verbs/route.hpp even though nothing here can be routed -- they come with
// every deployment, and Connect/Disconnect are what a script would reach for
// first when a card does arrive.
//
#include "dev/suite/scripts.hpp"
#include "runner/prelude.hpp"
//
// Included for the same reason the bench's prelude includes it, even though the
// table it opens is empty (see dev/dut/adapter.inc): at( ...) resolves through
// the `dut` struct, so a script that grows a routed reading the day this desk
// grows a card needs nothing added here.
//
#include "dev/dut/adapter.inc"
