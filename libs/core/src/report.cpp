#include "core/report.hpp"

#include <cstddef>
#include <iterator>

namespace core
{
    namespace
    {
        //
        // Column widths for the human log's fixed layout. A test log is read by
        // scanning a column, not by reading prose -- the value a reader wants
        // ("did this pass") should be in the same place on every line, so
        // padding is deliberate rather than cosmetic.
        //
        //
        // Wide enough for the longest label plus a separating space --
        // "Criteria variant" is 16 characters, so 16 would butt the value
        // straight up against it.
        //
        constexpr std::size_t kLabelWidth   = 18;   // metadata label column in the header
        constexpr std::size_t kVerbWidth    = 8;    // "measure" / "verify"
        constexpr std::size_t kSubjectWidth = 22;   // point name / criterion id
        constexpr std::size_t kValueWidth   = 14;   // "5.021 V"

        constexpr std::string_view kRule = "--------------------------------------------------------------------------";

        auto padded( const std::string_view text, const std::size_t width) -> std::string
        {
            std::string result( text);

            if( result.size() < width)
            {
                result.append( width - result.size(), ' ');
            }

            return result;
        }

        //
        // A header row, or nothing at all for a field this run has no value
        // for. Skipping rather than printing an empty value is the point: an
        // absent DutSerial means nobody told this run which unit was in the
        // fixture, and a row reading "Serial:" with nothing after it invites a
        // reader to assume the value was blank rather than never supplied.
        //
        auto metadataLine( const std::string_view label, const std::string_view value) -> std::vector<ReportLine>
        {
            if( value.empty())
            {
                return {};
            }

            return { ReportLine{ Emphasis::Detail, padded( label, kLabelWidth) + std::string( value) } };
        }

        auto append( std::vector<ReportLine> & into, std::vector<ReportLine> lines) -> void
        {
            into.insert( into.end(), std::make_move_iterator( lines.begin()), std::make_move_iterator( lines.end()));
        }

        auto testTitle( const std::string_view group, const std::string_view test) -> std::string
        {
            return std::string( group) + "::" + std::string( test);
        }
    } // namespace

    auto isHumanRelevant( const JournalEvent & event) -> bool
    {
        //
        // Measure and Verify only -- see core/report.hpp on why the rule lives
        // here. Note and Warning-carrying events are deliberately excluded
        // too: a Note is how the runner records something for the machine log
        // (an uncaught exception's text, say), and the console has already
        // shown that to the operator by other means.
        //
        return event.Method == Verb::Measure || event.Method == Verb::Verify;
    }

    auto humanHeaderLines( const RunInfo & info) -> std::vector<ReportLine>
    {
        std::vector<ReportLine> lines;

        lines.push_back( ReportLine{ Emphasis::Heading,
            info.FrameworkName + " " + info.FrameworkVersion + " -- test run log" });

        append( lines, metadataLine( "DUT",              info.DutName));
        append( lines, metadataLine( "DUT serial",       info.DutSerial));
        append( lines, metadataLine( "Rig",              info.RigName));
        append( lines, metadataLine( "Criteria variant", info.CriteriaVariant));
        append( lines, metadataLine( "Operator",         info.Operator));
        append( lines, metadataLine( "Host",             info.HostName));
        append( lines, metadataLine( "Started (UTC)",    info.StartedUtc));
        append( lines, metadataLine( "Command line",     info.CommandLine));

        lines.push_back( ReportLine{ Emphasis::Detail, std::string( kRule) });

        return lines;
    }

    auto humanTestHeadingLines( const std::string_view group, const std::string_view test, const std::string_view description) -> std::vector<ReportLine>
    {
        std::vector<ReportLine> lines;

        // Blank line first, so consecutive tests don't run together.
        lines.push_back( ReportLine{ Emphasis::Plain, {} });
        lines.push_back( ReportLine{ Emphasis::Heading, "TEST  " + testTitle( group, test) });

        if( !description.empty())
        {
            lines.push_back( ReportLine{ Emphasis::Detail, "      " + std::string( description) });
        }

        return lines;
    }

    auto humanEventLines( const JournalEvent & event) -> std::vector<ReportLine>
    {
        if( !isHumanRelevant( event))
        {
            return {};
        }

        if( event.Method == Verb::Measure)
        {
            //
            // "measure  Output5V  5.021 V  (Dmm1)  5Vdc supply port" -- the
            // reading first, since that is what a reader is checking, and the
            // instrument and the point's own description after it as
            // supporting context.
            //
            auto text = padded( "measure", kVerbWidth)
                      + padded( event.Subject, kSubjectWidth)
                      + padded( event.Value, kValueWidth);

            if( !event.Instrument.empty())
            {
                text += "(" + event.Instrument + ")";
            }

            if( !event.Detail.empty())
            {
                text += "  " + event.Detail;
            }

            return { ReportLine{ Emphasis::Plain, "  " + text } };
        }

        //
        // A Verify line ends in its verdict, in the verdict's own colour --
        // both, not either: colour alone is lost the moment the log is printed
        // in black and white or read by somebody who can't distinguish red
        // from green, and the bracketed word alone is what a reader has to
        // hunt for down a long column.
        //
        const bool passed = event.Passed.value_or( false);

        auto text = padded( "verify", kVerbWidth)
                  + padded( event.Subject, kSubjectWidth)
                  + padded( event.Value, kValueWidth)
                  + ( passed ? "[PASS]" : "[FAIL]");

        if( !event.Detail.empty())
        {
            text += "  " + event.Detail;
        }

        return { ReportLine{ passed ? Emphasis::Pass : Emphasis::Fail, "  " + text } };
    }

    auto humanTestResultLines( const std::string_view group, const std::string_view test, const bool passed) -> std::vector<ReportLine>
    {
        return { ReportLine{ passed ? Emphasis::Pass : Emphasis::Fail,
            "  " + padded( "RESULT", kVerbWidth) + padded( testTitle( group, test), kSubjectWidth + kValueWidth)
                 + ( passed ? "[PASS]" : "[FAIL]") } };
    }

    auto humanSummaryLines( const bool allPassed) -> std::vector<ReportLine>
    {
        return {
            ReportLine{ Emphasis::Plain, {} },
            ReportLine{ Emphasis::Detail, std::string( kRule) },
            ReportLine{ allPassed ? Emphasis::Pass : Emphasis::Fail,
                allPassed ? "ALL SCRIPTS PASSED" : "SOME SCRIPTS FAILED" }
        };
    }
} // namespace core
