#include "core/session/session.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>
#include <variant>

#include "core/journal/journal.hpp"

namespace core
{
    namespace
    {
        auto checkKind( const std::string_view name, const QuantityVariant & value, const QuantityKind expected) -> void
        {
            if( static_cast<QuantityKind>( value.index()) != expected)
            {
                throw std::runtime_error(
                    "ScriptedSession: point '" + std::string( name) + "' was programmed as " +
                    std::string( to_string( static_cast<QuantityKind>( value.index()))) +
                    " but was fetched as " + std::string( to_string( expected)));
            }
        }
    } // namespace

    auto constantSource( QuantityVariant value) -> ValueSource
    {
        return [ value = std::move( value)]() -> std::optional<QuantityVariant> { return value; };
    }

    auto constantDataSource( Bytes value) -> DataSource
    {
        return [ value = std::move( value)]() -> std::optional<Bytes> { return value; };
    }

    auto constantFlagSource( const bool value) -> FlagSource
    {
        return [ value]() -> std::optional<bool> { return value; };
    }

    auto constantTraceSource( Waveform value) -> TraceSource
    {
        return [ value = std::move( value)]() -> std::optional<Waveform> { return value; };
    }

    auto ScriptedSession::program( const std::string_view name, QuantityVariant value) -> void
    {
        program( name, constantSource( std::move( value)));
    }

    auto ScriptedSession::program( const std::string_view name, ValueSource source) -> void
    {
        mSources.program( name, std::move( source));
    }

    auto ScriptedSession::programData( const std::string_view name, Bytes value) -> void
    {
        programData( name, constantDataSource( std::move( value)));
    }

    auto ScriptedSession::programData( const std::string_view name, DataSource source) -> void
    {
        mDataSources.program( name, std::move( source));
    }

    auto ScriptedSession::programFlag( const std::string_view name, const bool value) -> void
    {
        programFlag( name, constantFlagSource( value));
    }

    auto ScriptedSession::programFlag( const std::string_view name, FlagSource source) -> void
    {
        mFlagSources.program( name, std::move( source));
    }

    auto ScriptedSession::programTrace( const std::string_view name, Waveform value) -> void
    {
        programTrace( name, constantTraceSource( std::move( value)));
    }

    auto ScriptedSession::programTrace( const std::string_view name, TraceSource source) -> void
    {
        mTraceSources.program( name, std::move( source));
    }

    auto ScriptedSession::loadFromFile(
        const std::string &                    path,
        const std::vector<std::string_view> &  selection) -> ScriptedSession
    {
        std::ifstream in( path);

        if( !in)
        {
            throw std::runtime_error( "ScriptedSession::loadFromFile: could not open '" + path + "'");
        }

        //
        // Derived from the recording's own path rather than asked for, so that
        // a caller with a recording has everything it needs to replay it --
        // see core::sidecarDirectoryFor, which is the one place the pairing
        // rule is written down.
        //
        const auto sidecarDirectory = sidecarDirectoryFor( path);

        //
        // Collected per point first, then turned into one source each. A
        // recording interleaves points in the order they were measured, so the
        // per-point sequence only exists once the whole file has been read --
        // there is nothing to hand to sourceOf() until then.
        //
        std::unordered_map<std::string, std::vector<QuantityVariant>>  samples;
        std::unordered_map<std::string, std::vector<Bytes>>            payloads;
        std::unordered_map<std::string, std::vector<bool>>             flags;
        std::unordered_map<std::string, std::vector<Waveform>>         traces;

        //
        // Sorted into the four seams by which alternative each row holds, not
        // by anything about its name -- a recording is a flat, interleaved
        // stream and a row says for itself what it is (see core::RecordedValue).
        //
        //
        // Which tests the file actually has rows for, collected whether or not
        // there is a selection to check against: it costs one insert per row and
        // it is the whole of the refusal below. Ordered, so that two runs
        // rejecting the same file word it the same way.
        //
        // Owning strings rather than views into the samples: the vector they
        // came from is a temporary whose life ends with the loop, and the
        // refusal that reads this comes after it.
        //
        std::set<std::string>  recordedTests;
        auto                   matchedSelection = false;

        for( auto & sample : readRecording( in, sidecarDirectory))
        {
            //
            // Rows outside any test are always kept -- see this function's
            // comment in core/session/session.hpp on RUN_SETUP and RUN_TEARDOWN.
            //
            const auto scoped = sample.mTestId != kRunScope;

            if( scoped)
            {
                recordedTests.insert( sample.mTestId);
            }

            if( scoped && !selection.empty())
            {
                if( std::ranges::find( selection, sample.mTestId) == selection.end())
                {
                    continue;
                }

                matchedSelection = true;
            }

            if( auto * payload = std::get_if<Bytes>( &sample.mValue))
            {
                payloads[ sample.mPointName].push_back( std::move( *payload));
            }
            else if( const auto * flag = std::get_if<bool>( &sample.mValue))
            {
                flags[ sample.mPointName].push_back( *flag);
            }
            else if( auto * trace = std::get_if<Waveform>( &sample.mValue))
            {
                traces[ sample.mPointName].push_back( std::move( *trace));
            }
            else
            {
                samples[ sample.mPointName].push_back( std::get<QuantityVariant>( sample.mValue));
            }
        }

        //
        // Nothing in this recording belongs to any test that was asked for. The
        // file is of a run that did not include them -- see this function's
        // comment in core/session/session.hpp on why that is refused here rather than
        // left to surface as a missing point name later.
        //
        if( !selection.empty() && !matchedSelection)
        {
            auto message = "ScriptedSession::loadFromFile: '" + path +
                           "' has no readings for the selected test(s). It recorded:";

            for( const auto & test : recordedTests)
            {
                message += ' ' + test;
            }

            throw std::runtime_error(
                recordedTests.empty() ? message + " nothing but hook readings" : message);
        }

        ScriptedSession session;

        for( auto & [ pointName, values] : samples)
        {
            session.program( pointName, sourceOf( std::move( values)));
        }

        for( auto & [ pointName, values] : payloads)
        {
            session.programData( pointName, dataSourceOf( std::move( values)));
        }

        for( auto & [ pointName, values] : flags)
        {
            session.programFlag( pointName, flagSourceOf( std::move( values)));
        }

        for( auto & [ pointName, values] : traces)
        {
            session.programTrace( pointName, traceSourceOf( std::move( values)));
        }

        return session;
    }

    auto ScriptedSession::fetch(
        const std::string_view                    name,
        std::string_view,
        const QuantityKind                        kind,
        const std::function<QuantityVariant()> &) -> QuantityVariant
    {
        //
        // Absence and exhaustion are both diagnosed by the slot map, in the
        // words this seam was given -- see detail::ObservationSlots in
        // core/session/session.hpp. What is left here is the one thing that is genuinely
        // this seam's own: a value programmed in one unit and fetched as
        // another.
        //
        auto value = mSources.next( name);

        checkKind( name, value, kind);

        return value;
    }

    auto ScriptedSession::fetchData(
        const std::string_view          name,
        std::string_view,
        const std::function<Bytes()> &) -> Bytes
    {
        //
        // Nothing beside the slot map here, unlike fetch above: a payload has
        // no unit for a programmed answer to disagree with the call site about.
        //
        return mDataSources.next( name);
    }

    auto ScriptedSession::fetchFlag(
        const std::string_view         name,
        std::string_view,
        const std::function<bool()> &) -> bool
    {
        //
        // Exhaustion matters most on this seam of the three, which is worth
        // saying somewhere: silently repeating the last answer would be a
        // "capture completed" that was never observed, with real measurements
        // checked against it. detail::ObservationSlots throws instead.
        //
        return mFlagSources.next( name);
    }

    auto ScriptedSession::fetchTrace(
        const std::string_view              name,
        std::string_view,
        const std::function<Waveform()> &) -> Waveform
    {
        return mTraceSources.next( name);
    }

    auto RecordingSession::record(
        const std::string_view  name,
        const std::string_view  instrumentId,
        RecordedValue           value) -> void
    {
        const auto wallClockMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        //
        // Which test this belongs to, taken from the journal rather than
        // threaded in. The journal is already stamping every event with it (see
        // JournalEvent in core/journal/journal.hpp) for exactly the same reason a
        // recording needs it, and it is reached the same way -- process-wide --
        // so no seam between here and a script has to learn to carry a test
        // name it has no other use for.
        //
        // Outside any test the journal answers empty, which is a hook --
        // RUN_SETUP/RUN_TEARDOWN, or a group's own SETUP/TEARDOWN: recorded
        // under kRunScope, so that a later replay of one test keeps their
        // readings. Normalised here rather than at the writer
        // because this is the one place a run makes a sample, and samples() has
        // to agree with the file about what it says.
        //
        const auto runningTest = core::journal().currentTest();

        auto sample = RecordedSample{
            .mSequence            = mNextSequence++,
            .mWallClockUnixMillis = static_cast<std::int64_t>( wallClockMillis),
            .mTestId              = runningTest.empty() ? std::string( kRunScope) : std::string( runningTest),
            .mPointName           = std::string( name),
            .mInstrumentId        = std::string( instrumentId),
            .mValue               = std::move( value)
        };

        //
        // One ordered stream whichever way it is going, which is the whole of
        // what makes a replay faithful (see core::RecordedSample). A run that
        // measured a rail, read a reply and measured again has to come back in
        // that order, and the sequence number above is stamped here for both
        // paths so it cannot depend on which one a run happened to take.
        //
        if( mWriter)
        {
            mWriter->write( sample);
        }
        else
        {
            mSamples.push_back( std::move( sample));
        }
    }

    auto RecordingSession::fetch(
        const std::string_view                    name,
        const std::string_view                    instrumentId,
        const QuantityKind                        kind,
        const std::function<QuantityVariant()> &  liveRead) -> QuantityVariant
    {
        auto value = mInner.fetch( name, instrumentId, kind, liveRead);

        record( name, instrumentId, value);

        return value;
    }

    auto RecordingSession::fetchData(
        const std::string_view          name,
        const std::string_view          instrumentId,
        const std::function<Bytes()> &  liveRead) -> Bytes
    {
        auto value = mInner.fetchData( name, instrumentId, liveRead);

        record( name, instrumentId, value);

        return value;
    }

    auto RecordingSession::fetchFlag(
        const std::string_view         name,
        const std::string_view         instrumentId,
        const std::function<bool()> &  liveRead) -> bool
    {
        const auto value = mInner.fetchFlag( name, instrumentId, liveRead);

        //
        // Into the same ordered stream as the other two seams: an Await sits in
        // the middle of a sequence whose meaning depends on where it sits --
        // arm, drop the rail, wait, then measure the transient that the wait is
        // the evidence for.
        //
        record( name, instrumentId, value);

        return value;
    }

    auto RecordingSession::fetchTrace(
        const std::string_view              name,
        const std::string_view              instrumentId,
        const std::function<Waveform()> &   liveRead) -> Waveform
    {
        auto value = mInner.fetchTrace( name, instrumentId, liveRead);

        record( name, instrumentId, value);

        return value;
    }
} // namespace core
