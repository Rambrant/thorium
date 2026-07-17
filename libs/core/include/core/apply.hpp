#pragma once

#include <stdexcept>

namespace core
{
    //
    // Apply/Remove: programs -- or disables -- a source instrument's own
    // output, exactly as if by hand at its front panel. Deliberately just
    // instrument I/O, with no idea the switching fabric exists: it used to
    // also route the fabric in the same call, but that coupled two facts
    // with very different timing (programming/ramping a supply can take a
    // while; opening or closing a relay is close to instantaneous) and two
    // very different reasons to call them separately -- e.g. programming a
    // supply well ahead of when it's actually switched onto the DUT, or
    // dropping a connection immediately for a safety interlock without
    // waiting on a slower output ramp-down. See ConnectEngine/DisconnectEngine
    // below for the half that moved out.
    //
    //     Apply(  Dcp1.dc( at( Output24V)).voltage( 24_V).currentLimit( 7_A));
    //     Connect( Dcp1.dc( at( Output24V)));
    //     ...
    //     Disconnect( Dcp1.dc( at( Output24V)));
    //     Remove( Dcp1.dc( at( Output24V)));
    //
    // Apply/Remove can be called in either order relative to Connect/
    // Disconnect -- neither reaches for the fabric, so neither cares
    // whether the DUT is actually wired up yet.
    //
    // Dispatch to the actual instrument driver happens via an ADL
    // customization point (an unqualified applyDriver/removeDriver call
    // inside operator() below), the same trick core/measure.hpp uses for
    // to_string(instrumentId): a hal-side overload for each concrete
    // builder type resolves via ADL, and this header stays free of any
    // hal:: dependency. Neither engine carries any state of its own any
    // more -- there's nothing left to inject -- but they stay classes
    // rather than bare functions for symmetry with Connect/Disconnect/
    // Measure, and because the session/injection/recording seam
    // MeasureEngine has is still meant to reach these eventually.
    //
    class ApplyEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                applyDriver( builder.config());
            }
    };

    class RemoveEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                removeDriver( builder.config());
            }
    };

    //
    // Connect/Disconnect: the fast, fabric-only half of what Apply/Remove
    // used to do together -- close (or open) exactly the matrix/mux path a
    // source point needs, with no instrument I/O at all. Additive, not
    // exclusive -- see hal::SwitchFabric::connect()/disconnect() -- so
    // connecting or disconnecting one instrument's path never disturbs
    // whatever else is currently routed.
    //
    //     Connect( Dcp1.dc( at( Output24V)));
    //     Disconnect( Dcp1.dc( at( Output24V)));
    //
    // Generic over the same three externally-supplied types as
    // MeasureEngine (see core/measure.hpp) -- FabricT/InstrumentWiringT/
    // ConnectorWiringT are rig facts, injected the same way. Dispatch is
    // the same ADL trick as Apply/Remove above, via connectDriver/
    // disconnectDriver.
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
                connectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());
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
                disconnectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());
            }

        private:
            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;
    };
} // namespace core
