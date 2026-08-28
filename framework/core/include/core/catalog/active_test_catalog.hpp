#pragma once

#include <array>
#include <cstddef>
#include <meta>
#include <vector>

#include "core/catalog/test_catalog.hpp"

#include THORIUM_TEST_SCRIPTS

//
// Pulls together the test catalog for this specific DUT, in one shot. This
// is deliberately a separate header from test_catalog.hpp: that file is the
// general, dependency-free GROUP/TEST/END_GROUP mechanism (no notion of any
// specific test suite at all), while this one is a specific consumer of it
// -- resolving THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS requires a suite's
// build configuration, so folding this into test_catalog.hpp would force
// that requirement onto every unrelated user of the general macros. Exactly
// mirrors why core/criteria/active_criteria.hpp is separate from core/criteria/criterion.hpp.
//
// THORIUM_TEST_SCRIPTS points at a suite's scripts.hpp (see
// suite/scripts/scripts.hpp) -- the declarations every TEST(...) below is
// checked against. It's included here, at file scope, deliberately -- NOT
// inside namespace detail below.
//
// Notice what's absent compared to active_criteria.hpp: no
// "using namespace" of any suite-specific namespace. Scripts are declared
// at global scope (see suite/scripts/scripts.hpp's own reasoning for why),
// so TEST(...) below can already name one directly, via ordinary
// unqualified lookup reaching all the way out to global scope -- the same
// way this file can write std::array without "using namespace std;". That's
// what lets this whole file live in core: unlike active_criteria.hpp (whose
// namespace core::quantities is core's own), there is no suite-specific
// namespace name for this file to know about at all.
//
namespace core::catalog
{
    //
    // Fallbacks for a catalog that declares no RUN_SETUP/RUN_TEARDOWN of its
    // own -- which is the normal case, and must stay the case that needs no
    // placeholder written anywhere.
    //
    // These have to be declared *before* namespace detail below, because that
    // is what makes them findable from inside it. A catalog's own RUN_SETUP
    // expands to a SetupHook in detail, which hides the one here; a catalog
    // with no RUN_SETUP declares nothing, and unqualified lookup inside detail
    // walks out to this one instead. That is the whole mechanism: ordinary name lookup, not
    // a detection trick, and the reason the two resolutions below are written
    // unqualified rather than as detail::SetupHook.
    //
    inline constexpr Bracket SetupHook    {};
    inline constexpr Bracket TeardownHook {};

    namespace detail
    {
        #include THORIUM_TEST_CATALOG

        //
        // Unqualified on purpose -- see the fallbacks' comment above. Resolved
        // here, inside detail, because that is the only scope where a catalog's
        // own declaration is visible to hide the fallback.
        //
        inline constexpr Bracket ResolvedSetup    = SetupHook;
        inline constexpr Bracket ResolvedTeardown = TeardownHook;
    } // namespace detail

    //
    // The hooks that bracket a run, each with the prose its RUN_SETUP /
    // RUN_TEARDOWN line described it with -- or a null hook described by
    // nothing, where the catalog declared none. The runner checks before
    // calling (see framework/runner/src/main.cpp).
    //
    inline constexpr Bracket Setup    = detail::ResolvedSetup;
    inline constexpr Bracket Teardown = detail::ResolvedTeardown;

    namespace detail
    {
        //
        // Every GROUP the catalog declared, found by reflecting over this
        // namespace rather than by being listed by hand.
        //
        // That list used to be written out here, one core::TestGroup line per
        // group, and it was the last place in this codebase where adding a
        // thing to a declarative table meant also editing framework code to
        // acknowledge it. The cost was not just the extra line. Because this
        // header is compiled against *whichever* catalog a target supplies, a
        // hardcoded list forced every catalog to declare every group name the
        // list mentioned -- so the hook fixture
        // (framework/runner/tests/fixtures/hooked_catalog.inc) had to carry a group
        // named after one in the shipped suite, and adding a group to the
        // shipped suite meant adding a matching one to the fixture, changing
        // what the acceptance tests observe. A suite gaining a test group could
        // not be a suite-only change.
        //
        // Reflection removes the list and the requirement together: a catalog
        // now declares what it declares, and this header reads it. Same move,
        // and the same reasoning, as core::meta::all() replacing the
        // re-#include walks in core/criteria/criterion.hpp and hal::safeRig()'s
        // replacing the third read of instrument.inc.
        //
        // A group is recognised by the marker it inherits (core::TestGroupTag,
        // which GROUP attaches) rather than by the members it happens to have.
        // This was a shape match -- a type with Name, Description and Tests --
        // for as long as every group had exactly those three members, and had
        // to stop being one when SETUP/TEARDOWN made the member
        // list vary per group. The marker is the better test anyway, and is
        // what hal::bundleLocationList() already matches core::AdapterBundleTag
        // by: it asks what a type *is*, so nothing a catalog could plausibly
        // declare alongside its groups can be mistaken for one.
        //
        consteval auto isTestGroup( const std::meta::info type) -> bool
        {
            for( const auto base : std::meta::bases_of( type, std::meta::access_context::current()))
            {
                if( std::meta::type_of( base) == ^^core::TestGroupTag)
                {
                    return true;
                }
            }

            return false;
        }

        consteval auto groupTypeInfos() -> std::vector<std::meta::info>
        {
            std::vector<std::meta::info> result;

            for( const auto member : std::meta::members_of( ^^detail, std::meta::access_context::current()))
            {
                if( !std::meta::is_type( member))
                {
                    continue;
                }

                const auto type = std::meta::dealias( member);

                if( std::meta::is_class_type( type) && isTestGroup( type))
                {
                    result.push_back( type);
                }
            }

            return result;
        }

        constexpr auto groupRefs = std::define_static_array( groupTypeInfos());

        //
        // Every TEST of one group, in declaration order -- the array the
        // runner walks, assembled from the group's own static members rather
        // than accumulated by the TEST macro (see core/catalog/test_catalog.hpp on why
        // that changed).
        //
        // Matched by type, the same way core::meta::detail::criterionMembers()
        // matches a CRIT: remove_cv() first, because a static constexpr member
        // reflects as const-qualified and an unqualified ^^core::TestCase
        // would therefore match nothing at all. GROUP's own Name/Description
        // and the hook members below are variables too, and this is what tells
        // them apart from a test.
        //
        template<typename GroupT>
        consteval auto testCaseMembers() -> std::vector<std::meta::info>
        {
            std::vector<std::meta::info> result;

            for( const auto member : std::meta::members_of( ^^GroupT, std::meta::access_context::current()))
            {
                if( std::meta::is_variable( member) &&
                    std::meta::remove_cv( std::meta::type_of( member)) == ^^core::TestCase)
                {
                    result.push_back( member);
                }
            }

            return result;
        }

        //
        // Promoted to static storage so the splices below have a genuinely
        // constexpr entity to splice -- see core::meta::detail::members's own
        // comment for the same requirement biting the same way. std::meta::info
        // is fine to promote; core::TestCase would not be (its std::string_view
        // members are not structural types on this standard library, see
        // hal::detail::ConnectorWiringKey), which is why it is the *handles*
        // that go through define_static_array here and the values that go into
        // an ordinary std::array below.
        //
        template<typename GroupT>
        constexpr auto testCaseRefs = std::define_static_array( testCaseMembers<GroupT>());

        //
        // The group's tests as values, in a constexpr object with static
        // storage duration -- which is what lets core::TestGroup::tests be a
        // span over it, exactly as it was a span over GROUP's own Tests array
        // before.
        //
        template<typename GroupT>
        constexpr auto testsOf = []
        {
            std::array<core::TestCase, testCaseRefs<GroupT>.size()> cases{};

            std::size_t next = 0;

            template for( constexpr auto testRef : testCaseRefs<GroupT>)
            {
                cases[ next++] = [: testRef :];
            }

            return cases;
        }();

        //
        // A group's own hooks and their prose, or a null Bracket where it
        // declared none.
        //
        // Ordinary member lookup in a requires-expression, not reflection:
        // SETUP declares the member and no SETUP line leaves it
        // absent, which is precisely the question `requires` answers. The
        // run-level pair resolves its own absence by name lookup too (see the
        // fallbacks above) -- differently, because a namespace-scope name can
        // be shadowed by a nearer declaration while a class member cannot.
        //
        template<typename GroupT>
        consteval auto setupOf() -> Bracket
        {
            if constexpr( requires { GroupT::GroupSetup; })
            {
                return GroupT::GroupSetup;
            }
            else
            {
                return {};
            }
        }

        template<typename GroupT>
        consteval auto teardownOf() -> Bracket
        {
            if constexpr( requires { GroupT::GroupTeardown; })
            {
                return GroupT::GroupTeardown;
            }
            else
            {
                return {};
            }
        }
    } // namespace detail

    //
    // The whole catalog, flattened to one compile-time array of groups, in the
    // order the catalog file declares them -- members_of preserves declaration
    // order, which is what a UI listing "group -> tests" top to bottom depends
    // on. A UI walks this; nothing here allocates or parses a string to find a
    // test -- ids and descriptions are for display and reporting, never for
    // internal dispatch.
    //
    // Adding a GROUP or a TEST now touches the catalog file and nothing else.
    //
    inline constexpr auto Catalog = []
    {
        std::array<core::TestGroup, detail::groupRefs.size()> groups{};

        std::size_t next = 0;

        template for( constexpr auto groupRef : detail::groupRefs)
        {
            using GroupT = typename [: groupRef :];

            groups[ next++] = core::TestGroup{ GroupT::Name,
                                               GroupT::Description,
                                               detail::testsOf<GroupT>,
                                               detail::setupOf<GroupT>(),
                                               detail::teardownOf<GroupT>() };
        }

        return groups;
    }();
} // namespace core::catalog
