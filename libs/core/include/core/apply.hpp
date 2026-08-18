#pragma once

#include <stdexcept>
#include <string>

#include "core/adapter.hpp"
#include "core/at.hpp"
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
    //     Connect( Dcp1.dc());
    //     Apply(   Dcp1.dc().voltage( 24_V).currentLimit( 7_A));
    //     ...
    //     Remove(     Dcp1.dc());
    //     Disconnect( Dcp1.dc());
    //
    // Apply/Remove can be called in either order relative to Connect/
    // Disconnect as far as this code is concerned -- neither reaches for the
    // fabric, so neither cares whether the DUT is actually wired up yet. The
    // hardware is not indifferent, and the order above is the one to copy:
    // the relay moves while the path is dead, at both ends of the sequence.
    //
    // Switching a relay with the circuit live is hot switching. On break it is
    // the well-known half -- current flowing through contacts that are parting
    // draws an arc, which erodes and eventually welds them, and it is what
    // hal::safeRig() is ordered to avoid (see hal/safing.hpp). On make it is
    // the same mechanism arriving from the other side: contacts bounce as they
    // close, and closing onto a live supply means each bounce breaks an
    // energised circuit again. So the sequence nests rather than mirrors --
    // Connect, Apply ... Remove, Disconnect -- and a relay that only ever moves
    // cold outlives one that does not by orders of magnitude.
    //
    // Deliberately not enforced here, and that is not an oversight: hot
    // switching is a wear argument, not a correctness one, and there are cases
    // where the wear is the cheaper cost. A safety interlock dropping a
    // connection the instant it is asked to should not first wait out a
    // supply's ramp-down. That call belongs to whoever is writing the sequence,
    // so these engines take no view -- see suite/scripts/rig_power_on.cpp and
    // rig_power_off.cpp for the same ordering written out as a rig's actual
    // power-up and power-down.
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

    //
    // Setup: tell an instrument how to behave, without making it do anything.
    //
    // The distinction from Apply is the whole reason this verb exists, and it
    // is a bench distinction rather than a code one. Apply energises: after it,
    // the rig is doing something to the DUT that it was not doing before, and
    // the ordering rules in this file's comment above -- Connect, Apply ...
    // Remove, Disconnect -- exist because of that. Setup does not: configuring
    // a UART for 9600 8N1, or a scope for a timebase and a trigger level,
    // changes what a *later* verb will mean and changes nothing at the DUT's
    // pins. Collapsing the two would put "the port was configured" and "24 V is
    // now on the backup rail" behind one word in the log, and would drag
    // configuration into the hot-switching argument it has no part in.
    //
    //     Connect( Ser1.rs232(), at( dut::Console));
    //     Setup(   Ser1.rs232().baudRate( 9600).wordLength( 8).parity( Parity::None));
    //     Write(   Ser1.rs232(), "RD 30\r");
    //
    // It touches the fabric not at all, exactly like Apply/Remove, so it takes
    // no wiring and no point -- see this file's comment above on why that
    // separation was made.
    //
    // Dispatched through setupDriver, an ADL customization point of its own
    // rather than a flag on applyDriver. A driver that has settings but nothing
    // to energise (a serial port) writes only setupDriver; one that energises
    // but has nothing to pre-configure writes only applyDriver; and calling a
    // verb an instrument has no implementation for is "no matching function"
    // at compile time rather than a silently ignored call.
    //
    class SetupEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                setupDriver( builder.config());

                // With settings -- the settings *are* what a Setup did.
                detail::postSourceEvent( Verb::Setup, builder.config(), true);
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
    // choose (hal::N6701A/hal::Ac6834B -- see their own comments on why a
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
                connectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config(), wrapped.point);

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
                    .Detail     = std::string( wrapped.point.Description),
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
                disconnectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config());

                detail::postSourceEvent( Verb::Disconnect, builder.config(), false);
            }

            // The inverse of ConnectEngine's bundle overload -- see its comment.
            template<typename BuilderT, typename BundleT>
            auto operator()( const BuilderT & builder, const At<AdapterBundle<BundleT>> & wrapped) -> void
            {
                disconnectDriver( mFabric, mInstrumentWiring, mConnectorWiring, builder.config(), wrapped.point);

                const auto described = describeConfig( builder.config());

                journal().post( JournalRecord{
                    .Method     = Verb::Disconnect,
                    .Subject    = described.Instrument,
                    .Detail     = std::string( wrapped.point.Description),
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
