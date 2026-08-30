#include "core/journal/human_sink.hpp"

namespace core
{
    //
    // Eight hooks, each one line: ask core/journal/report.hpp what this moment
    // in the run reads as, and hand the answer to the medium. The whole of the
    // human stream's dispatch is this file -- see core::HumanReportSink on why
    // it is one file rather than one per sink.
    //
    auto HumanReportSink::onRunStart( const RunInfo & info) -> void
    {
        writeAll( humanHeaderLines( info));
    }

    auto HumanReportSink::onGroupStart( const std::string_view group, const std::string_view description) -> void
    {
        writeAll( humanGroupHeadingLines( group, description));
    }

    auto HumanReportSink::onTestStart( const std::string_view test, const std::string_view description) -> void
    {
        writeAll( humanTestHeadingLines( test, description));
    }

    auto HumanReportSink::onPhaseStart(
        const std::string_view  group,
        const std::string_view  phase,
        const std::string_view  title) -> void
    {
        writeAll( humanPhaseHeadingLines( group, phase, title));
    }

    //
    // The phase's id is not passed on, and that is report.hpp's decision
    // rather than an omission here: what closes a hook's block is the same
    // blank line whichever hook it was, because the heading above it has
    // already said which (see core::humanPhaseClosingLines).
    //
    auto HumanReportSink::onPhaseEnd( std::string_view) -> void
    {
        writeAll( humanPhaseClosingLines());
    }

    auto HumanReportSink::onEvent( const JournalEvent & event) -> void
    {
        writeAll( humanEventLines( event));
    }

    //
    // The group is not passed on either: a test's result line sits inside its
    // group's block, which has already named it -- the same nesting rule
    // humanTestHeadingLines follows.
    //
    auto HumanReportSink::onTestEnd( std::string_view, const std::string_view test, const bool passed) -> void
    {
        writeAll( humanTestResultLines( test, passed));
    }

    //
    // The one hook that is two steps. finishDocument() is where a sink that
    // has a document to close closes it -- and it runs after the summary
    // rather than instead of it, so the last thing in the file is the run's
    // verdict.
    //
    auto HumanReportSink::onRunEnd( const bool allPassed) -> void
    {
        writeAll( humanSummaryLines( allPassed));

        finishDocument();
    }
} // namespace core
