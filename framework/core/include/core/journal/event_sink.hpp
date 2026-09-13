#pragma once

#include <iosfwd>

#include "core/journal/journal.hpp"

namespace core
{
    //
    // The run as a stream of JSON objects, one per line, flushed as each one
    // happens -- what a supervising process (ui/, a CI agent, a bench console)
    // reads to watch a run it started.
    //
    // This is the third rendering of the journal and needs no change to any
    // verb to exist, which is the property core/journal/journal.hpp claims for
    // the design and this is the first thing to actually test it.
    //
    // -------------------------------------------------------------------
    // Why not just read the SARIF log
    // -------------------------------------------------------------------
    // Because core::SarifSink buffers every event and writes the document at
    // onRunEnd -- it has to, since a SARIF run is one JSON object and its
    // tool.driver.rules array is not known until the last result is in. A
    // supervisor reading that file learns what happened *after* it happened,
    // which is precisely the thing a live view cannot be built on. A fifty-pass
    // soak would show nothing for an hour and then everything at once.
    //
    // So the format is the opposite of SARIF's on every axis, and each choice
    // is forced by that one requirement:
    //
    //   - one object per line, not one document -- a reader can act on a line
    //     the moment it arrives, with no incremental JSON parser and no
    //     recovery problem if the run is killed half way through. A truncated
    //     stream is a whole number of valid events plus, at worst, one partial
    //     line to discard. A truncated SARIF file is nothing at all.
    //   - flushed per line, not per buffer -- a UI showing a rail voltage four
    //     kilobytes late is showing the wrong rail.
    //   - every hook emitted, including the boundaries -- a live view is
    //     drawing the catalog's shape as the run walks it, so onGroupStart and
    //     onTestEnd are events here rather than something to be re-derived.
    //
    // It is deliberately NOT an archival format and nothing in this tree reads
    // it back. The two logs are the record of a run (core/journal/rtf_sink.hpp,
    // core/journal/sarif_sink.hpp) and the recording is the replayable data
    // (core/session/recording.hpp); this is the wire a watcher listens on while
    // the run is in progress, and a run with no watcher does not write one.
    //
    // -------------------------------------------------------------------
    // The schema
    // -------------------------------------------------------------------
    // Every line is an object with a "kind", and the kind decides the rest:
    //
    //   {"kind":"runStart","info":{...}}        the whole RunInfo, incl.
    //                                           instruments and benchAttached
    //   {"kind":"groupStart","group":..,"description":..}
    //   {"kind":"groupEnd","group":..}
    //   {"kind":"testStart","test":..,"description":..}
    //   {"kind":"testEnd","group":..,"test":..,"passed":bool}
    //   {"kind":"phaseStart","group":..,"phase":..,"title":..}
    //   {"kind":"phaseEnd","phase":..}
    //   {"kind":"event", <every JournalEvent field that is set>}
    //   {"kind":"runEnd","allPassed":bool}
    //
    // Empty strings and unset optionals are omitted rather than written as ""
    // or null, for the reason core::SarifSink omits them: a reader cannot tell
    // "this event has no instrument" from "this event's instrument is the empty
    // string" if both are spelled the same, and the first is the common case.
    //
    // "kind" is first on every line on purpose. A reader that only cares about
    // verdicts can decide whether to parse the rest from the first twenty
    // bytes, and a person tailing the stream can read it.
    //
    class EventSink : public IJournalSink
    {
        public:
            //
            // The stream is referenced, not owned -- the caller holds it, the
            // way it holds every other sink (see framework/runner/src/main.cpp).
            // That matters more here than elsewhere: the stream is usually the
            // process's own stdout, which this must not close.
            //
            explicit EventSink( std::ostream & out);

        private:
            auto onRunStart( const RunInfo & info) -> void override;
            auto onGroupStart( std::string_view group, std::string_view description) -> void override;
            auto onGroupEnd( std::string_view group) -> void override;
            auto onTestStart( std::string_view test, std::string_view description) -> void override;
            auto onPhaseStart( std::string_view group, std::string_view phase, std::string_view title) -> void override;
            auto onPhaseEnd( std::string_view phase) -> void override;
            auto onEvent( const JournalEvent & event) -> void override;
            auto onTestEnd( std::string_view group, std::string_view test, bool passed) -> void override;
            auto onRunEnd( bool allPassed) -> void override;

            std::ostream *  mOut;
    };
} // namespace core
