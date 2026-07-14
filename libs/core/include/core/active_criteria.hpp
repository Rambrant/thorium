#pragma once

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/quantity.hpp"

//
// Pulls in every CRITERIA/CRIT table for the currently active tolerance
// variant, in one shot. This is deliberately a separate header from
// criterion.hpp: that file is the general, dependency-free CRITERIA/CRIT
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
// (THORIUM_CRITERIA_VARIANT -- see suite/README.md and
// scripts/CMakeLists.txt), not a raw, untyped environment variable read via
// #ifdef.
//
// Every .inc file itself has no scaffolding of its own (no #pragma once, no
// #includes, no namespace, no "core::"/"using namespace" of its own) --
// this header supplies all of that, including bringing core::quantities'
// predicates (EQ, MASK, ...), quantity types (Voltage, ...), and
// core::literals' literals (_V, ...) into unqualified scope, so a criteria
// file can write EQ( 5.0_V).epsilon( 0.05_V) instead of
// core::quantities::EQ( core::quantities::Voltage{ 5.0}).epsilon( core::quantities::Voltage{ 0.05}).
//
// Deliberately core::quantities, not a blanket "using namespace core;":
// core also holds Instrument, Verify, SwitchMatrix, Logger, and everything
// else in the framework, none of which a criteria file has any business
// touching. core::quantities holds only the predicate/quantity vocabulary
// (see quantity.hpp and predicates.hpp, which define it and reopen it
// respectively) -- anything new added there is automatically part of what a
// criteria file can use, with nothing else dragged in alongside it.
//
// Each .inc file is one flat file holding every script's CRITERIA/CRIT side
// by side.
//

//
// The baseline: production's tolerances, always available regardless of
// which variant is actually active, so other variants can use
// CRIT_FROM_PRODUCTION (see core/criterion.hpp) to borrow "the same value as
// production" for criteria that don't change, instead of retyping it.
// THORIUM_PRODUCTION_CRITERIA is a fixed path -- always suite/production.inc
// -- unlike THORIUM_ACTIVE_CRITERIA below, which depends on
// THORIUM_CRITERIA_VARIANT.
//
// The two "using namespace" lines here are confined to this reopened
// "production" block -- a using-directive inside a *named* namespace does
// not leak to the enclosing scope (verified directly; unlike an anonymous
// namespace, which would, since using-directives are transitive and an
// anonymous namespace implicitly "using"s itself into whatever scope it's
// declared in).
//
namespace production
{
    using namespace core::quantities;
    using namespace core::literals;

    #include THORIUM_PRODUCTION_CRITERIA
}

//
// The active variant. If THORIUM_CRITERIA_VARIANT is "production", this is
// the same file as above, included a second time, now unqualified --
// redundant but harmless. Any other variant can use CRIT_FROM_PRODUCTION for
// criteria that don't change; see suite/stress.inc and suite/aged.inc.
//
// Unlike the production block above, this can't be confined to a named
// namespace: the resulting CRITERIA structs (FS_Supply_1, FS_Fuse_6, ...)
// need to land unqualified, at whatever scope includes this header, so
// scripts can keep writing FS_Supply_1::FS_Supply_5V0 directly. That means
// the two "using namespace" lines below are NOT confined the way
// production's are -- they remain in effect for the rest of whatever
// script.cpp includes this header, the same as if that script had written
// `using namespace core::literals;` itself (which fuse_register_script.cpp
// already does, explicitly, today).
//
using namespace core::quantities;
using namespace core::literals;

#include THORIUM_ACTIVE_CRITERIA
