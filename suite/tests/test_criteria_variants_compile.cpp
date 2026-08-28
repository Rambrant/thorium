#include "core/criteria/criterion.hpp"
#include "core/criteria/predicates.hpp"
#include "core/quantities/quantity.hpp"

#include <gtest/gtest.h>

#include <meta>
#include <string_view>
#include <vector>

//
// This file has no runtime assertions -- like test_static_constraints.cpp
// in framework/core, the entire point is that it compiles. Every criteria
// variant for every script is #included here, each in its own namespace,
// and checked against every other for group/id parity.
//
// What this file is for has narrowed, and it is worth being precise about
// what is left. It used to be the only place all variants existed side by
// side: the scripts saw exactly one at a time, so a typo in a variant nobody
// was currently building against went unnoticed until somebody targeted that
// hardware/temperature/age scenario for real, and this file existed to close
// that gap.
//
// That is now how the framework itself works. core/criteria/active_criteria.hpp reads
// every variant into thorium::criteria::<name> and merges them, so a real
// build already fails if a group or CRIT that production declares is missing
// from another variant -- with an error naming the id and the variant, which
// is a better diagnostic than the static_asserts below produce.
//
// What survives that, and is why this file is still here: the merge only ever
// looks up the ids production declares. A group or CRIT that exists ONLY in a
// non-reference variant is invisible to it -- never merged, never run, and
// never complained about. The both-directions checkParity() calls at the
// bottom are what catch that, and they are the reason this runs both ways
// between every pair rather than only outward from production.
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
    // still compiles fine). This section generates the actual cross-variant
    // check, automatically, from the three namespaces above -- no hand-
    // written concept/static_assert per CRITERIA group. That matters here
    // specifically: with dozens (eventually up to ~100) groups, a hand-
    // written check per group would itself become a maintenance burden and
    // a place for a missed check to hide the exact bug this file exists to
    // catch.
    //
    // The technique used to be the same redefine-the-macro-and-re-#include
    // trick CRIT_FROM_PRODUCTION and hal::safeRig() also use -- re-reading
    // each variant's .inc file a second time with CRITERIA/CRIT redefined
    // to emit a concept+static_assert instead of building a Criterion
    // table. Reflection replaces that here: production::/stress::/aged::
    // above already ARE real, declared C++ entities after the first (and
    // only) read, so there's no need to re-parse anything -- just reflect
    // over what's already there. See core/criteria/criterion.hpp's own comment on
    // core::meta::all()/get() for the same shift, applied to a single
    // group rather than three namespaces side by side.
    //
    namespace meta
    {
        consteval auto membersOfInfo( std::meta::info scope) -> std::vector<std::meta::info>
        {
            return std::meta::members_of( scope, std::meta::access_context::current());
        }

        //
        // Every member of a namespace or struct reflection, promoted to
        // static storage -- see core/criteria/criterion.hpp's own comment on
        // std::meta::info being fine to promote (unlike a type with a
        // std::string_view member) for why this doesn't need the
        // key-only workaround hal/topology/wiring.hpp's connectorWiringKeys does.
        //
        template<std::meta::info Scope>
        constexpr auto members = std::define_static_array( membersOfInfo( Scope));

        template<typename> constexpr bool dependentFalse = false;

        // The type member of Other (a namespace) named `wanted`, or
        // std::meta::info{} if Other has no such member -- i.e. "does this
        // variant have a CRITERIA group with this name at all".
        template<std::meta::info Other>
        consteval auto findGroupByName( std::string_view wanted) -> std::meta::info
        {
            for( auto member : members<Other>)
            {
                if( std::meta::is_type( member) && std::meta::identifier_of( member) == wanted)
                {
                    return member;
                }
            }

            return {};
        }

        // Does the group reflection Group declare a CRIT (a data member)
        // named `wanted`?
        template<std::meta::info Group>
        consteval auto hasCrit( std::string_view wanted) -> bool
        {
            for( auto member : members<Group>)
            {
                if( std::meta::is_variable( member) && std::meta::identifier_of( member) == wanted)
                {
                    return true;
                }
            }

            return false;
        }

        //
        // For every CRITERIA group declared in Reference, and every CRIT in
        // it, static_assert that Other has a same-named group with a same-
        // named CRIT too. One direction only catches an id/group *missing*
        // from Other; everyVariantMatches below runs this both ways between
        // every pair, which is what also catches a stray/extra group or id
        // that exists in only one non-reference variant -- the same
        // coverage driving the old mechanism from all three variants (not
        // just production) gave.
        //
        template<std::meta::info Reference, std::meta::info Other>
        consteval auto checkParity() -> bool
        {
            template for( constexpr auto group : members<Reference>)
            {
                if constexpr( std::meta::is_type( group))
                {
                    constexpr auto groupName  = std::meta::identifier_of( group);
                    constexpr auto otherGroup = findGroupByName<Other>( groupName);

                    if constexpr( otherGroup == std::meta::info{})
                    {
                        static_assert( dependentFalse<std::integral_constant<int, 0>>,
                                      "a CRITERIA group exists in one variant but not another -- see "
                                      "the failing template arguments above for which group/variant");
                    }
                    else
                    {
                        template for( constexpr auto member : members<group>)
                        {
                            if constexpr( std::meta::is_variable( member))
                            {
                                constexpr auto critName = std::meta::identifier_of( member);

                                static_assert( hasCrit<otherGroup>( critName),
                                              "a CRIT id exists in one variant's group but not the "
                                              "same-named group in another -- see the failing template "
                                              "arguments above for which id/group/variant");
                            }
                        }
                    }
                }
            }

            return true;
        }
    } // namespace meta

    constexpr bool everyVariantMatches =
        meta::checkParity<^^production, ^^stress>() &&
        meta::checkParity<^^stress, ^^production>() &&
        meta::checkParity<^^production, ^^aged>()   &&
        meta::checkParity<^^aged, ^^production>();

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
