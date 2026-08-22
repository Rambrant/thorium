#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core
{
    //
    // The run journal: one ordered stream of everything the framework's verbs
    // did, fanned out to any number of sinks that each render it their own way.
    //
    // Two sinks exist today and they want very different things from the same
    // run, which is the whole reason this is an event stream rather than each
    // verb formatting its own log line:
    //
    //   - core::SarifSink -- every verb, machine-consumable, for a tool or a
    //     server to ingest (core/sarif_sink.hpp)
    //   - core::RtfSink / core::ConsoleSink -- Measure and Verify only,
    //     colour-coded, for a person (core/rtf_sink.hpp, core/console_sink.hpp)
    //
    // Neither filtering nor formatting lives at the call site, so adding a
    // third stream later (JUnit XML, a live socket, whatever) needs no change
    // to Measure/Apply/Verify at all -- which matters because those call sites
    // are spread across core/measure.hpp, core/source.hpp, core/route.hpp,
    // core/verify.cpp and
    // hal/src/safing.cpp, and a per-sink `if` in each of them is exactly the
    // list that falls behind the next sink added.
    //
    // Deliberately reached through a single process-wide journal() below rather
    // than passed into each verb. That isn't convenience: a catalog-registered
    // test script takes no parameters at all, so there is no room for a
    // logger one (see core/test_catalog.hpp on why), and the verbs
    // a script calls -- Measure, Apply, Verify -- are themselves globals for
    // the same reason (see hal/src/measure.cpp, hal/src/source.cpp). A journal
    // threaded by parameter would have to stop somewhere, and the place it
    // would stop is precisely the layer that needs it.
    //

    //
    // Which framework verb produced an event. Named for the verbs a test
    // script actually writes (Measure/Apply/Verify/...) rather than for
    // severity or category: the machine log's consumers ask "what did the run
    // do", and the answer is a sequence of these.
    //
    // Safe is the one that no script calls -- hal::safeRig() posts it, from a
    // guard destructor or a --safe re-invocation (see hal/safing.hpp). It is
    // in the same enum rather than a separate notion because "the rig was
    // dropped to idle here" is exactly as much a part of what a run did as
    // any Apply, and a reader reconstructing the run needs it in order.
    //
    // Setup/Write/Read are the byte-oriented half, added with serial
    // communication (see core/transfer.hpp). Each is a genuinely distinct thing
    // a run did, and collapsing any of them into a neighbour would lose
    // something a reader needs:
    //
    //   - Setup configures an instrument without acting -- a UART's baud rate
    //     and framing, a scope's timebase. Not Apply, which energises
    //     something: the whole reason it is separate is that the two have
    //     different consequences at the bench, and a log in which "the port
    //     was configured for 9600 8N1" and "the supply was brought up to 24 V"
    //     are the same word cannot be read for what a run actually did.
    //   - Write is a stimulus sent to the DUT, Read an answer that came back.
    //     Two verbs rather than one Transact, because a fire-and-forget write
    //     and an unsolicited read both happen, and because a reply that never
    //     arrived has to be visible in the log as a Write with no Read after
    //     it.
    //
    // There is deliberately no Fail. A check that could not be made -- no
    // capture, no reply to read the byte out of -- posts as a Verify with
    // Passed = false and a placeholder where the value would be (see core::Fail
    // in core/verify.hpp). Everything that already tells a failed check from a
    // passing one therefore handles it: an enumerator of its own would have
    // bought a parallel branch in every consumer listed above and nothing else.
    //
    enum class Verb
    {
        Measure,
        Setup,
        Apply,
        Remove,
        Connect,
        Disconnect,
        Write,
        Read,
        //
        // The triggered-acquisition pair (see core/acquire.hpp). Two verbs
        // rather than one because they straddle the stimulus/observation
        // boundary this enum's consumers care about: an Arm is an instruction
        // and belongs with Setup and Write, an Await is an observation and
        // belongs with Measure and Read.
        //
        Arm,
        Await,
        Verify,
        Safe,
        Note
    };

    [[nodiscard]]
    auto to_string( Verb verb) -> std::string_view;

    //
    // Everything a log needs to say about *which* run this is, as opposed to
    // what happened during it -- the traceability header both streams carry.
    //
    // Split by where each field can honestly come from, because that decides
    // whether it can be trusted:
    //   - baked in at build time (framework version, criteria variant, DUT
    //     name, rig name) -- see defaultRunInfo() below and
    //     libs/core/CMakeLists.txt. These cannot be wrong about the binary
    //     doing the testing, because they *are* the binary.
    //   - observed at run time (started-at, command line, host, operator)
    //   - knowable only from outside (DutSerial -- which physical unit is in
    //     the fixture right now), so it is empty unless a caller supplies it
    //
    struct RunInfo
    {
        std::string FrameworkName;
        std::string FrameworkVersion;
        std::string CriteriaVariant;
        std::string DutName;
        std::string DutSerial;
        std::string RigName;
        std::string Operator;
        std::string HostName;
        std::string CommandLine;

        //
        // What revision of this deployment's *content* the binary was built
        // against -- the test scripts, the DUT profile and criteria, the rig's
        // wiring. Three fields rather than one because suite/, dut/ and rig/
        // are separable: they all come from this repo today (and so carry the
        // same revision), but a rig repo consuming this framework would have
        // its own, and a DUT profile shared between benches its own again.
        //
        // Distinct from FrameworkVersion, which versions libs/ -- the code that
        // runs a test. These version what is being tested and how, which is the
        // half that actually changes between two runs that disagree.
        //
        std::string SuiteVersion;
        std::string DutVersion;
        std::string RigVersion;

        //
        // The same instant, twice. StartedUtc is what the machine log records
        // and what two runs are compared by; StartedLocal is what the operator
        // who was standing there recognises, and is what the human log's title
        // carries. Both are produced from one clock reading (see
        // defaultRunInfo) -- deriving them separately would let a report
        // disagree with itself across a second boundary.
        //
        std::string StartedUtc;      // ISO-8601, e.g. 2026-08-01T08:56:02.371Z
        std::string StartedLocal;    // e.g. Sat 01 Aug 2026 10:56:02 CEST
    };

    //
    // RunInfo with everything this build knows about itself already filled in:
    // framework name/version, criteria variant, DUT and rig name from the
    // compile definitions libs/core/CMakeLists.txt sets, plus the current UTC
    // time. Operator/HostName come from the environment (USER/LOGNAME and
    // HOSTNAME/HOST) -- a convenience, not a claim, which is why both are
    // overridable; DutSerial and CommandLine are left empty for the caller
    // (see app/src/main.cpp) to fill in, since neither is derivable here.
    //
    [[nodiscard]]
    auto defaultRunInfo() -> RunInfo;

    //
    // What a call site knows about one event. Everything a call site cannot
    // know -- when it happened, what order it happened in, which test was
    // running -- is stamped on by the journal (see JournalEvent below), so no
    // verb has to reach for a clock or thread a test name through.
    //
    // Value/Numeric/Unit are three renderings of one thing and every one of
    // them is optional: Value is the printable form a human log shows, Numeric
    // and Unit are what a machine consumer compares against a limit without
    // re-parsing text. A Connect event has none of the three; a Measure has
    // all three; a Verify against an integer register has Value and Numeric
    // but no Unit. See core/format.hpp for what produces them.
    //
    struct JournalRecord
    {
        Verb                   Method{ Verb::Note };

        //
        // What the event is about, in the terms the reader of the log thinks
        // in: a DUT point name for Measure, a criterion id for Verify, an
        // instrument id for Apply/Connect, the rig for Safe.
        //
        std::string            Subject{};

        //
        // The grouping Subject itself belongs to, which is NOT the running
        // test's group. For a Verify this is the CRITERIA group name a CRIT
        // entry declares (FS_Supply_1), while the journal's own Group is the
        // catalog test group that happened to reach it (OutputVoltage) -- two
        // genuinely different facts about the same event, and the criteria one
        // is what a test spec traces to. Empty where Subject has no such
        // grouping (a DUT point, an instrument).
        //
        std::string            SubjectGroup{};

        // The traceable prose behind Subject -- a point's or criterion's
        // description, straight from dut/adapter.inc or a CRIT entry.
        std::string            Detail{};

        // Which instrument was involved, where that is a distinct fact from
        // Subject (a Measure's point is not its DMM); empty otherwise.
        std::string            Instrument{};

        std::string            Value{};
        std::optional<double>  Numeric{};
        std::string            Unit{};

        //
        // For a Verify: the criterion's own tolerance, spelled out ("= 5 V
        // +/-0.05 V") -- see core/predicate_text.hpp. Distinct from Detail,
        // which is the prose a CRIT entry's author wrote: nothing checks that
        // prose against the predicate beside it, so a log carrying only the
        // description cannot actually state the limit the run enforced.
        //
        // Empty where there is nothing to say -- every non-Verify event, and a
        // Verify against a predicate that can't describe itself (a lambda).
        //
        std::string            CriterionText{};

        //
        // Set for Verify and nothing else. std::optional rather than a bool
        // defaulting to true: "this event has no pass/fail notion" and "this
        // event passed" are different things, and a sink that renders the
        // second for the first is how an Apply ends up looking like a passing
        // check in a report.
        //
        std::optional<bool>    Passed{};
    };

    //
    // What a sink receives: the call site's record plus the journal's own
    // stamps. Inheritance rather than a nested member so a sink writes
    // event.Subject and event.Sequence the same way, and so a call site can
    // still brace-initialise a JournalRecord with designated initialisers
    // naming only the fields it has.
    //
    struct JournalEvent : JournalRecord
    {
        std::uint64_t  Sequence{ 0 };
        std::int64_t   WallClockUnixMillis{ 0 };
        std::string    TimeUtc{};

        // The catalog group/test that was running, or empty outside any test
        // -- a Safe posted from a guard destructor after the last test ended
        // belongs to the run, not to a test.
        std::string    Group{};
        std::string    Test{};
    };

    //
    // A log stream. Every hook has an empty default body rather than being
    // pure: ConsoleSink and RtfSink ignore nothing, but SarifSink has no use
    // for onTestEnd (it derives per-test results from the events themselves),
    // and a future sink that only wants failures should not have to write four
    // empty overrides to say so.
    //
    class IJournalSink
    {
        public:
            virtual ~IJournalSink() = default;

            virtual auto onRunStart( const RunInfo &) -> void {}

            //
            // A catalog group's own boundary, distinct from its tests'. This
            // exists because the human log states a group once and nests its
            // tests under it -- so the group's name and description have to
            // arrive on their own, not repeated on every test that happens to
            // belong to it.
            //
            // Only groups with at least one test actually running get these:
            // see app/src/main.cpp, which is where the selection is known.
            //
            virtual auto onGroupStart( std::string_view, std::string_view) -> void {}
            virtual auto onGroupEnd( std::string_view) -> void {}

            // The test's own id and description -- the group is whichever
            // onGroupStart is currently open.
            virtual auto onTestStart( std::string_view, std::string_view) -> void {}
            virtual auto onEvent( const JournalEvent &) -> void {}
            virtual auto onTestEnd( std::string_view, std::string_view, bool) -> void {}
            virtual auto onRunEnd( bool) -> void {}
    };

    class Journal
    {
        public:
            //
            // Sinks are referenced, never owned -- they outlive the journal's
            // use of them because the caller holds them (see app/src/main.cpp,
            // where both file sinks are locals whose scope encloses the run).
            // That ordering matters: a sink's destructor is what closes its
            // file, and a journal that owned them would decide when that
            // happened relative to the last event posted.
            //
            auto add( IJournalSink & sink) -> void;

            // Drops every registered sink. Exists for tests, which register
            // recording sinks on the one process-wide journal and must not
            // leak them into the next test case.
            auto clearSinks() -> void;

            //
            // Run boundaries. begin() takes the RunInfo by value and keeps
            // it: sinks are handed a reference to the journal's copy, so a
            // sink that wants the traceability header at close time (SarifSink
            // writes it into its tool/invocation blocks) can hold on to what
            // it was given.
            //
            auto begin( RunInfo info) -> void;
            auto end( bool allPassed) -> void;

            //
            // Group and test boundaries. Everything posted inside them is
            // stamped with that group/test, which is what lets a Measure inside
            // a script be attributed to the test that caused it without the
            // script (or Measure) knowing its own name.
            //
            // The group is set by beginGroup rather than passed to beginTest:
            // a test's group is the one enclosing it, and having each test
            // restate it would be two sources for one fact -- with the usual
            // consequence that they can disagree.
            //
            auto beginGroup( std::string_view group, std::string_view description) -> void;
            auto endGroup() -> void;

            auto beginTest( std::string_view test, std::string_view description) -> void;
            auto endTest( bool passed) -> void;

            //
            // Post one event. Stamps sequence, wall clock, and the current
            // group/test, then fans out. Safe to call with no sinks
            // registered -- that is the normal state in a unit test binary,
            // and a verb that had to check would be a verb that could get it
            // wrong.
            //
            auto post( JournalRecord record) -> void;

            [[nodiscard]]
            auto runInfo() const -> const RunInfo &
            {
                return mRunInfo;
            }

            [[nodiscard]]
            auto currentGroup() const -> std::string_view
            {
                return mGroup;
            }

            [[nodiscard]]
            auto currentTest() const -> std::string_view
            {
                return mTest;
            }

        private:
            std::vector<IJournalSink *>  mSinks;
            RunInfo                      mRunInfo;
            std::string                  mGroup;
            std::string                  mTest;
            std::uint64_t                mNextSequence{ 0 };
    };

    //
    // The one journal a run posts to -- see this header's own comment on why
    // this is process-wide rather than passed in.
    //
    [[nodiscard]]
    auto journal() -> Journal &;

    //
    // ISO-8601 UTC, millisecond precision, from a Unix-epoch millisecond
    // count. Exposed (rather than kept in journal.cpp) because both file sinks
    // stamp their own close time, and a log whose events and whose trailer
    // disagree about how to spell a timestamp is a log somebody has to write
    // two parsers for.
    //
    [[nodiscard]]
    auto isoUtcFromUnixMillis( std::int64_t millis) -> std::string;

    //
    // The same instant in the machine's local zone, spelled for a person
    // ("Sat 01 Aug 2026 10:56:02 CEST") rather than for a parser. The zone
    // abbreviation is not decoration: a local time without one is ambiguous the
    // moment the log leaves the bench it was written on, and this is the field
    // an operator will use to say "that was the run before lunch".
    //
    [[nodiscard]]
    auto localTimeFromUnixMillis( std::int64_t millis) -> std::string;

    [[nodiscard]]
    auto unixMillisNow() -> std::int64_t;
} // namespace core
