#pragma once

#include <stdexcept>

namespace core
{
    //
    // Apply/Remove engines: the generic sourcing counterparts to
    // MeasureEngine, called as:
    //
    //     Apply(  Dcp1.dc( at( Output24V)).voltage( 24_V).currentLimit( 7_A));
    //     Remove( Dcp1.dc( at( Output24V)));
    //     Apply(  Acp1.threePhaseWye({ .a=at(A), .b=at(B), .c=at(C) })
    //             .phaseVoltage( 115_V).frequency( 400_Hz).currentLimit( 3_A));
    //
    // Symmetric with MeasureEngine: same layered session/injection/recording
    // seams -- eventually -- so a script's sourcing calls can be recorded
    // and replayed the same way a measurement can. Kept minimal for the
    // first pass; the session hookup will be added in a subsequent step.
    //
    // Generic over three externally-supplied types:
    //   - FabricT:           see MeasureEngine
    //   - InstrumentWiringT: see MeasureEngine
    //   - ConnectorWiringT:  see MeasureEngine
    //
    // Dispatch to the actual instrument driver happens via two ADL
    // customization points (unqualified applyDriver/removeDriver calls
    // inside operator() below), the same trick core/measure.hpp uses for
    // to_string(instrumentId): a hal-side overload for each concrete
    // builder type resolves via ADL, and this header stays free of any
    // hal:: dependency.
    //
    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    class ApplyEngine
    {
        public:
            ApplyEngine( FabricT & fabric, const InstrumentWiringT & instrumentWiring, const ConnectorWiringT & connectorWiring) :
                mFabric( fabric),
                mInstrumentWiring( instrumentWiring),
                mConnectorWiring( connectorWiring)
            {}

            //
            // Any BuilderT with a .config() member is accepted -- ADL then
            // resolves an applyDriver(fabric, instrumentWiring, connectorWiring, config)
            // free function for that config's concrete type. That is what
            // makes the engine work with an open-ended set of instrument
            // types (DcSourceBuilder today, AcSourceBuilder today, whatever
            // else later) without this header naming any of them.
            //
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) -> void
            {
                applyDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());
            }

        private:
            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;
    };

    template<typename FabricT, typename InstrumentWiringT, typename ConnectorWiringT>
    class RemoveEngine
    {
        public:
            RemoveEngine( FabricT & fabric, const InstrumentWiringT & instrumentWiring, const ConnectorWiringT & connectorWiring) :
                mFabric( fabric),
                mInstrumentWiring( instrumentWiring),
                mConnectorWiring( connectorWiring)
            {}

            //
            // Same ADL trick as Apply's operator() above -- resolves
            // removeDriver(fabric, instrumentWiring, connectorWiring, config)
            // for each config's concrete type. Remove takes the same
            // builder shape as Apply, but only reads the "which point"
            // fields, so a bare .dc(at(...)) with no .voltage()/.currentLimit()
            // is enough at the call site.
            //
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) -> void
            {
                removeDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());
            }

        private:
            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;
    };
} // namespace core
