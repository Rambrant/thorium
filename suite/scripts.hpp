#pragma once

//
// Each function here is a "test script": a scenario expressed using the
// dsl layer, exercising the dut through hal. Scripts should never reach
// down into hal:: or dut:: directly for behavior -- only to construct the
// device handle they're given.
//
// Declarations only, and deliberately so -- no instruments, no verbs, no
// tables, not even an #include. The one reader of this file is
// core/active_test_catalog.hpp via THORIUM_TEST_SCRIPTS, which name-checks
// each TEST(...) entry against the signatures below and needs nothing else;
// that header is compiled into main.cpp, so anything added here lands in a
// translation unit with no use for it. The two fixture stand-ins for this
// file (app/tests/fixtures/hooked_scripts.hpp,
// libs/core/tests/fixtures/hooked_scripts.hpp) are bare declaration files
// for that reason, and this one matches them.
//
// What a script *body* is written against -- the rig's instrument globals,
// Measure/Apply/Verify, the criteria tables, the adapter points -- lives in
// suite/prelude.hpp, which includes this file and is what each script .cpp
// includes. See its own comment for why the split runs this way round.
//
// Deliberately global, not namespace scripts { ... }: each script is the
// end point of its own call chain -- nothing calls *into* one script from
// another, or refers to them as a group by namespace -- so there's nothing
// a namespace would protect here. It also lets core/active_test_catalog.hpp
// name a script directly (fuseRegisterScript, not scripts::fuseRegisterScript)
// without a "using namespace scripts;" of its own, which is what keeps that
// wiring free of any project-specific namespace name. Collisions are easy
// to avoid by hand across a handful of distinctly-named scripts; if that
// stops being true, revisit this.
//

//
// Verifies fuse register content and output voltage against expected
// criteria. Demonstrates the declarative Criterion/Verify DSL style,
// as opposed to the fluent TestCase style used above.
//
[[nodiscard]]
auto fuseRegisterScript() -> bool;

// Verifies supply rail voltages by measuring at named test points. This is
// the instrument/matrix path: the script names *what* to check (a criterion)
// and *where* (a logical test point) -- the dsl::Measure verb hides the
// select-instrument / route-matrix / fetch sequence entirely.
//
[[nodiscard]]
auto supplyRailScript() -> bool;
