#pragma once

#include <iosfwd>
#include <string_view>
#include <vector>

#include "core/journal/human_sink.hpp"
#include "core/journal/report.hpp"

namespace core
{
    //
    // The human-readable log, live, on the terminal -- the same content the RTF
    // file gets (see core/journal/report.hpp), painted with ANSI colour instead of an
    // RTF colour table, flushed after every line.
    //
    // This is what makes a run watchable while it is happening. The RTF file is
    // openable mid-run too (see core::RtfSink's own comment on how), but that
    // answers "let me go and look at the log"; this answers "let me watch the
    // rig", which is what an operator standing at the bench actually wants.
    //
    // It also replaces what core::Verify used to print directly to stdout:
    // the pass/fail line an operator saw is now one rendering of a journal
    // event rather than a separate, differently-worded report that happened
    // to be the only one anybody read.
    //
    class ConsoleSink : public HumanReportSink
    {
        public:
            //
            // colour = false writes the same lines with no escape sequences at
            // all, for a log being piped into a file or a CI job's output. Not
            // auto-detected from isatty(): that is a platform call this library
            // has no other reason to make, and the caller (framework/runner/src/main.cpp,
            // which owns the command line) is better placed to decide -- see
            // its --no-color flag.
            //
            explicit ConsoleSink( std::ostream & out, bool colour);

        private:
            //
            // The only thing this sink decides. Which events produce which
            // lines is core::HumanReportSink's, so that this and core::RtfSink
            // cannot come to disagree about it; a terminal has nothing to close
            // at the end of a run either, so finishDocument() stays defaulted.
            //
            auto writeAll( const std::vector<ReportLine> & lines) -> void override;

            std::ostream *  mOut;
            bool            mColour;
    };
} // namespace core
