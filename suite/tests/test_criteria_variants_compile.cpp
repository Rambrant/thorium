#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/quantity.hpp"

#include <gtest/gtest.h>

//
// This file has no runtime assertions -- like test_static_constraints.cpp
// in libs/core, the entire point is that it compiles. Every criteria
// variant for every script is #included here, each in its own namespace,
// so a typo in a variant nobody is currently building against (e.g. "aged"
// while everyone's day-to-day building "production") is still caught the
// moment anyone builds this target -- not the day someone finally targets
// that hardware/temperature/age scenario for real.
//
// The actual scripts never see more than one variant at a time (see
// core/active_criteria.hpp), selected by THORIUM_CRITERIA_VARIANT. This
// file is the only place all variants exist side by side.
//
namespace thorium_criteria_compile_check
{
    namespace production
    {
        using namespace core::quantities;
        using namespace core::literals;

        #include "dut/criteria_production.inc"
    }

    namespace stress
    {
        using namespace core::quantities;
        using namespace core::literals;

        #include "dut/criteria_stress.inc"
    }

    namespace aged
    {
        using namespace core::quantities;
        using namespace core::literals;

        #include "dut/criteria_aged.inc"
    }

    //
    // Bare compilation only proves a variant's CRITERIA/CRIT block is
    // *syntactically* valid C++ -- it does NOT prove a CRIT id wasn't
    // silently renamed or dropped (a struct with a renamed/missing member
    // still compiles fine). This section generates the actual cross-
    // variant check, automatically, from the same three .inc files above
    // -- no hand-written concept/static_assert per CRITERIA group. That
    // matters here specifically: with dozens (eventually up to ~100)
    // groups, a hand-written check per group would itself become a
    // maintenance burden and a place for a missed check to hide the exact
    // bug this file exists to catch.
    //
    // The technique: redefine CRITERIA/CRIT/CRIT_FROM_PRODUCTION/
    // END_CRITERIA (push_macro/pop_macro-scoped, so the redefinition
    // doesn't leak) so that instead of building a Criterion table, each
    // CRIT/CRIT_FROM_PRODUCTION emits a concept + static_assert checking
    // that the exact same id exists in production, stress, AND aged.
    // Re-including each variant's .inc file under these definitions -- once
    // per variant, as the "driver" of which groups/ids get checked -- means
    // an id present in any ONE variant but missing (or renamed) from
    // another is caught, regardless of which variant happens to drift.
    // Driving from all three (not just production) is what also catches a
    // stray/extra group or id that exists in only one non-production
    // variant.
    //
    // CRITERIA (redefined) opens a namespace named after the group,
    // aliasing each variant's version of it as G_production/G_stress/
    // G_aged -- the same role core/criterion.hpp's real CRITERIA macro
    // gives "Name" (a fixed identifier CRIT can refer to without needing
    // the group name passed to it directly, since a CRIT/CRIT_FROM_PRODUCTION
    // invocation only ever receives an id, never its enclosing group's
    // name as a token).
    //
    #pragma push_macro("CRITERIA")
    #pragma push_macro("CRIT")
    #pragma push_macro("CRIT_FROM_PRODUCTION")
    #pragma push_macro("END_CRITERIA")
    #undef CRITERIA
    #undef CRIT
    #undef CRIT_FROM_PRODUCTION
    #undef END_CRITERIA

    #define CRITERIA( groupName, desc)                                                              \
        namespace check_##groupName                                                                 \
        {                                                                                           \
            using G_production = production::groupName;                                             \
            using G_stress     = stress::groupName;                                                 \
            using G_aged       = aged::groupName;

    #define THORIUM_CHECK_ID( id)                                                                   \
            template<typename G> concept Has_##id = requires { G::id; };                            \
            static_assert( Has_##id<G_production>, #id " missing/renamed in variant production");   \
            static_assert( Has_##id<G_stress>,     #id " missing/renamed in variant stress");       \
            static_assert( Has_##id<G_aged>,       #id " missing/renamed in variant aged");

    #define CRIT( id, pred, desc)              THORIUM_CHECK_ID( id)
    #define CRIT_FROM_PRODUCTION( group, id)   THORIUM_CHECK_ID( id)
    #define END_CRITERIA }

    namespace verify_from_production
    {
        #include "dut/criteria_production.inc"
    }

    namespace verify_from_stress
    {
        #include "dut/criteria_stress.inc"
    }

    namespace verify_from_aged
    {
        #include "dut/criteria_aged.inc"
    }

    #undef THORIUM_CHECK_ID
    #pragma pop_macro("END_CRITERIA")
    #pragma pop_macro("CRIT_FROM_PRODUCTION")
    #pragma pop_macro("CRIT")
    #pragma pop_macro("CRITERIA")

} // namespace thorium_criteria_compile_check

TEST( CriteriaVariants, AllVariantsCompile)
{
    //
    // Nothing to run: reaching this line at all means every #include above
    // already compiled as valid CRITERIA/CRIT C++, and every cross-variant
    // check above it held. See the file comment.
    //
    SUCCEED();
}
