#include "core/session.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <variant>

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

    auto ScriptedSession::program( const std::string_view name, QuantityVariant value) -> void
    {
        program( name, constantSource( std::move( value)));
    }

    auto ScriptedSession::program( const std::string_view name, ValueSource source) -> void
    {
        mSources.insert_or_assign( std::string( name), std::move( source));
    }

    auto ScriptedSession::programData( const std::string_view name, Bytes value) -> void
    {
        programData( name, constantDataSource( std::move( value)));
    }

    auto ScriptedSession::programData( const std::string_view name, DataSource source) -> void
    {
        mDataSources.insert_or_assign( std::string( name), std::move( source));
    }

    auto ScriptedSession::programFlag( const std::string_view name, const bool value) -> void
    {
        programFlag( name, constantFlagSource( value));
    }

    auto ScriptedSession::programFlag( const std::string_view name, FlagSource source) -> void
    {
        mFlagSources.insert_or_assign( std::string( name), std::move( source));
    }

    auto ScriptedSession::loadFromFile( const std::string & path) -> ScriptedSession
    {
        std::ifstream in( path);

        if( !in)
        {
            throw std::runtime_error( "ScriptedSession::loadFromFile: could not open '" + path + "'");
        }

        //
        // Collected per point first, then turned into one source each. A
        // recording interleaves points in the order they were measured, so the
        // per-point sequence only exists once the whole file has been read --
        // there is nothing to hand to sourceOf() until then.
        //
        std::unordered_map<std::string, std::vector<QuantityVariant>>  samples;
        std::unordered_map<std::string, std::vector<Bytes>>            payloads;
        std::unordered_map<std::string, std::vector<bool>>             flags;

        //
        // Sorted into the two seams by which alternative each row holds, not by
        // anything about its name -- a recording is a flat, interleaved stream
        // and a row says for itself what it is (see core::RecordedValue).
        //
        for( auto & sample : readRecording( in))
        {
            if( auto * payload = std::get_if<Bytes>( &sample.mValue))
            {
                payloads[ sample.mPointName].push_back( std::move( *payload));
            }
            else if( const auto * flag = std::get_if<bool>( &sample.mValue))
            {
                flags[ sample.mPointName].push_back( *flag);
            }
            else
            {
                samples[ sample.mPointName].push_back( std::get<QuantityVariant>( sample.mValue));
            }
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

        return session;
    }

    auto ScriptedSession::fetch(
        const std::string_view                    name,
        std::string_view,
        const QuantityKind                        kind,
        const std::function<QuantityVariant()> &) -> QuantityVariant
    {
        const auto key = std::string( name);
        const auto entry = mSources.find( key);

        if( entry == mSources.end())
        {
            throw std::runtime_error(
                "ScriptedSession: nothing programmed for point '" + key +
                "' -- program() it, or load a recording that covers it");
        }

        //
        // A source that has run out is a hard error rather than a silent repeat
        // of its last value: a script measuring a point more times than the
        // test authored values for it has diverged from what was expected, and
        // that is precisely the thing worth failing on. A source that should
        // never run out simply never returns nullopt -- see core::ValueSource.
        //
        const auto value = entry->second();

        if( !value)
        {
            throw std::runtime_error(
                "ScriptedSession: no programmed value left for point '" + key +
                "' -- either program() more, load a longer recording, or the script is measuring it more times than expected");
        }

        checkKind( name, *value, kind);

        return *value;
    }

    auto ScriptedSession::fetchData(
        const std::string_view          name,
        std::string_view,
        const std::function<Bytes()> &) -> Bytes
    {
        const auto key   = std::string( name);
        const auto entry = mDataSources.find( key);

        if( entry == mDataSources.end())
        {
            throw std::runtime_error(
                "ScriptedSession: nothing programmed for '" + key +
                "' -- programData() it, or load a recording that covers it");
        }

        //
        // Exhaustion is a hard error for the same reason it is on the quantity
        // side: a script reading a port more times than the test authored
        // replies for it has diverged from what was expected, and silently
        // repeating the last reply would hide exactly that.
        //
        auto value = entry->second();

        if( !value)
        {
            throw std::runtime_error(
                "ScriptedSession: no programmed payload left for '" + key +
                "' -- either programData() more, load a longer recording, or the script is reading it more times than expected");
        }

        return std::move( *value);
    }

    auto ScriptedSession::fetchFlag(
        const std::string_view         name,
        std::string_view,
        const std::function<bool()> &) -> bool
    {
        const auto key   = std::string( name);
        const auto entry = mFlagSources.find( key);

        if( entry == mFlagSources.end())
        {
            throw std::runtime_error(
                "ScriptedSession: nothing programmed for '" + key +
                "' -- programFlag() it, or load a recording that covers it");
        }

        //
        // Exhaustion is a hard error, as it is on the other two seams: a
        // script that armed and awaited more captures than the test authored
        // answers for has diverged from what was expected. Silently repeating
        // the last answer would be the worst of the options here -- a
        // "completed" that was never observed, with real measurements checked
        // against it.
        //
        const auto value = entry->second();

        if( !value)
        {
            throw std::runtime_error(
                "ScriptedSession: no programmed flag left for '" + key +
                "' -- either programFlag() more, load a longer recording, or the script is awaiting it more times than expected");
        }

        return *value;
    }

    auto RecordingSession::fetch(
        const std::string_view                    name,
        const std::string_view                    instrumentId,
        const QuantityKind                        kind,
        const std::function<QuantityVariant()> &  liveRead) -> QuantityVariant
    {
        auto value = mInner.fetch( name, instrumentId, kind, liveRead);

        const auto wallClockMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        mSamples.push_back( RecordedSample{
            .mSequence            = mNextSequence++,
            .mWallClockUnixMillis = static_cast<std::int64_t>( wallClockMillis),
            .mPointName           = std::string( name),
            .mInstrumentId        = std::string( instrumentId),
            .mValue               = value
        });

        return value;
    }

    auto RecordingSession::fetchData(
        const std::string_view          name,
        const std::string_view          instrumentId,
        const std::function<Bytes()> &  liveRead) -> Bytes
    {
        auto value = mInner.fetchData( name, instrumentId, liveRead);

        const auto wallClockMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        //
        // Into the same ordered vector as the quantity samples, not a second
        // one beside it. Replay correctness is a matter of order (see
        // core::RecordedSample), and a run that measured a rail, read a reply
        // and measured again has to come back in that order -- which two
        // vectors, each with its own sequence, could not reconstruct.
        //
        mSamples.push_back( RecordedSample{
            .mSequence            = mNextSequence++,
            .mWallClockUnixMillis = static_cast<std::int64_t>( wallClockMillis),
            .mPointName           = std::string( name),
            .mInstrumentId        = std::string( instrumentId),
            .mValue               = value
        });

        return value;
    }

    auto RecordingSession::fetchFlag(
        const std::string_view         name,
        const std::string_view         instrumentId,
        const std::function<bool()> &  liveRead) -> bool
    {
        const auto value = mInner.fetchFlag( name, instrumentId, liveRead);

        const auto wallClockMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        //
        // Into the same ordered vector as the other two seams, for the reason
        // fetchData's own comment gives: order is the whole of what makes a
        // replay faithful, and an Await sits in the middle of a sequence whose
        // meaning depends on it -- arm, drop the rail, wait, then measure the
        // transient that the wait is the evidence for.
        //
        mSamples.push_back( RecordedSample{
            .mSequence            = mNextSequence++,
            .mWallClockUnixMillis = static_cast<std::int64_t>( wallClockMillis),
            .mPointName           = std::string( name),
            .mInstrumentId        = std::string( instrumentId),
            .mValue               = value
        });

        return value;
    }
} // namespace core
