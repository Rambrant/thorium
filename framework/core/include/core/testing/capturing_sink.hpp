#pragma once

#include <vector>

#include "core/journal/journal.hpp"

namespace core
{
    //
    // A journal sink that keeps every event it is given, so a test can assert
    // on what a run *recorded* rather than only on what it returned.
    //
    // That distinction is the reason this exists at all, and it is not a
    // testing convenience -- it is the only way to check a whole class of
    // behaviour in this framework. A script returns nothing (see
    // core/catalog/test_catalog.hpp on why a TestScript takes no parameters
    // and hands nothing back), a verdict is *derived* from the checks a run
    // posted (core::Journal::endTest), and several verbs deliberately report
    // something the caller cannot otherwise see: a check that could not be
    // made still names its criterion (core::Fail), a Connect that moved a
    // contact on a live path says so, an unmeasurable reading carries the
    // instrument's reason beside the value. None of that is observable from a
    // return value. All of it is one event in this vector.
    //
    //     CapturingSink sink;
    //
    //     core::journal().clearSinks();
    //     core::journal().add( sink);
    //
    //     ... run the thing ...
    //
    //     ASSERT_EQ( sink.Events.size(), 1u);
    //     EXPECT_EQ( sink.Events.front().Method, core::Verb::Measure);
    //
    // ---------------------------------------------------------------------
    // Why this ships with core rather than living in core's own tests
    // ---------------------------------------------------------------------
    //
    // Because core's tests are not the only ones that need it. Ten copies of
    // this class existed before it, under four different names, and they were
    // spread across three separate test targets: core's own, the rig's
    // integration tests, and a suite's script tests. The last two are a
    // *deployment's* content, not the framework's -- a second rig testing a
    // second device brings its own rig/ and suite/ and reuses framework/
    // unchanged -- so a header under framework/core/tests/ would have been
    // reachable by exactly the one of the three that needed it least.
    //
    // So it is public, installed, and part of what a deployment gets: writing
    // tests for your own scripts is a thing this framework is for, and this is
    // the piece those tests cannot write for themselves without knowing how
    // core::IJournalSink dispatches.
    //
    // In core/testing/ rather than core/journal/ deliberately. Filed beside
    // ConsoleSink, RtfSink and SarifSink it would read as a fourth rendering
    // of the run, and the obvious question -- what does it render to? -- has
    // no answer. It renders to nothing; it is an assertion target. The group
    // it belongs to is "what a deployment's tests are written with", which is
    // this one.
    //
    // ---------------------------------------------------------------------
    // What it deliberately does not have
    // ---------------------------------------------------------------------
    //
    // No filtering, no per-verb accessors, no "the events of this test". Every
    // copy this replaces grew its own small helper on top -- the criteria a
    // run could not check, the subjects it named, the order its verdicts came
    // in -- and no two of them wanted the same one. Those stay where they are,
    // as free functions over Events beside the tests that ask them, because a
    // question asked once belongs next to the test asking it rather than in a
    // header every test reads.
    //
    // The events themselves are public rather than behind an accessor for the
    // same reason: what a test wants from them is a std::vector, and every
    // accessor that could be offered here is one std::ranges call at the call
    // site.
    //
    // Only onEvent is overridden. core::IJournalSink defaults every hook to an
    // empty body precisely so a sink that wants one of them writes one of them
    // (see its own comment) -- a test needing the run/group/test boundaries as
    // well wants a different sink, not a bigger version of this one, and
    // framework/core/tests/journal/test_journal.cpp has one.
    //
    class CapturingSink : public IJournalSink
    {
        public:
            auto onEvent( const JournalEvent & event) -> void override
            {
                Events.push_back( event);
            }

            std::vector<JournalEvent>  Events;
    };
} // namespace core
