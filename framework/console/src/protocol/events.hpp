#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace console
{
    //
    // One line of `run_scripts --events=-`, decoded.
    //
    // The schema is documented where it is produced -- see
    // core/journal/event_sink.hpp -- and this is deliberately a flat struct
    // with a Kind rather than a variant of nine types. Two reasons, and the
    // second is the real one:
    //
    //   - the fields overlap heavily. Group, test and phase appear on most
    //     kinds; a variant would repeat them or introduce a shared base, and
    //     the shared base would be this struct.
    //   - the consumer is a window that switches on Kind once and reads the
    //     two or three fields that kind carries. A variant would buy exhaustive
    //     matching over a set this program does not need to match exhaustively:
    //     an unknown kind is a *newer runner*, which must be ignored rather
    //     than refused (see RunEvent::Kind::Unknown).
    //
    struct RunEvent
    {
        enum class Kind
        {
            RunStart,
            GroupStart,
            GroupEnd,
            TestStart,
            TestEnd,
            PhaseStart,
            PhaseEnd,
            Event,
            RunEnd,

            //
            // A kind emitted by a run_scripts newer than this UI. Skipped, not
            // rejected: the bench installs suites, not consoles, so an older
            // window in front of a newer binary is the ordinary case and it
            // must degrade to "I did not draw that" rather than to "I cannot
            // read this run".
            //
            Unknown
        };

        Kind         Which{ Kind::Unknown };

        std::string  Group;
        std::string  Test;
        std::string  Phase;
        std::string  Description;
        std::string  Title;

        // Kind::Event only, and every one of them can legitimately be empty --
        // a Connect names no value, a Measure names no criterion.
        std::string             Verb;
        std::string             Subject;
        std::string             SubjectGroup;
        std::string             Detail;
        std::string             Instrument;
        std::string             Value;
        std::string             Unit;
        std::string             CriterionText;
        std::optional<double>   Numeric;

        //
        // The verdict, and the only thing in this struct the window must not
        // second-guess. Set on a Verify event, on TestEnd, and on RunEnd; unset
        // everywhere else, which is what stops an Apply being drawn as a
        // passing check (the same distinction core::JournalRecord::Passed draws,
        // for the same reason).
        //
        std::optional<bool>     Passed;

        //
        // Kind::RunStart only: the traceability header, kept as it arrived.
        // The window shows it above the run and otherwise does nothing with it.
        //
        struct RunHeader
        {
            std::string               DutName;
            std::string               DutSerial;
            std::string               RigName;
            std::string               CriteriaVariant;
            std::string               CriteriaMaster;
            std::string               Operator;
            std::string               StartedLocal;
            std::string               CommandLine;
            std::vector<std::string>  Instruments;

            //
            // False for a --replay, --inject or --skeleton run. Surfaced
            // prominently, for the reason core/journal/journal.hpp gives for
            // putting it in the header: it is the difference between a green
            // report about a DUT and a green report about a file, and a person
            // must not have to derive it from the command line.
            //
            bool                      BenchAttached{ true };
        };

        RunHeader  Header;
    };

    //
    // Nothing for a line that is not a complete JSON object.
    //
    // Which happens, and is not an error: a run killed mid-write leaves a
    // partial last line. console::EventStream drops those and carries on, which it
    // can do safely precisely because the format is one object per line --
    // see core/journal/event_sink.hpp on why it is line-delimited.
    //
    [[nodiscard]]
    auto parseEvent( std::string_view line) -> std::optional<RunEvent>;

    //
    // Feeds bytes in, gets whole events out.
    //
    // A reader over a pipe gets arbitrary chunks, not lines, so somebody has to
    // hold the partial tail between reads. That is all this is -- and it is a
    // type rather than a loop in the window because the window must not be the
    // thing that knows a read can split a line.
    //
    class EventStream
    {
        public:
            //
            // Appends a chunk and returns every event completed by it, in
            // order. A chunk with no newline in it returns nothing and is
            // remembered.
            //
            auto consume( std::string_view chunk) -> std::vector<RunEvent>;

            //
            // Whatever is left when the child's stdout closes. A well-behaved
            // run ends on a newline and this is empty; a killed one leaves a
            // fragment here, which is discarded -- the caller learns the run
            // died from the missing runEnd, not from this.
            //
            [[nodiscard]]
            auto residue() const -> const std::string & { return mPartial; }

        private:
            std::string  mPartial;
    };
} // namespace console
