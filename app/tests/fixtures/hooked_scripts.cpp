#include "hooked_scripts.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

//
// The suite behind run_scripts_hooked: one script and both run hooks, each
// announcing itself on stdout so an acceptance test can assert what ran, how
// often, and in what order -- none of which is visible from the logs alone,
// since a hook posts no journal event of its own.
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

auto fixtureScript() -> bool
{
    announce( "script");

    if( isSet( "THORIUM_FIXTURE_SCRIPT_THROWS"))
    {
        throw std::runtime_error( "fixture script throwing on purpose");
    }

    return !isSet( "THORIUM_FIXTURE_SCRIPT_FAILS");
}
