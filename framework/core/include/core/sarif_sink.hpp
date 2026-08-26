#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/journal.hpp"

namespace core
{
    //
    // The machine-readable log: SARIF 2.1.0 (OASIS Static Analysis Results
    // Interchange Format).
    //
    // SARIF for a hardware test log is a deliberate reuse rather than an
    // obvious fit, so it is worth saying what it buys. It is a standardised,
    // schema-validated envelope for "a tool ran, here is what it found, here is
    // exactly which tool and which inputs" -- which is the shape of a test run
    // -- and it already has first-class places for every traceability fact this
    // needs (tool.driver.version, invocations[].commandLine/startTimeUtc,
    // automationDetails, per-result rule references). Anything already able to
    // ingest SARIF -- a CI system, a results dashboard, a diffing tool -- reads
    // this with no Thorium-specific parser, and anything not able to still gets
    // ordinary, self-describing JSON.
    //
    // The mapping, in full:
    //
    //   criterion (a CRIT entry)   -> tool.driver.rules[] entry, ruleId
    //                                 "<group>/<id>"; description straight from
    //                                 the criterion's own prose
    //   Verify                     -> result, kind pass|fail, level none|error
    //   every other verb           -> result, kind informational, level none,
    //                                 ruleId "Thorium/<Verb>"
    //   test group/test id         -> each result's logicalLocations and
    //                                 properties
    //   entering a group/test      -> result, ruleId "Thorium/Group" or
    //                                 "Thorium/Test", carrying its title
    //   entering SETUP/TEARDOWN    -> result, ruleId "Thorium/Phase", id
    //                                 "setup"/"teardown", carrying its title
    //   RunInfo                    -> tool.driver + invocations[0] +
    //                                 automationDetails + run.properties
    //
    // Unlike the human log, this carries *every* verb -- Measure, Apply,
    // Remove, Connect, Disconnect, Verify and the safing pass -- because its
    // reader is reconstructing what the run did to the hardware, in order, and
    // a routing step omitted for brevity is exactly the step that explains a
    // failed reading.
    //
    // -------------------------------------------------------------------
    // Why the structure is in the results array too
    // -------------------------------------------------------------------
    // Group and test already ride along on every result as properties, which
    // answers "which test was this?" but not "what did the run consist of?".
    // The catalog's own shape -- the groups and tests entered, in order, each
    // with the title its GROUP/TEST entry gives it -- has nowhere else to go:
    // SARIF has no per-run structure section, the titles are nowhere in the
    // file otherwise, and a group whose every test was deselected leaves no
    // trace at all.
    //
    // So a boundary is a result, exactly as the "pass 2 of 3" note already is
    // -- informational, level none, with the id in its logical location and the
    // title in its message. Nothing is invented to carry it: a reader who
    // ignores the new rule ids sees the file it saw before.
    //
    // Boundaries are *entries*, not brackets: what closes a test is the next
    // boundary, and the per-test verdict is deliberately not restated here
    // (this sink derives it from the results inside the test, and a second,
    // separately-computed copy is the kind of thing that ends up disagreeing
    // with the first -- see core::Journal::endTest).
    //
    // -------------------------------------------------------------------
    // Why this one is not readable mid-run, where the RTF log is
    // -------------------------------------------------------------------
    // A SARIF log is one JSON document with one results array, so it is not
    // meaningfully valid until it is closed -- there is no equivalent of
    // core::RtfSink's rewrite-the-trailer trick, because a JSON array's
    // elements are comma-separated and a partial document would have to be
    // repaired rather than merely completed. Events are therefore buffered and
    // written once, at onRunEnd (or from the destructor, if a run dies before
    // getting there -- see ~SarifSink). That is the right trade: the live view
    // is a human need, and it is served by core::ConsoleSink and core::RtfSink.
    //
    class SarifSink : public IJournalSink
    {
        public:
            //
            // Records the path; the file is not opened until the log is
            // written. Deliberate -- an unwritable path should fail the run at
            // the point the log is produced, alongside whatever else is being
            // reported, rather than at construction where the failure has no
            // results to accompany it.
            //
            explicit SarifSink( std::string path);

            //
            // Writes the log if onRunEnd never arrived. That is not a
            // theoretical path: hal::RigSafingGuard exists precisely because a
            // suite run can end by an exception propagating out of a script
            // (see framework/runner/src/main.cpp), and the machine log for a run that failed
            // that way is the one most worth having.
            //
            ~SarifSink() override;

            SarifSink( const SarifSink &)                     = delete;
            auto operator=( const SarifSink &) -> SarifSink & = delete;

            auto onRunStart( const RunInfo & info) -> void override;
            auto onGroupStart( std::string_view group, std::string_view description) -> void override;
            auto onGroupEnd( std::string_view group) -> void override;
            auto onTestStart( std::string_view test, std::string_view description) -> void override;
            auto onPhaseStart( std::string_view group, std::string_view phase, std::string_view title) -> void override;
            auto onEvent( const JournalEvent & event) -> void override;
            auto onRunEnd( bool allPassed) -> void override;

            //
            // JSON string escaping, exposed for its own test -- a criterion
            // description containing a quote or a backslash must not be able
            // to produce a log that no consumer can parse.
            //
            [[nodiscard]]
            static auto escape( std::string_view text) -> std::string;

            //
            // The rule id a given event is reported under: "<group>/<id>" for a
            // Verify, "Thorium/<Verb>" for everything else. Exposed so the
            // mapping can be asserted directly rather than by grepping
            // generated JSON.
            //
            [[nodiscard]]
            static auto ruleIdFor( const JournalEvent & event) -> std::string;

        private:
            //
            // What one entry of the results array is. Ordinary events are the
            // bulk of them; the other three are the catalog boundaries this
            // sink is told about directly, which arrive through their own hooks
            // and never through the journal's event stream.
            //
            enum class Entry
            {
                Event,
                Group,
                Test,
                Phase
            };

            //
            // One result, waiting for the document to be written.
            //
            // A boundary borrows JournalEvent rather than getting a struct of
            // its own, because what the writer needs of it is what the writer
            // needs of any result: a name (Subject), prose (Detail), an
            // enclosing attribution (Group) and a time. What it does *not* get
            // is a Sequence -- it was never posted, so it has no place in the
            // journal's numbering, and inventing one would put a number in the
            // file that no event ever carried. Its position in the array and
            // its timeUtc are what order it.
            //
            // Group holds the *enclosing* group, not the boundary's own name,
            // which is what makes fullyQualifiedName() compose correctly for
            // all four kinds without a special case: "OutputVoltage",
            // "OutputVoltage/SupplyRail", "OutputVoltage/setup", "setup".
            //
            struct Result
            {
                Entry         What{ Entry::Event };
                JournalEvent  Event;
            };

            //
            // How a boundary of each kind describes itself: the rule it reports
            // under, the word its message opens with, and SARIF's own
            // vocabulary for what sort of logical location it is -- a group is a
            // naming scope, a test and a hook are things that ran.
            //
            // One place rather than two, because noteBoundary registers the
            // rule and writeLog writes the result that refers to it, and a
            // ruleId that disagreed between them would produce a document whose
            // results point at rules it does not declare.
            //
            struct BoundaryText
            {
                std::string_view  Rule;
                std::string_view  Label;
                std::string_view  LocationKind;
            };

            [[nodiscard]]
            static auto boundaryTextFor( Entry what) -> BoundaryText;

            //
            // SARIF requires every result's ruleId to resolve to a rule in
            // tool.driver.rules, so rules are collected as events arrive --
            // insertion-ordered and de-duplicated, since a criterion checked in
            // three tests is one rule with three results, not three rules.
            //
            auto noteRule( const JournalEvent & event) -> void;

            //
            // Records one boundary, and the rule it reports under. `enclosing`
            // is the group it sits inside, which is not the same as the group
            // it may itself name -- see Result above.
            //
            // Tracked here rather than read back off the last event, so a group
            // that ran no test at all -- everything in it deselected, or its
            // SETUP refusing -- still names itself on the boundaries nested
            // inside it.
            //
            auto noteBoundary( Entry what, std::string_view id, std::string_view title, std::string_view enclosing) -> void;

            auto writeLog( bool allPassed) -> void;

            std::string                                       mPath;
            RunInfo                                           mRunInfo;
            std::vector<Result>                               mResults;
            std::string                                       mGroup;
            std::vector<std::pair<std::string, std::string>>  mRules;   // ruleId -> description
            bool                                              mWritten{ false };
    };
} // namespace core
