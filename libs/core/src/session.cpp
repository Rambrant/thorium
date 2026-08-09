#include "core/session.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>

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

    auto ScriptedSession::program( const std::string_view name, QuantityVariant value) -> void
    {
        program( name, constantSource( std::move( value)));
    }

    auto ScriptedSession::program( const std::string_view name, ValueSource source) -> void
    {
        mSources.insert_or_assign( std::string( name), std::move( source));
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
        std::unordered_map<std::string, std::vector<QuantityVariant>> samples;

        for( const auto & sample : readRecording( in))
        {
            samples[ sample.mPointName].push_back( sample.mValue);
        }

        ScriptedSession session;

        for( auto & [ pointName, values] : samples)
        {
            session.program( pointName, sourceOf( std::move( values)));
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
            .mKind                = kind,
            .mValue               = value
        });

        return value;
    }
} // namespace core
