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
    //   Measure( Dmm1.voltage(), at( DeviceX::Output5V));
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
            template<auto Loc, quantities::QuantityType QuantityT, typename InstrumentT, SensePath Sense>
            [[nodiscard]]
            auto operator()( Port<QuantityT, InstrumentT, Sense> port, const At<AdapterPointTag<Loc>> & wrapped) -> QuantityT
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

                    auto value = QuantityVariant{ port.rawMeasure() };

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

                const auto value = asQuantity<QuantityT>(
                    activeSession().fetch( point.Name, instrumentName, kind, liveRead));

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
                journal().post( JournalRecord{
                    .Method     = Verb::Measure,
                    .Subject    = std::string( point.Name),
                    .Detail     = std::string( point.Description),
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
                const auto key = instrumentName + "." + std::string( to_string( kind));

                auto liveRead = [&]() -> QuantityVariant
                {
                    return QuantityVariant{ port.rawMeasure() };
                };

                const auto value = asQuantity<QuantityT>( activeSession().fetch( key, instrumentName, kind, liveRead));

                journal().post( JournalRecord{
                    .Method     = Verb::Measure,
                    .Subject    = key,
                    .Detail     = "instrument readback",
                    .Instrument = instrumentName,
                    .Value      = describeValue( value),
                    .Numeric    = value.value(),
                    .Unit       = std::string( value.symbol())
                });

                return value;
            }

            //
            // Feeds a canned value for a point, bypassing hal entirely --
            // for script unit tests. Switches this engine to use injected/
            // loaded values if it wasn't already (no separate "now use
            // this" call needed); useLive() goes back, and discards these
            // again. Keyed by the point's Name (e.g. "Output5V"), matching
            // what operator() above passes to the session.
            //
            // Programming accumulates until then: injecting two points arms
            // both, which is what a script measuring several of them needs.
            // That is also why useLive() has to discard rather than merely
            // switch away -- see its own comment.
            //
            auto inject( std::string_view pointName, QuantityVariant value) -> void
            {
                mScripted.program( pointName, std::move( value));
                mSwitchable.use( mScripted);
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
            auto inject( std::string_view pointName, ValueSource source) -> void
            {
                mScripted.program( pointName, std::move( source));
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

            //
            // Returns to real routing + real instrument reads, and discards
            // whatever was injected or loaded -- the full inverse of inject()
            // and load(), not half of one.
            //
            // Discarding is the point, and it used to be missing. Switching the
            // session back on its own left every programmed point still armed
            // in mScripted, which is unreachable state that can only do harm:
            // there is no useScripted() to re-enter with, so the only way back
            // is another inject() -- which programs afresh anyway -- and
            // meanwhile that later inject() would silently re-arm every point
            // programmed before it. A script then measuring a point *this*
            // caller never programmed would quietly receive a stale canned
            // value instead of the hard "nothing programmed for point" error
            // ScriptedSession::fetch exists to raise. A test that passes on a
            // value left behind by the test before it is exactly the kind of
            // quietly-wrong result this framework is built to refuse.
            //
            // It also makes the pair symmetric with what load() already
            // documents about itself: entering a scripted mode discards what
            // the previous one had, so leaving it discards too.
            //
            // Assignment rather than a clear() on ScriptedSession, because
            // load() above already establishes that idiom for exactly this --
            // "start from a fresh set of sources" -- and a second spelling of
            // one operation is a second thing to keep in step.
            //
            auto useLive() -> void
            {
                mSwitchable.useDefault();

                mScripted = ScriptedSession{};
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
