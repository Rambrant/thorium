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

    auto ScriptedSession::program( const std::string_view name, QuantityVariant value) -> void
    {
        mSticky.insert_or_assign( std::string( name), std::move( value));
        mQueue.erase( std::string( name));
    }

    auto ScriptedSession::loadFromFile( const std::string & path) -> ScriptedSession
    {
        std::ifstream in( path);

        if( !in)
        {
            throw std::runtime_error( "ScriptedSession::loadFromFile: could not open '" + path + "'");
        }

        ScriptedSession session;

        for( const auto & sample : readRecording( in))
        {
            session.mQueue[ sample.mPointName].push_back( sample.mValue);
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

        if( const auto sticky = mSticky.find( key); sticky != mSticky.end())
        {
            checkKind( name, sticky->second, kind);
            return sticky->second;
        }

        if( const auto queued = mQueue.find( key); queued != mQueue.end() && !queued->second.empty())
        {
            auto value = queued->second.front();
            queued->second.pop_front();

            checkKind( name, value, kind);

            if( queued->second.empty())
            {
                mQueue.erase( queued);
            }

            return value;
        }

        throw std::runtime_error(
            "ScriptedSession: no programmed value left for point '" + key +
            "' -- either program() it, load a longer recording, or the script is measuring it more times than expected");
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
