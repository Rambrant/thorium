#include "dut/measure_engine.hpp"

namespace dut
{
    //
    // TODO(reflection): this lookup, and the RouteTable lookup in route()
    // below, are runtime checks only because the Adapter table is a plain
    // runtime vector, searched by a std::string_view that's an ordinary
    // function parameter -- there is no macro/reflection layer yet giving
    // the compiler a compile-time picture of "this name resolves to this
    // location with this quantity". Once the reflection-based get<"id">()
    // work in core/criterion.hpp is verified on GCC 16, both this and the
    // RouteTable lookup (which has the matching TODO) are candidates to move
    // to compile-time static_asserts. Until then, a script that misspells a
    // point name or asks for the wrong quantity fails when Measure is
    // called, not at compile time.
    //
    auto MeasureEngine::resolve( const std::string_view pointName, const core::QuantityKind expectedKind) const -> hal::VpcLocation
    {
        const auto point = mAdapter.find( pointName);

        if( !point)
        {
            throw std::runtime_error(
                "Measure: adapter '" + std::string( mAdapter.name()) +
                "' has no point named '" + std::string( pointName) + "'");
        }

        if( point->kind != expectedKind)
        {
            throw std::runtime_error(
                "Measure: point '" + std::string( pointName) + "' is declared as " +
                std::string( core::to_string( point->kind)) + " but was measured as " +
                std::string( core::to_string( expectedKind)));
        }

        return point->location;
    }

    auto MeasureEngine::route( const hal::VpcLocation location, const hal::InstrumentId instrument, const core::QuantityKind kind) -> void
    {
        const auto & path = mRoutes.find( location, instrument, kind);
        mFabric.route( path);
    }

    auto MeasureEngine::activeSession() -> core::ISession &
    {
        return mRecording ? static_cast<core::ISession &>( mRecorder) : static_cast<core::ISession &>( mSwitchable);
    }

    auto MeasureEngine::inject( const std::string_view pointName, core::QuantityVariant value) -> void
    {
        mScripted.program( pointName, std::move( value));
        mSwitchable.use( mScripted);
    }

    auto MeasureEngine::load( const std::string & path) -> void
    {
        mScripted = core::ScriptedSession::loadFromFile( path);
        mSwitchable.use( mScripted);
    }

    auto MeasureEngine::useLive() -> void
    {
        mSwitchable.useDefault();
    }

    auto MeasureEngine::startRecording() -> void
    {
        mRecording = true;
    }

    auto MeasureEngine::stopRecording() -> void
    {
        mRecording = false;
    }

    auto MeasureEngine::dump( std::ostream & out) const -> void
    {
        core::writeRecording( out, mRecorder.samples());
    }
} // namespace dut
