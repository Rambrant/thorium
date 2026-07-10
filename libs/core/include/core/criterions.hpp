#pragma once

#include "core/criterion.hpp"
#include "core/predicates.hpp"

//
// Pulls in every GROUP/CRIT table for the currently active tolerance
// variant, in one shot. This lives in core (not dut) because it's pure
// framework mechanism -- it only touches core::criterion/core::predicates,
// nothing dut-specific -- so any layer above core can use it.
//
// Which file that is comes from THORIUM_ACTIVE_CRITERIA, a compile
// definition set by CMake from a single, validated option
// (THORIUM_CRITERIA_VARIANT -- see criteria/README.md and
// scripts/CMakeLists.txt), not a raw, untyped environment variable read via
// #ifdef.
//
// The .inc file itself has no scaffolding of its own (no #pragma once, no
// #includes, no namespace) -- this header supplies all of that. Every
// variant under criteria/ is one flat file holding every script's GROUP/CRIT
// side by side, so this is a single #include, not one per script.
//
#include THORIUM_ACTIVE_CRITERIA
