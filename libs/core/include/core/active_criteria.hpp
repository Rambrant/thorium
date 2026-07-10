#pragma once

#include "core/criterion.hpp"
#include "core/predicates.hpp"

//
// Pulls in every GROUP/CRIT table for the currently active tolerance
// variant, in one shot. This is deliberately a separate header from
// criterion.hpp: that file is the general, dependency-free GROUP/CRIT
// mechanism (used on its own by e.g. test_criterion.cpp, with no notion of
// "variants" at all), while this one is a specific consumer of it --
// resolving THORIUM_ACTIVE_CRITERIA requires the scripts target's build
// configuration, so folding this into criterion.hpp would force that
// requirement onto every unrelated user of the general macros.
//
// Which file THORIUM_ACTIVE_CRITERIA points to is a compile definition set
// by CMake from a single, validated option (THORIUM_CRITERIA_VARIANT --
// see criteria/README.md and scripts/CMakeLists.txt), not a raw, untyped
// environment variable read via #ifdef.
//
// The .inc file itself has no scaffolding of its own (no #pragma once, no
// #includes, no namespace) -- this header supplies all of that. Every
// variant under criteria/ is one flat file holding every script's GROUP/CRIT
// side by side, so this is a single #include, not one per script.
//
#include THORIUM_ACTIVE_CRITERIA
