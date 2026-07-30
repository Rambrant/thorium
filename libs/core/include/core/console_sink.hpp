#pragma once

#include <iosfwd>
#include <string_view>
#include <vector>

#include "core/journal.hpp"
#include "core/report.hpp"

namespace core
{
    //
    // The human-readable log, live, on the terminal -- the same content the RTF
    // file gets (see core/report.hpp), painted with ANSI colour instead of an
    // RTF colour table, flushed after every line.
    //
    // This is what makes a run watchable while it is happening. The RTF file is
    // openable mid-run too (see core::RtfSink's own comment on how), but that
    // answers "let me go and look at the log"; this answers "let me watch the
    // rig", which is what an operator standing at the bench actually wants.
    //
    // It also replaces what core::Verify used to print directly through
    // core::Logger: the pass/fail line an operator saw on stdout is now one
    // rendering of a journal event rather than a separate, differently-worded
    // report that happened to be the only one anybody read. core::Logger is
    // untouched and still the right tool for diagnostics that are not part of
    // the test record.
    //
    class ConsoleSink : public IJournalSink
    {
        public:
            //
            // colour = false writes the same lines with no escape sequences at
            // all, for a log being piped into a file or a CI job's output. Not
            // auto-detected from isatty(): that is a platform call this library
            // has no other reason to make, and the caller (app/src/main.cpp,
            // which owns the command line) is better placed to decide -- see
            // its --no-color flag.
            //
            explicit ConsoleSink( std::ostream & out, bool colour);

            auto onRunStart( const RunInfo & info) -> void override;
            auto onTestStart( std::string_view group, std::string_view test, std::string_view description) -> void override;
            auto onEvent( const JournalEvent & event) -> void override;
            auto onTestEnd( std::string_view group, std::string_view test, bool passed) -> void override;
            auto onRunEnd( bool allPassed) -> void override;

        private:
            auto writeAll( const std::vector<ReportLine> & lines) -> void;

            std::ostream *  mOut;
            bool            mColour;
    };
} // namespace core
