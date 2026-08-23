#pragma once

#include <string>

#include "core/adapter.hpp"
#include "core/at.hpp"
#include "core/bench.hpp"
#include "core/describe.hpp"
#include "core/journal.hpp"

namespace core
{
    //
    // Connect/Disconnect: the fast, fabric-only half of what Apply/Remove
    // used to do together -- close (or open) exactly the relay path a
    // source needs, with no instrument I/O at all. Additive, not
    // exclusive -- see hal::SwitchFabric::connect()/disconnect() -- so
    // connecting or disconnecting one instrument's path never disturbs
    // whatever else is currently routed. That path can be a genuine
    // matrix-plus-mux route chosen at the call site (a measuring
    // instrument like hal::DSO8064A, which takes an at(...) point), or
    // just an instrument's own fixed channel(s) with nothing left to
    // choose (hal::N6701A/hal::Ac6834B -- see their own comments on why a
    // real power rail is hard-wired rather than routed) -- Connect/
    // Disconnect don't need to know which; that's entirely up to what
    // connectDriver/disconnectDriver does for the concrete config type.
    //
    //     Connect( Dcp1.dc());
    //     Disconnect( Dcp1.dc());
    //
    // The order these two are called in relative to Apply/Remove is a
    // hardware argument rather than a code one -- the relay should move while
    // the path is dead, at both ends of the sequence -- and nothing in either
    // file enforces it. See core/source.hpp's own comment, which states the
    // rule, explains the wear it exists to avoid, and says when breaking it is
    // the right call.
    //
    // These two live apart from Apply/Setup/Remove because they need things
    // those do not: they are generic over the same three externally-supplied
    // types as MeasureEngine (see core/measure.hpp) -- FabricT/
    // InstrumentWiringT/ConnectorWiringT are rig facts, injected the same way,
    // where the sourcing verbs are pure instrument I/O and take no rig facts at
    // all. Dispatch is the same ADL trick, via connectDriver/disconnectDriver.
    //
    // Both driver calls below are conditional on a bench being attached -- see
    // core/bench.hpp for the argument, which is the same one every stimulus
    // verb makes. Two things about that are specific to these two, and worth
    // stating rather than leaving to be discovered:
    //
    // The whole call is skipped, which means the in-memory fabric model is not
    // updated either. That is deliberate: a detached run is one whose readings
    // come from a file, and nothing in it reads the model. core::MeasureEngine
    // does its own connect/disconnect *inside* the liveRead callback, which a
    // scripted session never invokes, and hal::safeRig() opens everything
    // regardless of what it thought was closed.
    //
    // And hal::SwitchFabric is bookkeeping today, with no I/O of its own -- so
    // "nothing reached the fabric" currently means "nothing was recorded as
    // closed". When relay I/O does arrive it belongs inside that class, behind
    // this same switch, rather than as a second flag beside it.
    //
    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    class ConnectEngine
    {
        public:
            ConnectEngine( FabricT & fabric, const InstrumentWiringT & instrumentWiring, const ConnectorWiringT & connectorWiring) :
                mFabric( fabric),
                mInstrumentWiring( instrumentWiring),
                mConnectorWiring( connectorWiring)
            {}

            template<typename BuilderT>
            auto operator()( const BuilderT & builder) -> void
            {
                if( bench().isAttached())
                {
                    connectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());
                }

                detail::postSourceEvent( Verb::Connect, builder.config(), false);
            }

            //
            // The routed form: an instrument that can reach more than one place
            // says which, and the destination is a whole multi-wire interface
            // rather than a pin (see core::AdapterBundle).
            //
            // A bundle rather than a point, because the instruments that need
            // this are the ones no single wire is any use to. An RS232 console
            // is a transmit line, a receive line and a return; connecting two
            // of the three is not a degraded link, it is no link. Making the
            // bundle the unit means a script cannot express the half-connected
            // case at all, and means adding a line to the interface in
            // dut/adapter.inc changes what Connect closes without touching a
            // single call site.
            //
            // The bundle's type is forwarded to connectDriver rather than being
            // expanded here, because how an interface's lines pair up with an
            // instrument's own channels is the driver's business -- this engine
            // has no idea which of them is transmit. See hal::Racal1260's
            // connectDriver, and hal::bundleLocations for the walk it uses.
            //
            template<typename BuilderT, typename BundleT>
            auto operator()( const BuilderT & builder, const At<AdapterBundle<BundleT>> & wrapped) -> void
            {
                if( bench().isAttached())
                {
                    connectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config(), wrapped.point);
                }

                //
                // Subject is still the instrument, matching every other
                // sourcing event, with the interface named in the settings
                // slot -- a reader filtering the machine log by instrument sees
                // this Connect alongside the Setup and Write that follow it.
                //
                const auto described = describeConfig( builder.config());

                journal().post( JournalRecord{
                    .Method     = Verb::Connect,
                    .Subject    = described.Instrument,

                    //
                    // The marker replaces the interface's description rather
                    // than joining it, the way core::WriteEngine's replaces
                    // "sent": a Detail is one sentence about what happened, and
                    // what happened is that nothing did. The interface is still
                    // named in the value column beside it.
                    //
                    .Detail     = bench().isAttached()
                                      ? std::string( wrapped.point.Description)
                                      : std::string( kDetachedDetail),
                    .Instrument = described.Instrument,
                    .Value      = std::string( wrapped.point.Name)
                });
            }

        private:
            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;
    };

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    class DisconnectEngine
    {
        public:
            DisconnectEngine( FabricT & fabric, const InstrumentWiringT & instrumentWiring, const ConnectorWiringT & connectorWiring) :
                mFabric( fabric),
                mInstrumentWiring( instrumentWiring),
                mConnectorWiring( connectorWiring)
            {}

            template<typename BuilderT>
            auto operator()( const BuilderT & builder) -> void
            {
                if( bench().isAttached())
                {
                    disconnectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());
                }

                detail::postSourceEvent( Verb::Disconnect, builder.config(), false);
            }

            // The inverse of ConnectEngine's bundle overload -- see its comment.
            template<typename BuilderT, typename BundleT>
            auto operator()( const BuilderT & builder, const At<AdapterBundle<BundleT>> & wrapped) -> void
            {
                if( bench().isAttached())
                {
                    disconnectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config(), wrapped.point);
                }

                const auto described = describeConfig( builder.config());

                journal().post( JournalRecord{
                    .Method     = Verb::Disconnect,
                    .Subject    = described.Instrument,
                    .Detail     = bench().isAttached()
                                      ? std::string( wrapped.point.Description)
                                      : std::string( kDetachedDetail),
                    .Instrument = described.Instrument,
                    .Value      = std::string( wrapped.point.Name)
                });
            }

        private:
            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;
    };
} // namespace core
