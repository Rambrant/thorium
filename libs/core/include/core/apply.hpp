#pragma once

#include <stdexcept>
#include <string>

#include "core/journal.hpp"

namespace core
{
    //
    // What an ADL describeConfig(config) hands back for the log -- the two
    // things a log needs about a sourcing or routing step that the config type
    // itself is the only thing able to answer: which instrument, and what it
    // was told to do.
    //
    // A struct rather than one string, because the two are used differently:
    // the instrument id is a key a consumer groups and filters by (it matches
    // hal::InstrumentId's own spelling, and a Measure's Instrument field), while
    // the settings text is prose for a reader. Flattening them would leave a
    // machine consumer parsing "DcP1: 24 V, limit 7 A" apart again.
    //
    // Empty Settings is normal and correct: Remove/Connect/Disconnect have
    // nothing to report beyond which instrument they acted on (removeDriver
    // only ever reads Instrument -- see hal/n6701a.hpp), so only Apply asks for
    // this half at all.
    //
    struct SourceDescription
    {
        std::string Instrument;
        std::string Settings;
    };

    namespace detail
    {
        //
        // Posts one sourcing/routing event. describeConfig is an unqualified
        // call, so it resolves via ADL to the hal-side overload for the concrete
        // config type -- exactly the customization point applyDriver/
        // connectDriver already are (see the engines below), which is what keeps
        // this header free of any hal:: dependency while still logging
        // instrument-specific detail.
        //
        // withSettings is a compile-time choice made by the calling engine
        // rather than something describeConfig decides, so a driver author has
        // one function to write per config type instead of one per verb.
        //
        template<typename ConfigT>
        auto postSourceEvent( const Verb method, const ConfigT & config, const bool withSettings) -> void
        {
            const auto described = describeConfig( config);

            journal().post( JournalRecord{
                .Method     = method,
                .Subject    = described.Instrument,
                .Instrument = described.Instrument,
                .Value      = withSettings ? described.Settings : std::string{}
            });
        }
    } // namespace detail

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
    //     Apply(  Dcp1.dc().voltage( 24_V).currentLimit( 7_A));
    //     Connect( Dcp1.dc());
    //     ...
    //     Remove( Dcp1.dc());
    //     Disconnect( Dcp1.dc());
    //
    // Apply/Remove can be called in either order relative to Connect/
    // Disconnect as far as this code is concerned -- neither reaches for the
    // fabric, so neither cares whether the DUT is actually wired up yet. The
    // hardware is less indifferent, which is why the teardown above is
    // Remove-then-Disconnect rather than the mirror image of the setup:
    // opening a relay with current still flowing through it is hot switching,
    // which arcs, welds contacts and destroys the relay (the same rule
    // hal::safeRig() is ordered by -- see hal/safing.hpp). Output off first,
    // relay open second, unless something makes waiting on the ramp-down worse
    // than the wear -- a safety interlock dropping the connection immediately
    // is exactly that case, and is why this file enforces no order at all.
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

                //
                // Logged after the driver call, not before: the log should say
                // what the rig was actually told, and if applyDriver throws
                // (a real instrument refusing a setpoint) then nothing was
                // applied and nothing should claim it was. Same ordering in all
                // four engines here, and in core::MeasureEngine.
                //
                detail::postSourceEvent( Verb::Apply, builder.config(), true);
            }
    };

    class RemoveEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                removeDriver( builder.config());

                // No settings -- see core::SourceDescription's own comment.
                detail::postSourceEvent( Verb::Remove, builder.config(), false);
            }
    };

    //
    // Connect/Disconnect: the fast, fabric-only half of what Apply/Remove
    // used to do together -- close (or open) exactly the relay path a
    // source needs, with no instrument I/O at all. Additive, not
    // exclusive -- see hal::SwitchFabric::connect()/disconnect() -- so
    // connecting or disconnecting one instrument's path never disturbs
    // whatever else is currently routed. That path can be a genuine
    // matrix-plus-mux route chosen at the call site (a measuring
    // instrument like hal::DSO8064, which takes an at(...) point), or
    // just an instrument's own fixed channel(s) with nothing left to
    // choose (hal::N6701A/hal::Ac6677A -- see their own comments on why a
    // real power rail is hard-wired rather than routed) -- Connect/
    // Disconnect don't need to know which; that's entirely up to what
    // connectDriver/disconnectDriver does for the concrete config type.
    //
    //     Connect( Dcp1.dc());
    //     Disconnect( Dcp1.dc());
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

                detail::postSourceEvent( Verb::Connect, builder.config(), false);
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

                detail::postSourceEvent( Verb::Disconnect, builder.config(), false);
            }

        private:
            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;
    };
} // namespace core
