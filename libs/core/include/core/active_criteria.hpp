#pragma once

#include "core/criterion.hpp"
#include "core/predicates.hpp"

//
// Pulls in every GROUP/CRIT table for the currently active tolerance
// variant, in one shot. This is deliberately a separate header from
// criterion.hpp: that file is the general, dependency-free GROUP/CRIT
// mechanism (used on its own by e.g. test_criterion.cpp, with no notion of
// "variants" at all), while this one is a specific consumer of it --
// resolving THORIUM_ACTIVE_CRITERIA/THORIUM_PRODUCTION_CRITERIA requires the
// scripts target's build configuration, so folding this into criterion.hpp
// would force that requirement onto every unrelated user of the general
// macros. (CRIT_FROM_PRODUCTION's *definition* lives in criterion.hpp
// despite assuming the same convention -- see the comment there for why
// that one's different: it's opt-in and inert until actually used.)
//
// Which files THORIUM_PRODUCTION_CRITERIA / THORIUM_ACTIVE_CRITERIA point to
// are compile definitions set by CMake from a single, validated option
// (THORIUM_CRITERIA_VARIANT -- see criteria/README.md and
// scripts/CMakeLists.txt), not a raw, untyped environment variable read via
// #ifdef.
//
// Every .inc file itself has no scaffolding of its own (no #pragma once, no
// #includes, no namespace) -- this header supplies all of that. Each one is
// one flat file holding every script's GROUP/CRIT side by side.
//

//
// The baseline: production's tolerances, always available regardless of
// which variant is actually active, so other variants can use
// CRIT_FROM_PRODUCTION (see core/criterion.hpp) to borrow "the same value as
// production" for criteria that don't change, instead of retyping it.
// THORIUM_PRODUCTION_CRITERIA is a fixed path -- always criteria/production.inc
// -- unlike THORIUM_ACTIVE_CRITERIA below, which depends on
// THORIUM_CRITERIA_VARIANT.
//
namespace production
{
    #include THORIUM_PRODUCTION_CRITERIA
}

//
// The active variant. If THORIUM_CRITERIA_VARIANT is "production", this is
// the same file as above, included a second time, now unqualified --
// redundant but harmless. Any other variant can use CRIT_FROM_PRODUCTION for
// criteria that don't change; see criteria/stress.inc and criteria/aged.inc.
//
#include THORIUM_ACTIVE_CRITERIA
