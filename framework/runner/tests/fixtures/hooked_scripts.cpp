#include "hooked_scripts.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "core/criteria/predicates.hpp"
#include "core/criteria/verify.hpp"

//
// The suite behind run_scripts_hooked: two scripts and four hooks -- the pair
// that brackets the run and the pair that brackets one group -- each announcing
// itself on stdout so an acceptance test can assert what ran, how often, and in
// what order. None of that is visible from the logs alone, since a hook posts no
// journal event of its own.
//
// Each one's outcome is steered by an environment variable rather than by a
// flag, deliberately: the runner's own command line is the thing under test
// here, and adding flags to it for a fixture's benefit would mean testing a
// binary that no rig will ever run.
//
namespace
{
    //
    // Written straight to stdout rather than through the journal. A hook is
    // not a test and posts no event, so the journal would have nothing to show
    // -- and stdout keeps these markers in the same stream as the console
    // sink's own output, which is what makes their relative order meaningful.
    //
    // Flushed on every marker (std::endl, not '\n'): the assertions are about
    // ordering, and a buffer that flushes at exit would hand back a file whose
    // order is the buffer's rather than the run's.
    //
    auto announce( const std::string_view marker) -> void
    {
        std::cout << "HOOKFIXTURE " << marker << std::endl;
    }

    [[nodiscard]]
    auto isSet( const char * name) -> bool
    {
        return std::getenv( name) != nullptr;
    }
} // namespace

auto fixtureSetup() -> bool
{
    announce( "setup");

    return !isSet( "THORIUM_FIXTURE_SETUP_FAILS");
}

auto fixtureTeardown() -> bool
{
    announce( "teardown");

    return !isSet( "THORIUM_FIXTURE_TEARDOWN_FAILS");
}

//
// The group-level pair, on the first group only (see hooked_catalog.inc). Same
// shape as the two above and steered the same way -- what differs is when the
// runner calls them, which is the thing under test rather than anything these
// bodies can express.
//
auto fixtureGroupSetup() -> bool
{
    announce( "group-setup");

    return !isSet( "THORIUM_FIXTURE_GROUP_SETUP_FAILS");
}

auto fixtureGroupTeardown() -> bool
{
    announce( "group-teardown");

    return !isSet( "THORIUM_FIXTURE_GROUP_TEARDOWN_FAILS");
}

//
// Unlike the hooks above, this records its outcome instead of returning it --
// a script's verdict is derived from the checks it made (see
// core::Journal::endTest). Which means the passing path has to make one: a
// script that records nothing cannot pass, and an empty body here would have
// turned every hook test into a failing run.
//
// The failing path uses core::Fail rather than a check contrived to be false,
// for the same reason a real script would: nothing here measured anything, and
// the reason is the only thing worth putting in the log.
//
auto fixtureScript() -> void
{
    announce( "script");

    if( isSet( "THORIUM_FIXTURE_SCRIPT_THROWS"))
    {
        throw std::runtime_error( "fixture script throwing on purpose");
    }

    if( isSet( "THORIUM_FIXTURE_SCRIPT_FAILS"))
    {
        core::Fail( "fixture script failing on purpose");

        return;
    }

    core::Verify( "fixture script ran", core::quantities::EQ( 1u), 1u);
}

//
// The second group's script. Deliberately reads none of the environment
// variables above: what it is for is being in the *other* group, so a run that
// selects it must show the first group's hooks not running -- and a marker that
// could also fail or throw would give the assertions a second thing to explain.
//
auto otherFixtureScript() -> void
{
    announce( "other-script");

    core::Verify( "other fixture script ran", core::quantities::EQ( 1u), 1u);
}
