#pragma once

#include <string_view>
#include <vector>

#include "core/journal/journal.hpp"
#include "core/journal/report.hpp"

namespace core
{
    //
    // The human-readable stream's *dispatch*, written once and inherited
    // twice. core/journal/report.hpp is the other half: it settles what the
    // stream says, this settles when each piece of it is said.
    //
    // The pair of them is one idea split by what varies. Which events appear,
    // in what words and what order, is a property of the log (report.hpp).
    // Which journal hook asks for which of those lines is a property of the
    // journal, and identical for every sink that renders the log at all. What
    // is genuinely per-sink is only the last step -- painting an Emphasis as
    // an ANSI escape or an RTF colour-table index, and putting the result
    // somewhere -- which is writeAll() below and nothing else.
    //
    // Before this existed, core::ConsoleSink and core::RtfSink each carried
    // all eight overrides, and all eight were the same line twice:
    //
    //     auto ConsoleSink::onGroupStart( group, description) -> void
    //     { writeAll( humanGroupHeadingLines( group, description)); }
    //
    //     auto RtfSink::onGroupStart( group, description) -> void
    //     { writeAll( humanGroupHeadingLines( group, description)); }
    //
    // That is not duplication worth removing for its length -- sixteen
    // forwarding bodies is nothing to read. It is worth removing because of
    // what it does when the log grows. report.hpp gains a
    // humanGroupClosingLines(), or IJournalSink gains a ninth hook, and the
    // edit has to land in two places that no compiler and no test relates to
    // each other: wiring it into one sink and not the other produces a run
    // whose console and whose RTF disagree about what happened, silently, in
    // exactly the direction report.hpp's own comment says must never happen --
    // "an operator who saw a line on screen that isn't in the file has been
    // misled". One dispatch means there is no second place to forget.
    //
    // ---------------------------------------------------------------------
    // Why the eight are final
    // ---------------------------------------------------------------------
    //
    // A subclass overriding one of them would reintroduce precisely the drift
    // this removes, and it would do it in the least visible way available: one
    // hook out of eight, in one sink out of two, still compiling and still
    // passing every test that looks at either sink on its own.
    //
    // Nothing is given up by forbidding it. Every hook funnels through
    // writeAll(), so a sink wanting to count events, filter them, or wrap them
    // has one place to do it that cannot miss a hook; and a sink whose *content*
    // should differ is not a human sink at all -- it is a second stream, and it
    // derives from core::IJournalSink directly the way core::SarifSink does.
    //
    class HumanReportSink : public IJournalSink
    {
        public:
            auto onRunStart( const RunInfo & info) -> void final;
            auto onGroupStart( std::string_view group, std::string_view description) -> void final;
            auto onTestStart( std::string_view test, std::string_view description) -> void final;
            auto onPhaseStart( std::string_view group, std::string_view phase, std::string_view title) -> void final;
            auto onPhaseEnd( std::string_view phase) -> void final;
            auto onEvent( const JournalEvent & event) -> void final;
            auto onTestEnd( std::string_view group, std::string_view test, bool passed) -> void final;
            auto onRunEnd( bool allPassed) -> void final;

        protected:
            //
            // Render these lines, in this medium. The one thing a human sink
            // actually has to decide.
            //
            // Called with an empty vector for an event the human stream does
            // not carry (see core::isHumanRelevant), rather than being skipped
            // here: both sinks already return early on empty, and each has its
            // own reason to -- the console would emit a bare flush, the RTF
            // would rewrite its trailer for a verb it did not log. Leaving the
            // check with them keeps that reason where it applies instead of
            // asserting one shared justification for two different costs.
            //
            virtual auto writeAll( const std::vector<ReportLine> & lines) -> void = 0;

            //
            // The run is over and the last line is written. Empty by default:
            // a console has nothing to close.
            //
            // core::RtfSink does -- an RTF document is one brace-delimited
            // group, and until this is called the closing brace is being
            // rewritten after every flush so the file is readable mid-run (see
            // that class's own comment). This is where it stops being rewritten
            // and becomes final.
            //
            virtual auto finishDocument() -> void {}
    };
} // namespace core
