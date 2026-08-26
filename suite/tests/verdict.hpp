#pragma once

#include "core/journal.hpp"

//
// What a script's own test needs now that a script returns nothing.
//
// A script's verdict is derived from the checks it recorded, by the journal, at
// the point the test bracket closes (see core::Journal::endTest and
// core/test_catalog.hpp). So a test that calls a script directly has to do what
// framework/runner/src/main.cpp's runner does around it -- open a bracket, run it, close the
// bracket and take the answer -- and this is that, once, rather than three lines
// copied into every fixture in this directory.
//
// Two things worth knowing about testing a script this way:
//
//   - The rule that a script recording no check at all cannot pass reaches
//     these tests too, because the rule lives in endTest rather than in the
//     runner. A test that stubs out every reading a script takes gets a
//     failure, not a vacuous pass.
//
//   - Nothing here registers a sink. The verdict is the journal's own
//     arithmetic over what was posted, so a test that only wants the outcome
//     needs no recording sink -- only one that wants to assert on the *rows*
//     does (test_console_script.cpp and test_ac_dropout_script.cpp both do).
//
// A template rather than a plain function pointer parameter, so a test can hand
// over a lambda that sets a variant or injects readings before calling the
// script -- which two of them do.
//
// Not in a namespace, matching the scripts it wraps (see suite/scripts.hpp on
// why those are at global scope) -- and a header rather than a .cpp because
// add_layer_tests globs *.cpp, so a file here would become a test target of its
// own with no tests in it.
//
template<typename Script>
[[nodiscard]]
auto verdictOf( Script && script) -> bool
{
    core::journal().beginTest( "ScriptUnderTest", "A script run directly by its own unit test");

    script();

    return core::journal().endTest();
}
