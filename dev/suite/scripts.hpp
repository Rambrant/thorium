#pragma once

//
// The dev suite's script declarations -- what THORIUM_TEST_SCRIPTS points at,
// and what every TEST( ...) row in dev/suite/test_catalog.inc is name-checked
// against. The sibling of suite/scripts.hpp; see that file and suite/README.md
// for why these are at global scope and why declarations live apart from the
// prelude a script body includes.
//
auto dmmSelfCheck() -> void;
