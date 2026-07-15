#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/port.hpp"
#include "core/quantity_kind.hpp"
#include "core/recording.hpp"
#include "core/session.hpp"

namespace core
{
    //
    // The mechanism behind a single callable Measure object:
    //
    //   Measure( Dmm1.voltage(), "5VOutput");
    //
    // Generic over three externally-supplied types, each a "build stage"
    // concern this header knows nothing about:
    //   - FabricT:     something with .route(path) -- see hal::SwitchFabric
    //   - RouteTableT: something with .find(location, instrumentId, kind)
    //                  returning a path FabricT::route() accepts -- see
    //                  hal::RouteTable
    //   - AdapterT:    something with .find(name) returning an optional-like
    //                  point with .location/.kind members, plus a .name()
    //                  for error messages -- see dut::Adapter
    //
    // This is what lets the low-level machinery here (this file, core::Port,
    // core/session.hpp) be lifted into a standalone library with zero
    // dependency on any specific rig's hal/dut types: the concrete
    // FabricT/RouteTableT/AdapterT are only ever named where the concrete
    // instance is put together -- see dut/measure.cpp -- the same "inject
    // the project-specific bit at the build stage" role
    // THORIUM_ACTIVE_CRITERIA/THORIUM_TEST_CATALOG already play for the
    // criteria variants and the test catalog (see core/active_criteria.hpp,
    // core/active_test_catalog.hpp).
    //
    // The one place this reaches for a project-specific name is
    // to_string(instrumentId) inside operator() below -- deliberately
    // unqualified, so it resolves via ADL to e.g. hal::to_string(InstrumentId)
    // once instantiated with hal's types, without this header ever
    // #include-ing anything from hal.
    //
    template<typename FabricT, typename RouteTableT, typename AdapterT>
    class MeasureEngine
    {
        public:
            MeasureEngine( FabricT & fabric, const RouteTableT & routes, const AdapterT & adapter) :
                mFabric( fabric),
                mRoutes( routes),
                mAdapter( adapter)
            {}

            //
            // Resolves pointName on this DUT's Adapter, checks its declared
            // quantity against QuantityT, then fetches through whichever
            // session is currently active (see below) -- live routing by
            // default. See the TODO in resolve()'s definition for why both
            // that check and the RouteTable lookup inside operator() are
            // runtime checks today rather than compile-time ones.
            //
            template<quantities::QuantityType QuantityT, typename InstrumentT>
            [[nodiscard]]
            auto operator()( Port<QuantityT, InstrumentT> port, std::string_view pointName) -> QuantityT
            {
                constexpr auto expectedKind = quantityKindOf<QuantityT>();

                const auto location     = resolve( pointName, expectedKind);
                const auto instrumentId = port.instrumentId();

                auto liveRead = [&]() -> QuantityVariant
                {
                    mFabric.route( mRoutes.find( location, instrumentId, expectedKind));
                    return QuantityVariant{ port.rawMeasure() };
                };

                auto value = activeSession().fetch( pointName, to_string( instrumentId), expectedKind, liveRead);

                return asQuantity<QuantityT>( value);
            }

            //
            // Feeds a canned value for a point, bypassing hal entirely --
            // for script unit tests. Switches this engine to use injected/
            // loaded values if it wasn't already (no separate "now use
            // this" call needed); see useLive() to go back.
            //
            auto inject( std::string_view pointName, QuantityVariant value) -> void
            {
                mScripted.program( pointName, std::move( value));
                mSwitchable.use( mScripted);
            }

            //
            // Loads a recording (see dump() below) and replays it in place
            // of injected values -- discards whatever was previously
            // injected or loaded.
            //
            auto load( const std::string & path) -> void
            {
                mScripted = ScriptedSession::loadFromFile( path);
                mSwitchable.use( mScripted);
            }

            // Returns to real routing + real instrument reads.
            auto useLive() -> void
            {
                mSwitchable.useDefault();
            }

            // Starts/stops logging every fetch (live, injected, or replayed).
            auto startRecording() -> void
            {
                mRecording = true;
            }

            auto stopRecording() -> void
            {
                mRecording = false;
            }

            // Writes out whatever has been recorded so far.
            auto dump( std::ostream & out) const -> void
            {
                writeRecording( out, mRecorder.samples());
            }

        private:
            //
            // TODO(reflection): this lookup, and the RouteTable lookup in
            // operator() above, are runtime checks only because AdapterT's
            // find() is a plain runtime search, keyed by a std::string_view
            // that's an ordinary function parameter -- there is no
            // macro/reflection layer yet giving the compiler a compile-time
            // picture of "this name resolves to this location with this
            // quantity". Once the reflection-based get<"id">() work in
            // core/criterion.hpp is verified on GCC 16, both this and the
            // RouteTable lookup (which has the matching TODO in
            // hal/route_table.hpp) are candidates to move to compile-time
            // static_asserts. Until then, a script that misspells a point
            // name or asks for the wrong quantity fails when Measure is
            // called, not at compile time.
            //
            [[nodiscard]]
            auto resolve( std::string_view pointName, QuantityKind expectedKind) const
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
                        std::string( to_string( point->kind)) + " but was measured as " +
                        std::string( to_string( expectedKind)));
                }

                return point->location;
            }

            [[nodiscard]]
            auto activeSession() -> ISession &
            {
                return mRecording ? static_cast<ISession &>( mRecorder) : static_cast<ISession &>( mSwitchable);
            }

            FabricT &            mFabric;
            const RouteTableT &  mRoutes;
            const AdapterT &     mAdapter;

            LiveSession        mLive;
            ScriptedSession    mScripted;
            SwitchableSession  mSwitchable{ mLive };
            RecordingSession   mRecorder{ mSwitchable };
            bool               mRecording{ false };
    };
} // namespace core
