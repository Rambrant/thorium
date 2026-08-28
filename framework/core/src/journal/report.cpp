#include "core/journal/report.hpp"

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>

namespace core
{
    namespace
    {
        //
        // The body's one level of nesting -- a literal tab, see
        // core/journal/report.hpp on why a tab rather than spaces.
        //
        constexpr std::string_view kIndent = "\t";

        //
        // Column widths for the fixed layout *inside* a line. A test log is read
        // by scanning a column, not by reading prose -- the value a reader wants
        // ("did this pass", "what was required") should be in the same place on
        // every line, so padding is deliberate rather than cosmetic.
        //
        // kSubjectWidth fits a fully qualified criterion ("FS_Supply_1::
        // FS_Supply_5V0") without pushing every measurement line out to match a
        // width only the checks need.
        //
        constexpr std::size_t kLabelWidth    = 18;   // metadata label column in the header
        constexpr std::size_t kVerbWidth     = 8;    // "measure" / "verify"
        constexpr std::size_t kSubjectWidth  = 28;   // point name / group::criterion
        constexpr std::size_t kValueWidth    = 12;   // "5.021 V"
        constexpr std::size_t kRequiredWidth = 24;   // "= 5 V +/-0.05 V"

        constexpr std::string_view kRule = "--------------------------------------------------------------------------";

        //
        // A verb's enumerator spelling as the human log writes it -- "Measure"
        // becomes "measure". ASCII-only by construction: the input is always a
        // core::Verb enumerator name, so there is no locale, no multi-byte
        // character and nothing for std::tolower's notorious signed-char trap
        // to catch.
        //
        auto lowercased( const std::string_view text) -> std::string
        {
            std::string result;

            result.reserve( text.size());

            for( const auto character : text)
            {
                result += ( character >= 'A' && character <= 'Z')
                              ? static_cast<char>( character - 'A' + 'a')
                              : character;
            }

            return result;
        }

        //
        // A whole line in one emphasis -- most lines are still this shape.
        //
        auto line( const Emphasis style, std::string text) -> ReportLine
        {
            return ReportLine{ { ReportSpan{ style, std::move( text) } } };
        }

        //
        // A line whose trailing description is rendered quietly -- see
        // ReportSpan's own comment in core/journal/report.hpp. The description span is
        // omitted entirely when there is none, rather than added empty, so a
        // sink never emits a colour change for nothing.
        //
        auto describedLine( const Emphasis style, std::string text, const std::string_view description) -> ReportLine
        {
            if( description.empty())
            {
                return line( style, std::move( text));
            }

            return ReportLine{ {
                ReportSpan{ style,             std::move( text) },
                ReportSpan{ Emphasis::Detail,  std::string( description) }
            } };
        }

        auto blankLine() -> ReportLine
        {
            return ReportLine{};
        }

        //
        // Pads to width -- and, when the text is already at or past it, still
        // emits a single space. Without that last part an over-long entry runs
        // straight into the next column ("...at nominal28 V"), which reads as a
        // corrupt value rather than as a wide field.
        //
        // Truncating instead was the alternative and is worse: these columns
        // hold criterion ids, DUT point names and the prose of an ad-hoc check
        // (see core/criteria/verify.hpp's three-argument Verify), and a report that
        // quietly shortens the name of the thing being reported on is a report
        // a reader cannot search. A ragged line is legible; a clipped
        // identifier is a wrong answer.
        //
        auto padded( const std::string_view text, const std::size_t width) -> std::string
        {
            std::string result( text);

            result.append( result.size() < width ? width - result.size() : 1u, ' ');

            return result;
        }

        //
        // "<name> <description>" as a heading plus a quiet description. Shared by
        // the group and test headings, so the two levels are spelled the same way
        // and can't drift apart.
        //
        auto titleLine( const std::string_view indent, const std::string_view name, const std::string_view description) -> ReportLine
        {
            const auto separator = description.empty() ? "" : " ";

            return describedLine( Emphasis::Heading,
                                  std::string( indent) + std::string( name) + separator,
                                  description);
        }

        //
        // A header row, always emitted -- including with nothing after the
        // label when the run has no value for it.
        //
        // This reverses an earlier choice here (skip the row entirely) on
        // purpose. The header is a report form, not a summary: its rows are the
        // set of facts a test record is required to carry, and a form with a
        // field left blank says "nobody filled this in", which is itself the
        // finding. An omitted row says nothing at all, and leaves two runs of
        // the same suite with different-shaped headers -- which is exactly what
        // makes two reports awkward to compare side by side.
        //
        auto metadataLine( const std::string_view label, const std::string_view value) -> ReportLine
        {
            return line( Emphasis::Detail, padded( label, kLabelWidth) + std::string( value));
        }

        //
        // Whether the run reached a bench, as a header row of its own.
        //
        // Always present, like every other row here -- the header is a report
        // form, and a form whose fields appear only sometimes cannot be
        // compared between two runs (see metadataLine's own comment). So an
        // ordinary run says so too, which is the reassurance half: "attached"
        // is the row that says these readings came off hardware.
        //
        // Emphasis::Warning when detached, where every other metadata row is
        // quiet -- "something a reader must notice that is not a failed check"
        // is exactly what this is. A detached run's checks can all pass, and
        // what they passed about is a file.
        //
        auto benchLine( const bool attached) -> ReportLine
        {
            if( attached)
            {
                return metadataLine( "Bench", "attached");
            }

            return line( Emphasis::Warning,
                         padded( "Bench", kLabelWidth) + "DETACHED -- no instrument was touched");
        }

        //
        // The content revisions, as one row when suite/, dut/ and rig/ agree and
        // three when they don't.
        //
        // They agree whenever all three come from one repository, which is the
        // normal case (and this repo's) -- printing the same revision three
        // times there would be three lines of noise for one fact. They diverge
        // once a rig repo pulls this framework in with its own dut/ or suite/,
        // and then each one matters separately, which is why the collapse is
        // conditional rather than a single field.
        //
        auto versionLines( const RunInfo & info) -> std::vector<ReportLine>
        {
            if( info.SuiteVersion == info.DutVersion && info.DutVersion == info.RigVersion)
            {
                return { metadataLine( "Suite/DUT/rig", info.SuiteVersion) };
            }

            return {
                metadataLine( "Suite version", info.SuiteVersion),
                metadataLine( "DUT version",   info.DutVersion),
                metadataLine( "Rig version",   info.RigVersion)
            };
        }

        auto append( std::vector<ReportLine> & into, std::vector<ReportLine> lines) -> void
        {
            into.insert( into.end(), std::make_move_iterator( lines.begin()), std::make_move_iterator( lines.end()));
        }

        //
        // A criterion as the log names it: its CRITERIA group and its CRIT id,
        // qualified. Both, not just the id, because the id alone is only unique
        // within its group -- and because "FS_Supply_1::FS_Supply_5V0" is the
        // spelling a test spec traces to and the one a reader can search a
        // criteria file for.
        //
        auto qualifiedSubject( const JournalEvent & event) -> std::string
        {
            if( event.SubjectGroup.empty())
            {
                return event.Subject;
            }

            return event.SubjectGroup + "::" + event.Subject;
        }
    } // namespace

    auto plainText( const ReportLine & reportLine) -> std::string
    {
        std::string result;

        for( const auto & span : reportLine.Spans)
        {
            result += span.Text;
        }

        return result;
    }

    auto isHumanRelevant( const JournalEvent & event) -> bool
    {
        //
        // What the run *observed*, and the checks made on it -- see
        // core/journal/report.hpp on why the rule lives here. Note and Warning-carrying
        // events are deliberately excluded too: a Note is how the runner
        // records something for the machine log (an uncaught exception's text,
        // say), and the console has already shown that to the operator by other
        // means.
        //
        // Read is here for the same reason Measure is, and the boundary is
        // worth stating because the serial verbs straddle it. A Read is an
        // observation: a value that came back off the DUT, which a criterion is
        // about to be checked against, and which a reader of the human log has
        // to see to follow the verdict beneath it. Setup and Write are
        // stimulus -- what the bench was told to do -- and belong with Apply
        // and Connect on the machine stream only.
        //
        // That does mean a reply appears without the command that provoked it,
        // which looks like an omission and is not: a Measure already appears
        // without the Apply that brought its rail up, for exactly the same
        // reason. The human log answers "what did the DUT do"; the SARIF stream
        // carries every verb in order for anyone asking "what did the bench do"
        // (see core/journal/sarif_sink.hpp).
        //
        // Await sits on the observation side of that same line, and its
        // partner Arm on the stimulus side (see core/verbs/acquire.hpp). Await earns
        // its place here rather than being one more thing the bench was told
        // to do: a transient measured after a capture that timed out is not a
        // measurement of the transient at all, so a reader following the
        // verdict below it has to be able to see whether the capture landed.
        //
        //
        // Fetch is here for Read's reason: a trace that came back is a value a
        // reader is checking the verdict below against, and its summary line
        // says how long the record was and how far it swung -- which is what
        // tells them the capture was the one the test meant.
        //
        return event.Method == Verb::Measure
            || event.Method == Verb::Read
            || event.Method == Verb::Await
            || event.Method == Verb::Fetch
            || event.Method == Verb::Verify;
    }

    auto humanHeaderLines( const RunInfo & info) -> std::vector<ReportLine>
    {
        std::vector<ReportLine> lines;

        //
        // The title says which unit was tested and when, in that order, because
        // those are the two things somebody holding a stack of reports is
        // looking for. Local time here rather than UTC: this line is read by
        // the person who was in the room. The UTC instant is a row below, for
        // whoever is comparing runs across benches or time zones -- both are
        // the same reading (see RunInfo).
        //
        lines.push_back( line( Emphasis::Heading,
            info.DutName + " -- " + info.StartedLocal));

        append( lines, { metadataLine( "DUT serial", info.DutSerial) });
        append( lines, { metadataLine( "Operator",   info.Operator) });
        append( lines, { metadataLine( "Criteria",   info.CriteriaVariant) });
        append( lines, { benchLine( info.BenchAttached) });
        append( lines, { metadataLine( "Framework",  info.FrameworkName + " " + info.FrameworkVersion) });
        append( lines, versionLines( info));
        append( lines, { metadataLine( "Started (UTC)", info.StartedUtc) });
        append( lines, { metadataLine( "Command line",  info.CommandLine) });

        lines.push_back( line( Emphasis::Detail, std::string( kRule)));
        lines.push_back( blankLine());

        return lines;
    }

    auto humanGroupHeadingLines( const std::string_view group, const std::string_view description) -> std::vector<ReportLine>
    {
        // Unindented -- the outer level of the body's two.
        return { titleLine( {}, group, description) };
    }

    auto humanTestHeadingLines( const std::string_view test, const std::string_view description) -> std::vector<ReportLine>
    {
        return { titleLine( kIndent, test, description) };
    }

    auto humanPhaseHeadingLines( const std::string_view group, const std::string_view phase, const std::string_view title) -> std::vector<ReportLine>
    {
        return { titleLine( group.empty() ? std::string_view{} : kIndent, phase, title) };
    }

    auto humanPhaseClosingLines() -> std::vector<ReportLine>
    {
        return { blankLine() };
    }

    auto humanEventLines( const JournalEvent & event) -> std::vector<ReportLine>
    {
        if( !isHumanRelevant( event))
        {
            return {};
        }

        if( event.Method == Verb::Measure || event.Method == Verb::Read ||
            event.Method == Verb::Await   || event.Method == Verb::Fetch)
        {
            //
            // "measure Output5V  5.021 V  (Dmm1)  5Vdc supply port" -- the
            // reading first, since that is what a reader is checking, and the
            // instrument and the point's own description after it as supporting
            // context.
            //
            // A Read renders through this same branch, and lines up in the same
            // columns, because it is the same kind of line: a value that came
            // back, from an instrument, about a named thing. Only the verb
            // differs -- "read Console  \"0xF5\\r\"  (Ser1)" -- so the two
            // never need to be told apart by shape when a script does both.
            // An Await is the same shape a third time: "await
            // Osc1.Acquisition  complete  (Osc1)".
            //
            // Padded to the same verb/subject/value columns a verify line uses,
            // so a reading and the check against it line up rather than
            // staggering.
            //
            // The verb text comes from the enumerator rather than from a
            // ternary chain that grows a branch per verb -- lowercased because
            // these lines are prose, where core::to_string(Verb) answers for
            // the machine stream and wants the enumerator's own spelling.
            //
            auto text = padded( lowercased( to_string( event.Method)), kVerbWidth)
                      + padded( event.Subject, kSubjectWidth)
                      + padded( event.Value, kValueWidth);

            if( !event.Instrument.empty())
            {
                text += "(" + event.Instrument + ")";
            }

            if( !event.Detail.empty())
            {
                text += "  ";
            }

            return { describedLine( Emphasis::Plain, std::string( kIndent) + text, event.Detail) };
        }

        //
        // A check states four things, in this order: which criterion, what was
        // measured, what was required, and the verdict.
        //
        // "What was required" is the criterion's own predicate, rendered (see
        // core/criteria/predicate_text.hpp) -- not its description. That distinction is
        // the point: the description is prose somebody wrote, and nothing checks
        // it against the tolerance actually enforced, so a log carrying only the
        // description cannot be used to confirm the judgement. With both, a
        // reader sees "measured 0 V, required = 5 V +/-0.05 V" and needs no
        // second file open to agree with the [FAIL].
        //
        // The verdict is stated in words as well as in colour: colour alone is
        // lost the moment the log is printed in black and white or read by
        // somebody who can't distinguish red from green, and the bracketed word
        // alone is what a reader has to hunt for down a long column.
        //
        const bool passed = event.Passed.value_or( false);

        auto text = padded( "verify", kVerbWidth)
                  + padded( qualifiedSubject( event), kSubjectWidth)
                  + padded( event.Value, kValueWidth)
                  + padded( event.CriterionText, kRequiredWidth)
                  + ( passed ? "[PASS]" : "[FAIL]");

        if( !event.Detail.empty())
        {
            text += "  ";
        }

        return { describedLine( passed ? Emphasis::Pass : Emphasis::Fail, std::string( kIndent) + text, event.Detail) };
    }

    auto humanTestResultLines( const std::string_view test, const bool passed) -> std::vector<ReportLine>
    {
        return {
            line( passed ? Emphasis::Pass : Emphasis::Fail,
                std::string( kIndent) + padded( "RESULT", kVerbWidth)
                                      + padded( test, kSubjectWidth + kValueWidth + kRequiredWidth)
                                      + ( passed ? "[PASS]" : "[FAIL]")),

            // Closes this test's block -- see core/journal/report.hpp on why the blank
            // trails rather than leads.
            blankLine()
        };
    }

    auto humanSummaryLines( const bool allPassed) -> std::vector<ReportLine>
    {
        return {
            line( Emphasis::Detail, std::string( kRule)),
            line( allPassed ? Emphasis::Pass : Emphasis::Fail,
                  allPassed ? "ALL SCRIPTS PASSED" : "SOME SCRIPTS FAILED")
        };
    }
} // namespace core
