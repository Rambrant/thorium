#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/adapter.hpp"
#include "core/port.hpp"
#include "core/quantity_kind.hpp"
#include "core/recording.hpp"
#include "core/session.hpp"

namespace core
{
    //
    // The mechanism behind a single callable Measure object:
    //
    //   Measure( Dmm1.voltage(), DeviceX_StdAdapter::Output5V);
    //
    // Generic over three externally-supplied types, each a "build stage"
    // concern this header knows nothing about:
    //   - FabricT:           something with .route(path) -- see hal::SwitchFabric
    //   - InstrumentWiringT: something with .find(instrumentId) -> a channel
    //                        FabricT::route() accepts -- see hal::InstrumentWiring
    //   - ConnectorWiringT:  something with .find(location) -> a channel
    //                        FabricT::route() accepts -- see hal::ConnectorWiring
    //
    // There is no AdapterT any more: since an AdapterPointTag now carries its
    // location and quantity kind as compile-time values (see core/adapter.hpp),
    // there is nothing left to look up by name at runtime -- the point IS the
    // lookup result. That is also why what used to be one combined RouteTable
    // (keyed by (location, instrument, kind)) has become two independent
    // tables here: an instrument's matrix/mux channel and a connector pin's
    // channel are each fixed, static wiring facts on their own, so a route is
    // just their composition at the moment a measurement is taken, not a
    // fact that needs storing per (instrument, pin) pair.
    //
    // This is what lets the low-level machinery here (this file, core::Port,
    // core/session.hpp) be lifted into a standalone library with zero
    // dependency on any specific rig's hal/dut types: the concrete
    // FabricT/InstrumentWiringT/ConnectorWiringT are only ever named where
    // the concrete instance is put together -- see hal/measure.cpp -- the
    // same "inject the project-specific bit at the build stage" role
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
    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    class MeasureEngine
    {
        public:
            MeasureEngine( FabricT & fabric, const InstrumentWiringT & instrumentWiring, const ConnectorWiringT & connectorWiring) :
                mFabric( fabric),
                mInstrumentWiring( instrumentWiring),
                mConnectorWiring( connectorWiring)
            {}

            //
            // point's Loc/Kind are compile-time template parameters (see
            // core/adapter.hpp), so a mismatch between the port's quantity
            // and the point's declared quantity is an ordinary overload-
            // resolution failure -- Port<QuantityFor<Kind>, InstrumentT> is
            // the parameter type, so calling with e.g. dmm1.current() against
            // a Voltage-tagged point simply doesn't match, at compile time.
            //
            // Reachability -- whether InstrumentWiringT/ConnectorWiringT
            // actually have an entry for this instrument/location -- is still
            // a runtime check today; see the TODO in hal/wiring.hpp for why,
            // and for the compile-time upgrade path now that Loc is known at
            // compile time here too.
            //
            template<auto Loc, QuantityKind Kind, typename InstrumentT>
            [[nodiscard]]
            auto operator()( Port<QuantityFor<Kind>, InstrumentT> port, const AdapterPointTag<Loc, Kind> & point) -> QuantityFor<Kind>
            {
                const auto instrumentId = port.instrumentId();

                auto liveRead = [&]() -> QuantityVariant
                {
                    const auto instrumentChannel = mInstrumentWiring.find( instrumentId);
                    const auto connectorChannel  = mConnectorWiring.find( Loc);

                    mFabric.route( { instrumentChannel, connectorChannel });

                    return QuantityVariant{ port.rawMeasure() };
                };

                auto value = activeSession().fetch( point.Name, to_string( instrumentId), Kind, liveRead);

                return asQuantity<QuantityFor<Kind>>( value);
            }

            //
            // Feeds a canned value for a point, bypassing hal entirely --
            // for script unit tests. Switches this engine to use injected/
            // loaded values if it wasn't already (no separate "now use
            // this" call needed); see useLive() to go back. Keyed by the
            // point's Name (e.g. "Output5V"), matching what operator() above
            // passes to the session.
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
            [[nodiscard]]
            auto activeSession() -> ISession &
            {
                return mRecording ? static_cast<ISession &>( mRecorder) : static_cast<ISession &>( mSwitchable);
            }

            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;

            LiveSession        mLive;
            ScriptedSession    mScripted;
            SwitchableSession  mSwitchable{ mLive };
            RecordingSession   mRecorder{ mSwitchable };
            bool               mRecording{ false };
    };
} // namespace core
