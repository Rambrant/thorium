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
            // (see app/src/main.cpp), and the machine log for a run that failed
            // that way is the one most worth having.
            //
            ~SarifSink() override;

            SarifSink( const SarifSink &)                     = delete;
            auto operator=( const SarifSink &) -> SarifSink & = delete;

            auto onRunStart( const RunInfo & info) -> void override;
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
            // SARIF requires every result's ruleId to resolve to a rule in
            // tool.driver.rules, so rules are collected as events arrive --
            // insertion-ordered and de-duplicated, since a criterion checked in
            // three tests is one rule with three results, not three rules.
            //
            auto noteRule( const JournalEvent & event) -> void;

            auto writeLog( bool allPassed) -> void;

            std::string                                       mPath;
            RunInfo                                           mRunInfo;
            std::vector<JournalEvent>                         mEvents;
            std::vector<std::pair<std::string, std::string>>  mRules;   // ruleId -> description
            bool                                              mWritten{ false };
    };
} // namespace core
