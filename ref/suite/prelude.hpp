#pragma once
//
// Everything a test script is written against, in one include -- so a script
// begins with `#include "../prelude.hpp"` and nothing else. Most of that list is
// the framework's, in runner/prelude.hpp; what this file adds is the two things
// only this deployment can name.
//
// The split from scripts.hpp alongside it runs the opposite way from what the
// names suggest. scripts.hpp is the *declarations* file, and its only reader is
// core/catalog/active_test_catalog.hpp via THORIUM_TEST_SCRIPTS, which name-checks each
// TEST( ..., someScript, ...) entry and wants nothing else at all -- that file
// is compiled into main.cpp, which has no business seeing a criteria table or a
// rig's instrument globals. The two stand-ins for scripts.hpp
// (framework/runner/tests/fixtures/hooked_scripts.hpp,
// framework/runner/tests/fixtures/hooked_scripts.hpp) are pure declaration files for
// exactly that reason, and scripts.hpp now matches them; everything a script
// *body* needs collects here and in runner/prelude.hpp instead.
//
// This file reaches dut/ by a plain repo-root-relative #include, the way
// scripts.hpp always has. suite/ is deployment content, not framework, so
// unlike anything under framework/ it is allowed to know which DUT it was built
// for. That is why there is no THORIUM_DUT_ADAPTER compile definition to match
// THORIUM_CRITERIA_VARIANT_TABLES: active_criteria.hpp needs one only because it
// lives in core and must not name dut/ -- see the top-level CMakeLists.txt's own
// comment on that split.
//

//
// The scripts' own declarations -- each script .cpp is checked against the
// signature the catalog will call it by.
//
#include "scripts.hpp"
//
// The rig, the verbs, Verify and the criteria vocabulary.
//
#include "runner/prelude.hpp"
//
// The adapter points -- dut::Output5V and friends. runner/prelude.hpp has
// already brought in the ADAPTER/POINT macros the .inc is written in.
//
#include "dut/adapter.inc"
