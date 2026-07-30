#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/journal.hpp"

namespace core
{
    //
    // The human-readable stream's *content*, decided once and rendered twice.
    //
    // There are two human sinks -- an RTF file (core/rtf_sink.hpp) and the
    // live console view (core/console_sink.hpp) -- and they must say the same
    // thing, because they are the same log: the console is what an operator
    // watches while the run is in progress, the RTF is what gets attached to
    // the report afterwards, and an operator who saw a line on screen that
    // isn't in the file has been misled. So which events appear, in what
    // words, in what order, is settled here; each sink only decides how to
    // paint an Emphasis (an RTF colour-table index, or an ANSI escape).
    //
    // This is also why the filtering rule lives here rather than in each sink:
    // the human stream carries Measure and Verify and nothing else -- the
    // sourcing and routing verbs (Apply/Remove/Connect/Disconnect) and the
    // safing pass go to the machine log only, where a reader is a tool
    // reconstructing the whole run rather than a person checking a rail. One
    // rule, one place, both sinks.
    //

    //
    // What a line means, not what it looks like. Deliberately semantic: a sink
    // maps Pass to whatever "passed" looks like in its medium, and neither
    // sink has an opinion about which events are worth emphasising.
    //
    enum class Emphasis
    {
        Plain,      // ordinary body text -- a measurement
        Heading,    // a run or test title
        Detail,     // supporting metadata, deliberately quieter than Plain
        Pass,
        Fail,
        Warning     // something a reader must notice that is not a failed check
    };

    struct ReportLine
    {
        Emphasis     Style{ Emphasis::Plain };
        std::string  Text;
    };

    //
    // The traceability header -- framework version, DUT, rig, criteria
    // variant, operator, host, start time, command line. Emitted by both human
    // sinks at run start, so a printed log is self-describing without needing
    // the machine log beside it (which is the whole point of stamping the same
    // RunInfo into both -- see core/journal.hpp's RunInfo).
    //
    [[nodiscard]]
    auto humanHeaderLines( const RunInfo & info) -> std::vector<ReportLine>;

    //
    // The test name, marked out as a heading on its own rather than folded
    // into the first measurement's line: a reader scanning a multi-test log
    // for the one that failed is looking for exactly this.
    //
    [[nodiscard]]
    auto humanTestHeadingLines( std::string_view group, std::string_view test, std::string_view description) -> std::vector<ReportLine>;

    //
    // One event's lines, or empty for an event the human stream doesn't carry
    // -- see this header's own comment on the filtering rule. Returns a vector
    // rather than a single line so a future event that wants a continuation
    // line (a multi-value reading, say) needs no signature change.
    //
    [[nodiscard]]
    auto humanEventLines( const JournalEvent & event) -> std::vector<ReportLine>;

    // The test's own verdict, restated after its checks -- Pass or Fail
    // emphasis, so it is findable by colour as well as by text.
    [[nodiscard]]
    auto humanTestResultLines( std::string_view group, std::string_view test, bool passed) -> std::vector<ReportLine>;

    [[nodiscard]]
    auto humanSummaryLines( bool allPassed) -> std::vector<ReportLine>;

    //
    // Whether the human stream carries this event at all. Exposed alongside
    // humanEventLines() (which already returns empty for the same events) so a
    // sink can skip the work of asking for lines it won't get, and so the rule
    // itself is testable without going through line formatting.
    //
    [[nodiscard]]
    auto isHumanRelevant( const JournalEvent & event) -> bool;
} // namespace core
