#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/adapter.hpp"
#include "core/at.hpp"
#include "core/format.hpp"
#include "core/journal.hpp"
#include "core/port.hpp"
#include "core/quantity_kind.hpp"
#include "core/recording.hpp"
#include "core/session.hpp"

namespace core
{
    //
    // The mechanism behind a single callable Measure object:
    //
    //   Measure( Dmm1.voltage(), DeviceX::Output5V);
    //
    // Generic over three externally-supplied types, each a "build stage"
    // concern this header knows nothing about:
    //   - FabricT:           something with .connect(path)/.disconnect(path)
    //                        -- see hal::SwitchFabric
    //   - InstrumentWiringT: something with .find(instrumentId) -> a
    //                        hal::Path FabricT::connect()/disconnect()
    //                        accepts -- see hal::InstrumentWiring
    //   - ConnectorWiringT:  something with .find(location) -> a hal::Path
    //                        FabricT::connect()/disconnect() accepts -- see
    //                        hal::ConnectorWiring
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
            auto operator()( Port<QuantityFor<Kind>, InstrumentT> port, const At<AdapterPointTag<Loc, Kind>> & wrapped) -> QuantityFor<Kind>
            {
                const auto & point        = wrapped.point;
                const auto   instrumentId = port.instrumentId();

                auto liveRead = [&]() -> QuantityVariant
                {
                    //
                    // Each side's Path (see hal/switch_fabric.hpp) may be
                    // more than one element -- a mux narrowing down to a
                    // matrix, or an instrument passing through more than
                    // one relay before its own dedicated matrix column --
                    // so the composed route is a concatenation, not a
                    // fixed two-element list the way it used to be when
                    // both sides were always exactly one hop.
                    //
                    auto path                = mInstrumentWiring.find( instrumentId);
                    const auto connectorPath = mConnectorWiring.find( Loc);

                    path.insert( path.end(), connectorPath.begin(), connectorPath.end());

                    //
                    // A 4-wire (Kelvin) reading additionally needs its
                    // sense leads routed, on both sides -- see
                    // core::MeasureSetup::RequiresSensePath's own comment
                    // for why this is a per-reading flag rather than
                    // something InstrumentWiring/ConnectorWiring decide on
                    // their own. Deliberately separate find()/findSense()
                    // calls, not one combined lookup: a plain 2-wire
                    // reading on the very same instrument must never touch
                    // the sense channels at all.
                    //
                    if( port.setup().RequiresSensePath)
                    {
                        const auto instrumentSense = mInstrumentWiring.findSense( instrumentId);
                        const auto connectorSense  = mConnectorWiring.findSense( Loc);

                        path.insert( path.end(), instrumentSense.begin(), instrumentSense.end());
                        path.insert( path.end(), connectorSense.begin(), connectorSense.end());
                    }

                    //
                    // Connect just long enough to take the reading, then
                    // disconnect again -- rather than leaving the path
                    // routed until some later, unrelated Measure/Connect
                    // call happens to bump it. Both calls are additive/
                    // reference-counted (see hal::SwitchFabric's own
                    // comment), so this never disturbs a path some other
                    // instrument is holding open right now (e.g. a supply
                    // parked on the very point being read here) -- it only
                    // ever opens its own channels back up, and only once
                    // nothing else still needs them.
                    //
                    mFabric.connect( path);

                    auto value = QuantityVariant{ port.rawMeasure() };

                    mFabric.disconnect( path);

                    return value;
                };

                const auto instrumentName = std::string( to_string( instrumentId));

                auto value = activeSession().fetch( point.Name, instrumentName, Kind, liveRead);

                //
                // Logged here, after the session has produced the value and
                // before it is unwrapped -- so one Measure() call is one journal
                // event whether the value came from real hardware, an injected
                // constant, or a replayed recording. That is deliberate: a
                // script's log should say what the script measured, and which
                // session answered is a property of how the run was set up, not
                // of what the test did.
                //
                // Note this is a different artifact from core::RecordingSession
                // (core/session.hpp), which also captures every fetch. That one
                // exists to be *replayed* -- a strict, ordered value sequence,
                // deliberately carrying nothing a human wants and nothing a
                // report needs. This carries the point's description, its unit,
                // and which instrument was used, and is never read back in.
                //
                journal().post( JournalRecord{
                    .Method     = Verb::Measure,
                    .Subject    = std::string( point.Name),
                    .Detail     = std::string( point.Description),
                    .Instrument = instrumentName,
                    .Value      = formatQuantity( value),
                    .Numeric    = rawValue( value),
                    .Unit       = std::string( unitSymbol( Kind))
                });

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
