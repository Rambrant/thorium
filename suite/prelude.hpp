#pragma once

//
// Everything a test script is written against, in one include -- so a script
// begins with `#include "../prelude.hpp"` and nothing else, and the list of
// what the framework hands a script lives here, once, instead of being copied
// partially and inconsistently into the top of every script. Which header
// supplies Verify and which supplies Measure is framework bookkeeping; it was
// never a distinction a test engineer writing a script should have had to
// carry.
//
// The split from scripts.hpp alongside it runs the opposite way from what the
// names suggest. scripts.hpp is the *declarations* file, and its only reader is
// core/catalog/active_test_catalog.hpp via THORIUM_TEST_SCRIPTS, which name-checks each
// TEST( ..., someScript, ...) entry and wants nothing else at all -- that file
// is compiled into main.cpp, which has no business seeing a criteria table or a
// rig's instrument globals. The two stand-ins for scripts.hpp
// (framework/runner/tests/fixtures/hooked_scripts.hpp,
// framework/core/tests/fixtures/hooked_scripts.hpp) are pure declaration files for
// exactly that reason, and scripts.hpp now matches them; everything a script
// *body* needs collects here instead.
//
// This file reaches dut/ and rig/ by a plain repo-root-relative #include, the
// way scripts.hpp always has. suite/ is deployment content, not framework, so
// unlike anything under framework/ it is allowed to know which DUT and which rig it
// was built for. That is why there is no THORIUM_DUT_ADAPTER compile definition
// to match THORIUM_PRODUCTION_CRITERIA: active_criteria.hpp needs one only
// because it lives in core and must not name dut/ -- see the top-level
// CMakeLists.txt's own comment on that split.
//

//
// The scripts' own declarations -- each script .cpp is checked against the
// signature the catalog will call it by.
//
#include "scripts.hpp"

//
// The rig: Dmm1, Osc1, DcP1.. as globals a script addresses by name, and the
// verbs they are pointed through -- Measure, and the Apply/Connect/Disconnect/
// Remove sourcing counterparts. at(...) comes with them.
//
#include "rig/active_instruments.hpp"
#include "hal/verbs/measure.hpp"
#include "hal/verbs/source.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/acquire.hpp"
#include "hal/verbs/trace.hpp"

//
// The verdict verbs, unqualified -- so a script writes Verify( ...) next to
// Measure( ...), rather than core::Verify( ...) next to a bare Measure. That
// asymmetry was an accident of which header happened to bring what into scope,
// and the two scripts in this suite had already drifted to opposite sides of
// it.
//
// Fail comes with it, and has to: it is what a script writes where a check
// could not be made at all, so it appears in the same bodies and on the same
// footing as Verify (see core/criteria/verify.hpp).
//
// A using-declaration in a header, deliberately, and confined to this one:
// core/catalog/active_test_catalog.hpp does not include this file, so nothing outside a
// script translation unit is affected. It mirrors active_criteria.hpp's own
// using-directives, and for the same reason -- see its comment on why the
// merged tables have to land unqualified.
//
#include "core/criteria/verify.hpp"

using core::Verify;
using core::Fail;

//
// The tolerances -- every variant merged into the one table a script names
// (FS_Supply_1::FS_Supply_5V0, ...) -- and with them the unqualified predicate
// and quantity vocabulary (EQ, MASK, _V, _A) that active_criteria.hpp
// deliberately promises to whatever includes it. A script writing an ad-hoc
// EQ( 12.0_V).epsilon( 0.05_V) needs no further include of its own.
//
#include "core/criteria/active_criteria.hpp"

//
// The adapter points -- dut::Output5V and friends. hal/topology/adapter.hpp has to come
// first, for the ADAPTER/POINT macros the .inc is written in; that ordering
// rule is precisely the kind of thing worth stating once here instead of at the
// top of every script.
//
#include "hal/topology/adapter.hpp"
#include "dut/adapter.inc"
