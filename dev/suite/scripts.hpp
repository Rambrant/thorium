#pragma once

//
// The dev suite's script declarations -- what THORIUM_TEST_SCRIPTS points at,
// and what every TEST( ...) row in dev/suite/test_catalog.inc is name-checked
// against. The sibling of suite/scripts.hpp; see that file and suite/README.md
// for why these are at global scope and why declarations live apart from the
// prelude a script body includes.
//
auto dmmSelfCheck() -> void;

// DmmFunctions -- dev/suite/scripts/dmm_functions.cpp
auto dmmDcVoltage() -> void;
auto dmmResolution() -> void;
auto dmmAcVoltage() -> void;
auto dmmDcCurrent() -> void;
auto dmmAcCurrent() -> void;
auto dmmResistance() -> void;
auto dmmFourWireResistance() -> void;
auto dmmFrequency() -> void;
auto dmmCapacitance() -> void;

// SupplyOutputs -- dev/suite/scripts/supply_outputs.cpp
auto psuOutput1Check() -> void;
auto psuOutput2Check() -> void;
auto psuOutput3Check() -> void;

// ScopeProbeComp -- dev/suite/scripts/scope_probe_comp.cpp
auto scopeAmplitudeCh1() -> void;
auto scopeAmplitudeCh2() -> void;
auto scopeTimingCh1() -> void;
auto scopeTimingCh2() -> void;
auto scopeCapture() -> void;
auto scopeAcquisitionTypes() -> void;
auto scopeCouplingAndBandwidth() -> void;
