#pragma once

#include <concepts>
#include <initializer_list>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
    //   Measure( Dmm1.voltage(), at( dut::Output5V));
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
    // location as a compile-time value (see core/adapter.hpp), there is
    // nothing left to look up by name at runtime -- the point IS the lookup
    // result. That is also why what used to be one combined RouteTable (keyed
    // by (location, instrument, kind)) has become two independent tables here: an instrument's matrix/mux channel and a connector pin's
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
    // THORIUM_CRITERIA_VARIANT_TABLES/THORIUM_TEST_CATALOG already play for the
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
            // The point supplies *where* (Loc, a compile-time template
            // parameter -- see core/adapter.hpp); the port supplies *what*
            // (QuantityT). They are independent by design: any instrument port
            // can be pointed at any pin, so measuring current or frequency at a
            // pin whose description calls it a supply rail is an ordinary thing
            // to write. See AdapterPointTag's own comment for why a point used
            // to constrain the quantity and no longer does.
            //
            // The return type is the port's own QuantityT, so unit safety is
            // undiminished: what comes back is exactly the quantity that was
            // asked for, and a criterion in the wrong unit still fails to
            // compile at the Verify() call site.
            //
            // Reachability -- whether InstrumentWiringT/ConnectorWiringT
            // actually have an entry for this instrument/location -- is still
            // a runtime check today; see the TODO in hal/wiring.hpp for why,
            // and for the compile-time upgrade path now that Loc is known at
            // compile time here too.
            //
            // Kind is deduced and then ignored, rather than pinned to
            // PointKind::Signal so that a SOURCE_POINT would fail to match
            // here. That was this overload's first shape, and it banned the
            // very measurement a landing pin is worth declaring for: reading
            // what actually arrives at a cabled rail's DUT pin, which is a
            // different and stricter question than the supply's own readback
            // (see suite/scripts/rig_power_on.cpp, and core::PointKind's own
            // comment). A rail is cabled so the fabric never carries its
            // load current; a high-impedance tap onto the same pin is not
            // that, and forbidding it protected nothing.
            //
            // Which leaves reachability where it already was -- a runtime
            // question. Whether this rig taps a given pin at all is a
            // WIRE_CONNECTOR row, a rig fact, and a point's type carries
            // only DUT facts; there is nothing for a compile-time check here
            // to consult. dut/tests/test_wiring_coverage.cpp is where the
            // two files are checked against each other.
            //
            template<auto Loc, PointKind Kind, quantities::QuantityType QuantityT, typename InstrumentT, SensePath Sense>
            [[nodiscard]]
            auto operator()( Port<QuantityT, InstrumentT, Sense> port, const At<AdapterPointTag<Loc, Kind>> & wrapped) -> QuantityT
            {
                const auto & point        = wrapped.point;
                const auto   instrumentId = port.instrumentId();

                //
                // Set by readOrSubstitute below if the instrument said it
                // could not make this reading, and read again further down for
                // the journal event -- see core::UnmeasurableReading.
                //
                // Empty after a scripted or replayed fetch even when the
                // recorded value was itself a substitution, and that is
                // correct rather than a gap: the reason is something a live
                // instrument said at a particular moment, and a replay is not
                // that instrument saying it again. What replays is the value,
                // which is what the checks below it depend on.
                //
                std::string unmeasurableReason;

                //
                // The reading, with the one exception a driver is allowed to
                // raise already resolved to a value -- so the caller below can
                // treat "the instrument could not measure" as an ordinary
                // outcome, which is what lets the run carry on to the next
                // check instead of unwinding out of the script.
                //
                // Note this deliberately sits *inside* liveRead's
                // connect/disconnect pair rather than around the whole lambda:
                // an unmeasurable reading is now a normal path, and a normal
                // path must not leave the fabric holding channels closed. Other
                // exception types still propagate, and still leak the route --
                // an instrument that throws something else has failed in a way
                // this engine cannot reason about, and hal::safeRig() is what
                // answers for that (see hal/safing.hpp).
                //
                auto readOrSubstitute = [&]() -> QuantityT
                {
                    try
                    {
                        return port.rawMeasure();
                    }
                    catch( const UnmeasurableReading & unmeasurable)
                    {
                        unmeasurableReason = std::string( unmeasurable.reason());

                        return port.unmeasurableValue( unmeasurable.reason());
                    }
                };

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
                    // core::Port::requiresSensePath() for why this is a
                    // property of the port's type rather than something
                    // InstrumentWiring/ConnectorWiring decide on their own.
                    // Deliberately separate find()/findSense() calls, not one
                    // combined lookup: a plain 2-wire reading on the very same
                    // instrument must never touch the sense channels at all.
                    //
                    // if constexpr, not if: a 2-wire reading does not compile
                    // the sense lookups at all, so it cannot pay for them and
                    // cannot fail on a rig that never wired sense leads.
                    //
                    if constexpr( Sense == SensePath::Required)
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

                    auto value = QuantityVariant{ readOrSubstitute() };

                    mFabric.disconnect( path);

                    return value;
                };

                const auto instrumentName = std::string( to_string( instrumentId));

                //
                // Unwrapped to the concrete Quantity<Unit> before anything else
                // looks at it. The session seam has to hand back a type-erased
                // QuantityVariant (ISession::fetch is virtual -- see
                // core/session.hpp), but that erasure ends here: Kind is a
                // template parameter of this function, so everything downstream,
                // the log included, can work with the real type.
                //
                // This used to log from the variant -- formatQuantity( value),
                // rawValue( value), unitSymbol( Kind) -- which dragged the whole
                // runtime kind-to-symbol machinery into the reporting path for no
                // reason, and was quietly inconsistent: the unit came from the
                // *declared* Kind while the value was formatted from whichever
                // alternative the variant actually held, so a session returning
                // the wrong unit produced an event reading "5.02 A" with its unit
                // recorded as "V".
                //
                // The session seam is not a template (ISession::fetch is
                // virtual), so it needs the quantity as a runtime tag -- taken
                // from the port's type rather than from the point, which no
                // longer declares one.
                constexpr auto kind = quantityKindOf<QuantityT>();

                //
                // The point's name, plus the port's qualifier when it carries
                // one -- "Output5V" for an ordinary reading, "Output5V.Vbase"
                // for one of the fifteen different answers an oscilloscope can
                // give about the same pin (see hal::DSO8064AChannel).
                //
                // Without this, two measurements taken at one point in one
                // script share a session slot, so a test injecting them by
                // name cannot say which is which and gets them in whatever
                // order the script happens to measure. That is exactly the
                // collision core::Port::qualifiedBy was introduced to solve
                // for a three-phase source's per-phase readbacks; this is the
                // same mechanism reaching the routed overload.
                //
                // Opt-in, and therefore backward compatible: a port that names
                // no qualifier keys on the bare point name exactly as it
                // always has, so every existing injection and every existing
                // recording is unaffected.
                //
                // What this deliberately does NOT fix: the same collision
                // between two *quantities* at one point --
                // Measure( Dmm1.voltage(), at( p)) and Measure( Dmm1.current(),
                // at( p)) still share the key "p". Folding the QuantityKind
                // into the key would fix it and would rename every key in
                // every existing recording, which is a migration rather than a
                // change. A driver that needs the distinction today can
                // qualify its ports, which is what the scope does.
                //
                const auto qualifier = port.qualifier();

                const auto key = qualifier.empty()
                                     ? std::string( point.Name)
                                     : std::string( point.Name) + "." + std::string( qualifier);

                const auto value = asQuantity<QuantityT>(
                    activeSession().fetch( key, instrumentName, kind, liveRead));

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
                //
                // Formatted from the value's own type, exactly the way
                // core::Verify does it (see core/format.hpp) -- one code path for
                // "how a reading is written down", whether it is being reported
                // or checked.
                //
                //
                // The point's own description, plus what the instrument said
                // if it could not make the reading. Appended to the existing
                // detail rather than replacing it or taking a field of its
                // own: a reader scanning the log wants "Config clock probe --
                // unmeasurable: required edge not found" on the line that
                // already names the pin, not the reason orphaned somewhere
                // else, and every consumer of Detail keeps working unchanged.
                //
                const auto detail = unmeasurableReason.empty()
                                        ? std::string( point.Description)
                                        : std::string( point.Description) + " -- unmeasurable: " + unmeasurableReason;

                journal().post( JournalRecord{
                    .Method     = Verb::Measure,
                    .Subject    = key,
                    .Detail     = detail,
                    .Instrument = instrumentName,
                    .Value      = describeValue( value),
                    .Numeric    = value.value(),
                    .Unit       = std::string( value.symbol())
                });

                return value;
            }

            //
            // A reading that needs no routing at all: an instrument measuring
            // its own output, over its own interface.
            //
            // Note what is missing -- there is no at(...), and the fabric is
            // never touched. That is the whole point of the overload rather
            // than a shortcut: a supply reporting the current it is delivering
            // is not a signal that travels through the switching matrix to get
            // anywhere, so there is no path to compose, no relay to close, and
            // nothing that could collide with whatever else is currently
            // routed. Asking for at(...) here would be asking which pin to
            // route a measurement that never leaves the instrument.
            //
            // This is how current is actually read on a rig whose matrix
            // carries signals only: a supply's own readback measures its rail
            // at full load, where routing that current through signal relays
            // would be neither possible nor safe (see hal::N6701A's own comment
            // on why its output is hard-wired in the first place).
            //
            // Still goes through the session seam, so injection and replay work
            // exactly as they do for a routed reading -- see the key's own
            // comment below for what to inject against.
            //
            template<quantities::QuantityType QuantityT, typename InstrumentT, SensePath Sense>
            [[nodiscard]]
            auto operator()( Port<QuantityT, InstrumentT, Sense> port) -> QuantityT
            {
                const auto instrumentName = std::string( to_string( port.instrumentId()));

                constexpr auto kind = quantityKindOf<QuantityT>();

                //
                // Sessions key by name, and an instrument readback has no point
                // name to key by -- so it gets "<instrument>.<quantity>", e.g.
                // "DcP1.Current". Qualified by the quantity because one
                // instrument can report several (a supply reports both the
                // voltage it is holding and the current it is delivering), and
                // keying on the instrument alone would make those two the same
                // recording slot.
                //
                // The same argument a second time, for an instrument with more
                // than one output: a three-phase source reports a voltage per
                // phase, and all three share one InstrumentId, so the port may
                // also carry a qualifier naming which one -- "AcP1.B.Voltage"
                // (see core::Port::qualifiedBy and hal::Ac6834B::measuredVoltage).
                // Ports that name a single output leave it empty and key
                // exactly as they always did.
                //
                const auto qualifier = port.qualifier();

                const auto key = qualifier.empty()
                                     ? instrumentName + "." + std::string( to_string( kind))
                                     : instrumentName + "." + std::string( qualifier) + "." + std::string( to_string( kind));

                //
                // Same substitution the routed overload above performs, and
                // for the same reasons -- see its own comment. There is no
                // fabric to unwind here, so the try/catch is the whole of it.
                //
                std::string unmeasurableReason;

                auto liveRead = [&]() -> QuantityVariant
                {
                    try
                    {
                        return QuantityVariant{ port.rawMeasure() };
                    }
                    catch( const UnmeasurableReading & unmeasurable)
                    {
                        unmeasurableReason = std::string( unmeasurable.reason());

                        return QuantityVariant{ port.unmeasurableValue( unmeasurable.reason()) };
                    }
                };

                const auto value = asQuantity<QuantityT>( activeSession().fetch( key, instrumentName, kind, liveRead));

                const auto detail = unmeasurableReason.empty()
                                        ? std::string( "instrument readback")
                                        : "instrument readback -- unmeasurable: " + unmeasurableReason;

                journal().post( JournalRecord{
                    .Method     = Verb::Measure,
                    .Subject    = key,
                    .Detail     = detail,
                    .Instrument = instrumentName,
                    .Value      = describeValue( value),
                    .Numeric    = value.value(),
                    .Unit       = std::string( value.symbol())
                });

                return value;
            }

            //
            // Injection, replay and recording all live on a shared
            // core::SessionBank (see core/session.hpp) rather than on this
            // engine, because Measure is no longer the only verb that observes
            // something -- core::ReadEngine fetches through the same bank so
            // that one run produces one ordered recording. These forwarders
            // stay so that every existing Measure.inject(...) / Measure.load(...)
            // call site reads exactly as it did.
            //
            auto inject( const std::string_view pointName, QuantityVariant value) -> void
            {
                mSessions.inject( pointName, std::move( value));
            }

            //
            // Feeds a *sequence* of canned values for a point, one per
            // measurement, in order -- which is what lets one script be run
            // repeatedly against changing readings, and so what makes a
            // "repeat until it fails" run testable without hardware.
            //
            // Takes any input range of quantities, so the caller picks the
            // algorithm rather than this header offering a fixed menu of them:
            //
            //   Measure.inject( "Output5V", std::vector{ 5.02_V, 5.03_V, 4.90_V });
            //   Measure.inject( "Output5V", rampingRail( 5.0_V, 0.01_V));   // std::generator
            //
            // Running past the end of a finite sequence throws (see
            // core::ScriptedSession::fetch); a generator that never ends never
            // runs out. Both are the source's business, not this call's -- see
            // core::ValueSource.
            //
            // By value, not by forwarding reference: a std::generator is
            // move-only and must be moved in, and a caller wanting to keep its
            // own container can pass a view of it instead.
            //
            template<std::ranges::input_range R>
                requires std::constructible_from<QuantityVariant, std::ranges::range_reference_t<R>>
            auto inject( std::string_view pointName, R values) -> void
            {
                inject( pointName, sourceOf( std::move( values)));
            }

            //
            // The braced-list spelling -- Measure.inject( "Output5V", { 5.02_V,
            // 5.03_V }) -- which needs an overload of its own because a
            // braced-init-list deduces no range type for the template above.
            //
            template<quantities::QuantityType QuantityT>
            auto inject( std::string_view pointName, std::initializer_list<QuantityT> values) -> void
            {
                inject( pointName, std::vector<QuantityT>( values));
            }

            //
            // The seam itself, for a caller that has already built one (or
            // wants a source that is not expressible as a range at all).
            //
            auto inject( const std::string_view pointName, ValueSource source) -> void
            {
                mSessions.inject( pointName, std::move( source));
            }

            auto load( const std::string & path) -> void { mSessions.load( path); }
            auto useLive()                       -> void { mSessions.useLive(); }
            auto startRecording()                -> void { mSessions.startRecording(); }
            auto stopRecording()                 -> void { mSessions.stopRecording(); }

            auto dump( std::ostream & out) const -> void { mSessions.dump( out); }

            //
            // The bank itself, so the other reading verb can be constructed
            // against the same one -- see core::SessionBank on why sharing it
            // is a correctness requirement rather than a convenience, and
            // hal/measure.hpp for where the two globals are tied together.
            //
            [[nodiscard]]
            auto sessions() -> SessionBank &
            {
                return mSessions;
            }

        private:
            [[nodiscard]]
            auto activeSession() -> ISession &
            {
                return mSessions.active();
            }

            FabricT &                  mFabric;
            const InstrumentWiringT &  mInstrumentWiring;
            const ConnectorWiringT &   mConnectorWiring;

            SessionBank                mSessions;
    };
} // namespace core
