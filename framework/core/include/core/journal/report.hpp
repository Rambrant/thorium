#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/journal/journal.hpp"

namespace core
{
    //
    // The human-readable stream's *content*, decided once and rendered twice.
    //
    // There are two human sinks -- an RTF file (core/journal/rtf_sink.hpp) and the
    // live console view (core/journal/console_sink.hpp) -- and they must say the same
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
        Heading,    // a group or test title
        Detail,     // supporting prose, deliberately quieter than Plain
        Pass,
        Fail,
        Warning     // something a reader must notice that is not a failed check
    };

    //
    // A run of text with one emphasis. A line is a sequence of these rather than
    // a single styled string, because the two things on most lines want
    // different weight: the fact (a reading, a verdict, a name) and the prose
    // that explains it (a point's or criterion's description).
    //
    // Those descriptions are the bulk of the characters on a line and the least
    // of its information -- they are fixed text from dut/adapter.inc and the
    // criteria tables, identical on every run. Rendering them as quietly as the
    // header metadata (Emphasis::Detail) is what lets a reader's eye travel down
    // the values and verdicts without the descriptions competing, while still
    // having them there to read when a line needs explaining.
    //
    struct ReportSpan
    {
        Emphasis     Style{ Emphasis::Plain };
        std::string  Text;
    };

    struct ReportLine
    {
        std::vector<ReportSpan> Spans;
    };

    //
    // The line's text with its styling dropped -- what it reads as. For anything
    // that wants the content rather than the presentation: an assertion, a
    // plain-text sink, a grep.
    //
    [[nodiscard]]
    auto plainText( const ReportLine & line) -> std::string;

    //
    // The traceability header -- framework version, DUT, rig, criteria
    // variant, operator, host, start time, command line. Emitted by both human
    // sinks at run start, so a printed log is self-describing without needing
    // the machine log beside it (which is the whole point of stamping the same
    // RunInfo into both -- see core/journal/journal.hpp's RunInfo).
    //
    [[nodiscard]]
    auto humanHeaderLines( const RunInfo & info) -> std::vector<ReportLine>;

    //
    // The log's body is two levels deep, matching the catalog's own shape:
    //
    //     <hook id> <hook title>              -- the catalog's RUN_ pair
    //         <log output>
    //
    //     <group name> <group description>
    //         <hook id> <hook title>          -- that group's own pair
    //         <log output>
    //
    //         <test name> <test description>
    //         <log output>
    //
    //         <test name> <test description>
    //         <log output>
    //
    // The group states itself once and its tests nest under it, rather than
    // every test line repeating "Group::Test" -- which is what a reader
    // scanning a multi-group run actually wants, and what makes the group's
    // own description (straight from its GROUP entry, see
    // core/catalog/test_catalog.hpp) have somewhere to appear at all.
    //
    // The indent is a literal tab, so the nesting survives whatever the
    // reader's medium does with it -- an RTF tab stop, a terminal's tab width,
    // a paste into a report. Column alignment *within* a line is still spaces
    // (see report.cpp's width constants); a tab can't align a column.
    //
    [[nodiscard]]
    auto humanGroupHeadingLines( std::string_view group, std::string_view description) -> std::vector<ReportLine>;

    //
    // The test name, marked out as a heading on its own rather than folded
    // into the first measurement's line: a reader scanning for the test that
    // failed is looking for exactly this. Takes no group -- the enclosing
    // group heading has already named it.
    //
    [[nodiscard]]
    auto humanTestHeadingLines( std::string_view test, std::string_view description) -> std::vector<ReportLine>;

    //
    // A SETUP/TEARDOWN bracket's own heading, spelled the same way a group's and
    // a test's are -- the id, then its title quietly beside it.
    //
    // Indented like a test when a group encloses it, unindented like a group
    // when nothing does. That is the whole reason the group is a parameter: a
    // group's SETUP and the catalog's RUN_SETUP are both called "setup", and
    // the level they sit at is the only thing that tells them apart on the page
    // -- see core::IJournalSink::onPhaseStart.
    //
    // Emitted whether or not the hook goes on to log anything the human stream
    // carries. A heading with nothing under it is not an empty heading: a hook
    // that only sourced and routed says exactly that, and a report that showed
    // nothing at all could not be told from one for a run where the bracket
    // never ran.
    //
    [[nodiscard]]
    auto humanPhaseHeadingLines( std::string_view group, std::string_view phase, std::string_view title) -> std::vector<ReportLine>;

    //
    // What closes that block -- the blank line a test's block gets from its
    // RESULT row (see humanTestResultLines). A hook has no verdict to state, so
    // it has nothing else to close on, and without this its last reading runs
    // straight into the next group's heading.
    //
    [[nodiscard]]
    auto humanPhaseClosingLines() -> std::vector<ReportLine>;

    //
    // One event's lines, or empty for an event the human stream doesn't carry
    // -- see this header's own comment on the filtering rule. Returns a vector
    // rather than a single line so a future event that wants a continuation
    // line (a multi-value reading, say) needs no signature change.
    //
    [[nodiscard]]
    auto humanEventLines( const JournalEvent & event) -> std::vector<ReportLine>;

    //
    // The test's own verdict, restated after its checks -- Pass or Fail
    // emphasis, so it is findable by colour as well as by text -- followed by
    // the blank line that closes the test's block. That blank belongs here
    // rather than at the front of the next test's heading: a run's last test
    // has no next heading, and a log whose final block runs straight into the
    // summary reads as though something was cut off.
    //
    [[nodiscard]]
    auto humanTestResultLines( std::string_view test, bool passed) -> std::vector<ReportLine>;

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
