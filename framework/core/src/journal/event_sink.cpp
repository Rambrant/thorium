#include "core/journal/event_sink.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/journal/json.hpp"

namespace core
{
    namespace
    {
        //
        // One line's worth of object, assembled a field at a time.
        //
        // A tiny builder rather than the stream-of-operator<< the SARIF sink
        // uses, and the reason is the comma: every field here is conditional
        // (an empty one is omitted -- see the header), so a writer emitting
        // straight to the stream would have to track "have I written anything
        // yet" at every one of a dozen call sites. Holding the text instead
        // makes that one flag in one place, and the line is short enough that
        // building it in memory costs nothing worth measuring.
        //
        class Line
        {
            public:
                explicit Line( const std::string_view kind)
                {
                    //
                    // "kind" first, always, and written directly rather than
                    // through field(): it is the one field that is never
                    // omitted and never needs the separator logic, and having
                    // it seed the object is what guarantees the position the
                    // header promises.
                    //
                    mText = "{\"kind\":" + jsonQuoted( kind);
                }

                //
                // A string field, omitted when empty. Every caller below passes
                // a field that can legitimately be absent, which is why there
                // is no unconditional variant: the one field that is always
                // there is written by the constructor above.
                //
                auto field( const std::string_view key, const std::string_view value) -> Line &
                {
                    if( !value.empty())
                    {
                        mText += "," + jsonQuoted( key) + ":" + jsonQuoted( value);
                    }

                    return *this;
                }

                auto field( const std::string_view key, const bool value) -> Line &
                {
                    mText += "," + jsonQuoted( key) + ":" + ( value ? "true" : "false");

                    return *this;
                }

                auto field( const std::string_view key, const std::uint64_t value) -> Line &
                {
                    mText += "," + jsonQuoted( key) + ":" + std::to_string( value);

                    return *this;
                }

                auto field( const std::string_view key, const std::int64_t value) -> Line &
                {
                    mText += "," + jsonQuoted( key) + ":" + std::to_string( value);

                    return *this;
                }

                //
                // A number that may not be there -- a Measure has one, a
                // Connect does not. Written unquoted, because the whole point
                // of JournalRecord::Numeric is that a consumer can compare it
                // against a limit without re-parsing the printable Value beside
                // it, and a quoted number would hand that job straight back.
                //
                auto field( const std::string_view key, const std::optional<double> & value) -> Line &
                {
                    if( value)
                    {
                        mText += "," + jsonQuoted( key) + ":" + std::to_string( *value);
                    }

                    return *this;
                }

                auto field( const std::string_view key, const std::optional<bool> & value) -> Line &
                {
                    if( value)
                    {
                        mText += "," + jsonQuoted( key) + ":" + ( *value ? "true" : "false");
                    }

                    return *this;
                }

                //
                // A nested object, opened and closed by the caller. Only
                // runStart needs one -- the RunInfo is a header rather than an
                // event, and flattening a dozen of its fields into the top
                // level would put "operator" and "subject" in the same
                // namespace for no gain.
                //
                auto openObject( const std::string_view key) -> Line &
                {
                    mText += "," + jsonQuoted( key) + ":{";
                    mFirstNested = true;

                    return *this;
                }

                auto nested( const std::string_view key, const std::string_view value) -> Line &
                {
                    if( !value.empty())
                    {
                        mText += ( mFirstNested ? "" : ",") + jsonQuoted( key) + ":" + jsonQuoted( value);
                        mFirstNested = false;
                    }

                    return *this;
                }

                auto nested( const std::string_view key, const bool value) -> Line &
                {
                    mText += ( mFirstNested ? "" : ",") + jsonQuoted( key) + ":" + ( value ? "true" : "false");
                    mFirstNested = false;

                    return *this;
                }

                auto nestedArray( const std::string_view key, const std::vector<std::string> & values) -> Line &
                {
                    if( values.empty())
                    {
                        return *this;
                    }

                    mText += ( mFirstNested ? "" : ",") + jsonQuoted( key) + ":[";
                    mFirstNested = false;

                    bool first = true;

                    for( const auto & value : values)
                    {
                        mText += ( first ? "" : ",") + jsonQuoted( value);
                        first  = false;
                    }

                    mText += "]";

                    return *this;
                }

                auto closeObject() -> Line &
                {
                    mText += "}";

                    return *this;
                }

                //
                // Flushed, not merely newline-terminated. The stream is
                // typically a pipe to a parent process, and a watcher that sees
                // a rail voltage when the buffer happens to fill is watching
                // something other than the run.
                //
                auto writeTo( std::ostream & out) const -> void
                {
                    out << mText << "}\n" << std::flush;
                }

            private:
                std::string  mText;
                bool         mFirstNested{ true };
        };
    } // namespace

    EventSink::EventSink( std::ostream & out, LogFiles logs) :
        mOut( &out),
        mLogs( std::move( logs))
    {}

    auto EventSink::onRunStart( const RunInfo & info) -> void
    {
        Line line( "runStart");

        line.openObject( "info")
            .nested( "frameworkName",    info.FrameworkName)
            .nested( "frameworkVersion", info.FrameworkVersion)
            .nested( "criteriaVariant",  info.CriteriaVariant)
            .nested( "criteriaMaster",   info.CriteriaMaster)
            .nested( "dutName",          info.DutName)
            .nested( "dutSerial",        info.DutSerial)
            .nested( "rigName",          info.RigName)
            .nested( "operator",         info.Operator)
            .nested( "hostName",         info.HostName)
            .nested( "commandLine",      info.CommandLine)
            .nested( "suiteVersion",     info.SuiteVersion)
            .nested( "dutVersion",       info.DutVersion)
            .nested( "rigVersion",       info.RigVersion)
            .nested( "startedUtc",       info.StartedUtc)
            .nested( "startedLocal",     info.StartedLocal)
            //
            // Unconditional, unlike every string above it, and for the reason
            // core/journal/journal.hpp gives for putting it in the header at
            // all: "no instrument was touched" is the single most important
            // fact about a replayed run, and a field that is absent when false
            // would make the most important case the invisible one.
            //
            .nested( "benchAttached",    info.BenchAttached)
            .nestedArray( "instruments", info.Instruments)
            .closeObject();

        //
        // Beside "info" rather than in it -- see LogFiles on why the paths
        // are not part of the header.
        //
        if( !mLogs.Sarif.empty() || !mLogs.Rtf.empty())
        {
            line.openObject( "logs")
                .nested( "sarif", mLogs.Sarif)
                .nested( "rtf",   mLogs.Rtf)
                .closeObject();
        }

        line.writeTo( *mOut);
    }

    auto EventSink::onGroupStart( const std::string_view group, const std::string_view description) -> void
    {
        Line( "groupStart")
            .field( "group",       group)
            .field( "description", description)
            .writeTo( *mOut);
    }

    auto EventSink::onGroupEnd( const std::string_view group) -> void
    {
        Line( "groupEnd").field( "group", group).writeTo( *mOut);
    }

    auto EventSink::onTestStart( const std::string_view test, const std::string_view description) -> void
    {
        Line( "testStart")
            .field( "test",        test)
            .field( "description", description)
            .writeTo( *mOut);
    }

    auto EventSink::onPhaseStart( const std::string_view group, const std::string_view phase, const std::string_view title) -> void
    {
        //
        // The group is carried even though it is usually empty, because empty
        // is what distinguishes the catalog's own RUN_SETUP from a group's
        // SETUP -- the two spell their phase identically, and this is the field
        // that says which one a watcher is looking at (see IJournalSink).
        //
        Line( "phaseStart")
            .field( "group", group)
            .field( "phase", phase)
            .field( "title", title)
            .writeTo( *mOut);
    }

    auto EventSink::onPhaseEnd( const std::string_view phase) -> void
    {
        Line( "phaseEnd").field( "phase", phase).writeTo( *mOut);
    }

    auto EventSink::onEvent( const JournalEvent & event) -> void
    {
        //
        // Every verb, like the machine log and unlike the human one: a watcher
        // is reconstructing what the run did to the hardware as it does it, and
        // a routing step dropped for brevity is exactly the step that explains
        // the reading after it.
        //
        Line( "event")
            .field( "verb",          to_string( event.Method))
            .field( "sequence",      event.Sequence)
            .field( "timeUtc",       event.TimeUtc)
            .field( "wallClockMs",   event.WallClockUnixMillis)
            .field( "group",         event.Group)
            .field( "test",          event.Test)
            .field( "phase",         event.Phase)
            .field( "subject",       event.Subject)
            .field( "subjectGroup",  event.SubjectGroup)
            .field( "detail",        event.Detail)
            .field( "instrument",    event.Instrument)
            .field( "value",         event.Value)
            .field( "numeric",       event.Numeric)
            .field( "unit",          event.Unit)
            .field( "criterionText", event.CriterionText)
            .field( "passed",        event.Passed)
            .writeTo( *mOut);
    }

    auto EventSink::onTestEnd( const std::string_view group, const std::string_view test, const bool passed) -> void
    {
        //
        // (group, test), not (test, description) -- see IJournalSink::onTestEnd,
        // whose parameters are named for exactly the mistake this line made
        // first time round.
        //
        Line( "testEnd")
            .field( "group",  group)
            .field( "test",   test)
            .field( "passed", passed)
            .writeTo( *mOut);
    }

    auto EventSink::onRunEnd( const bool allPassed) -> void
    {
        Line( "runEnd").field( "allPassed", allPassed).writeTo( *mOut);
    }
} // namespace core
