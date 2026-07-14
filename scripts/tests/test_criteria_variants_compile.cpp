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

        #include "suite/production.inc"
    }

    namespace stress
    {
        using namespace core::quantities;
        using namespace core::literals;

        #include "suite/stress.inc"
    }

    namespace aged
    {
        using namespace core::quantities;
        using namespace core::literals;

        #include "suite/aged.inc"
    }

    //
    // Bare compilation only proves a variant's CRITERIA/CRIT block is
    // *syntactically* valid C++ -- it does NOT prove a CRIT id wasn't
    // silently renamed (a struct with a renamed member still compiles
    // fine). This concept, checked against every variant below, is
    // what actually catches that: the same "concept-wrapped requires"
    // pattern used in test_static_constraints.cpp for compile-time
    // checks, here used positively rather than negatively.
    //
    template<typename Group>
    concept HasSupplyRailCriteria = requires
    {
        Group::FS_Supply_5V0;
        Group::FS_Supply_3V3;
    };

    static_assert( HasSupplyRailCriteria<production::FS_Supply_1> );
    static_assert( HasSupplyRailCriteria<stress::FS_Supply_1> );
    static_assert( HasSupplyRailCriteria<aged::FS_Supply_1> );

    //
    // Same idea for the fuse-register criteria -- this check went missing
    // when the per-script folders were flattened into one file per variant;
    // without it, a typo'd FS_Fuse_01/02 in any variant would compile fine
    // (a struct with a renamed member still compiles) and go undetected.
    //
    template<typename Group>
    concept HasFuseRegisterCriteria = requires
    {
        Group::FS_Fuse_01;
        Group::FS_Fuse_02;
    };

    static_assert( HasFuseRegisterCriteria<production::FS_Fuse_6> );
    static_assert( HasFuseRegisterCriteria<stress::FS_Fuse_6> );
    static_assert( HasFuseRegisterCriteria<aged::FS_Fuse_6> );

} // namespace thorium_criteria_compile_check

TEST( CriteriaVariants, AllVariantsCompile)
{
    //
    // Nothing to run: reaching this line at all means every #include above
    // already compiled as valid CRITERIA/CRIT C++. See the file comment.
    //
    SUCCEED();
}
