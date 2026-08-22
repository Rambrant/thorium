#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "core/quantity.hpp"

namespace core
{
    //
    // What a driver throws when the instrument itself reports that it could
    // not make the reading it was asked for.
    //
    // This is not "the instrument is broken" and not "the value was out of
    // tolerance" -- it is the third answer a real instrument can give, and
    // the one a framework that only knows about numbers has nowhere to put.
    // A scope asked for the rise time of a trace with no edge on it has no
    // number to return and knows it; an Infiniium says so by returning
    // 9.99999E+37 and, with :MEASure:SENDvalid on, a reason code from its own
    // table ("required edge not found", "waveform is clipped high", "top and
    // base are equal", ...). A DMM reporting overrange is the same shape. So
    // this lives in core rather than in one driver: the situation is generic
    // even though only hal::DSO8064A answers for it today.
    //
    // Carrying the *reason* rather than a bare "invalid" flag is the whole
    // point. "Rise time unmeasurable" sends an engineer to the scope; "rise
    // time unmeasurable: waveform is clipped high" sends them to the vertical
    // scale, which is where the fault actually is. The legacy ATE this repo
    // replaces had an ISINVALID() predicate that answered only the first,
    // and every script written against it threw the diagnosis away.
    //
    // A std::runtime_error subclass, not a bare struct: core::MeasureEngine
    // always catches this (see core/measure.hpp), so it never escapes a
    // Measure() call -- but a driver test calling port.rawMeasure() directly
    // is a real and intended thing to do, and "unknown exception" is a poor
    // thing for a test framework to print about a condition this file has a
    // name for.
    //
    class UnmeasurableReading : public std::runtime_error
    {
        public:
            explicit UnmeasurableReading( const std::string_view reason) :
                std::runtime_error( "unmeasurable reading: " + std::string( reason)),
                mReason( reason)
            {}

            //
            // The instrument's own words, unprefixed -- what a log line or a
            // whenUnmeasurable() handler wants, as against what() which is
            // built for an uncaught-exception dump.
            //
            [[nodiscard]]
            auto reason() const -> std::string_view
            {
                return mReason;
            }

        private:
            std::string mReason;
    };

    //
    // The per-measurement setup a port can carry: DMM range, integration
    // time (NPLC), etc -- everything Measure() might want to send to the
    // instrument for one specific reading, on top of "which quantity".
    // Every field is optional: an instrument driver treats std::nullopt as
    // "use whatever's already configured", so a bare Measure(Dmm1.voltage(),
    // at(...)) call still works, no fields required. See core/port.hpp for
    // the surrounding rationale.
    //
    // Structurally symmetric with the sourcing side's builder configs (see
    // hal/dc_source.hpp/hal/ac_source.hpp) -- both are plain aggregates of
    // typed, optional parameters carried by a fluent builder chain, so
    // "how do I set X here" reads the same way whether X is being sourced
    // or sensed.
    //
    // Everything here is instrument configuration -- what to send the meter
    // before it reads. Deliberately nothing about *routing*: whether a reading
    // needs its sense leads wired alongside the force path used to live here as
    // a runtime bool, and is now a template parameter of Port below, because it
    // is a fixed property of the kind of measurement rather than a per-call
    // setting.
    //
    template<typename QuantityT> requires quantities::QuantityType<QuantityT>
    struct MeasureSetup
    {
        //
        // Full-scale range for this measurement. Instruments that only
        // auto-range (or don't have configurable ranges at all) ignore
        // this. For a DMM, "range" is the full-scale value -- .range(20_V)
        // means "the 20V range", not "0..20V".
        //
        std::optional<QuantityT>              Range;

        //
        // NPLC = "number of power-line cycles" to integrate over. Higher
        // NPLC = more precise, slower. Typical DMM values are 0.02, 0.2,
        // 1, 10, 100. int rather than a Quantity, since it's a mode
        // selector, not a physical time.
        //
        std::optional<int>                    Nplc;

        //
        // Frequency, for AC measurements where the meter needs to be told
        // what to look for (some AC DMMs can't autodetect below a few Hz).
        //
        std::optional<quantities::Frequency>  Frequency;

        //
        // Edge-timing reference levels for rise/fall-time measurements, as
        // a fraction of full swing (e.g. 0.1/0.9 for the usual 10%/90%
        // convention) -- SCPI :MEASure:RISetime/:FALLtime's own threshold
        // arguments. Meaningless for anything else, same as Range/Nplc are
        // meaningless off a scope; an instrument that doesn't do edge
        // timing simply never reads these two.
        //
        std::optional<double>  LowThreshold;
        std::optional<double>  HighThreshold;

    };

    //
    // A single physical port on an instrument: a quantity type bound to a
    // specific instrument instance. Returned by an instrument's builder
    // methods (e.g. dmm1.voltage()) rather than constructed directly, so
    // `Measure(dmm1.voltage(), at(Output5V))` both names the port and fixes
    // the return type at the call site -- there is no separate "which
    // quantity did I ask for" argument to get out of sync with the
    // instrument used.
    //
    // Optionally carries a MeasureSetup (range/nplc/etc) that Measure will
    // pass through to the instrument at read time -- e.g.
    // `Dmm1.voltage().range(20_V).nplc(10)`. See MeasureSetup above.
    //
    // Generic over InstrumentT: this header has no idea what a real
    // instrument looks like, only that it has a templated rawMeasure<Q>()
    // (with or without a MeasureSetup argument, dispatched at compile time)
    // and an id() -- hal::L4411A/hal::DSO8064A/etc (see hal/instrument.hpp,
    // hal/l4411a.hpp, hal/dso8064a.hpp) are simply concrete instrument types
    // that happen to satisfy this shape, not something this type depends on.
    //
    // Whether a reading needs its sense leads routed alongside the force path.
    // A named type rather than a bare bool template argument, so
    // Port<Voltage, L4411A, SensePath::Required> says what it means at every
    // spelling and a stray `true` cannot mean something else.
    enum class SensePath
    {
        NotUsed,
        Required
    };

    template<quantities::QuantityType QuantityT, typename InstrumentT, SensePath Sense = SensePath::NotUsed>
    class Port
    {
        public:
            static constexpr SensePath SenseUse = Sense;

            explicit Port( InstrumentT & instrument) : mInstrument( instrument) {}

            //
            // Setup builders -- each returns *this by value-updated so the
            // caller can chain, and each is a no-op unless called. Bare
            // `Dmm1.voltage()` still behaves exactly as before.
            //
            [[nodiscard]]
            auto range( QuantityT value) const -> Port
            {
                auto copy = *this;
                copy.mSetup.Range = value;
                return copy;
            }

            [[nodiscard]]
            auto nplc( int cycles) const -> Port
            {
                auto copy = *this;
                copy.mSetup.Nplc = cycles;
                return copy;
            }

            [[nodiscard]]
            auto frequency( quantities::Frequency f) const -> Port
            {
                auto copy = *this;
                copy.mSetup.Frequency = f;
                return copy;
            }

            [[nodiscard]]
            auto lowThreshold( double fraction) const -> Port
            {
                auto copy = *this;
                copy.mSetup.LowThreshold = fraction;
                return copy;
            }

            [[nodiscard]]
            auto highThreshold( double fraction) const -> Port
            {
                auto copy = *this;
                copy.mSetup.HighThreshold = fraction;
                return copy;
            }

            //
            // What this reading means when the instrument reports it could
            // not be made at all -- see core::UnmeasurableReading above.
            //
            // Not calling this is the common and correct case. An unmeasured
            // reading then comes back as NaN, which is chosen rather than
            // defaulted-to: NaN compares false against every predicate in
            // core/predicates.hpp, so a criterion checked against one fails,
            // reports the instrument's reason beside it, and the run carries
            // on to the next check. That is the behaviour a test rig wants.
            // Throwing out of Measure() would abort the script and lose every
            // later check; substituting a plausible number silently would
            // turn "the scope could not see the edge" into a pass.
            //
            // Calling it says the script has a *specific* meaning for the
            // absence, and the callable is where that meaning is written
            // down:
            //
            //     // No detectable transient is a transient of zero volts.
            //     Measure( Osc1.channel<3>().vmin()
            //                  .whenUnmeasurable( []{ return 0_V; }),
            //              at( dut::Vout));
            //
            //     // ...unless the reason says the scope could not have seen
            //     //    one, which is a different fact and not a zero.
            //     .whenUnmeasurable( []( const std::string_view reason)
            //     {
            //         return reason.contains( "clipped") ? Voltage{ NAN } : 0_V;
            //     })
            //
            // A callable rather than a plain value, because the substitution
            // is a decision and a decision deserves somewhere to put its
            // reasoning. The nullary form above is accepted as well as the
            // reason-taking one -- a handler that does not care why is common
            // enough that making it write an ignored parameter would be
            // noise. A bare value is deliberately NOT accepted: `.whenUn-
            // measurable( 0_V)` reads as "this measured zero", which is the
            // one thing it must never be mistaken for.
            //
            // The old ATE spelling this replaces was an if-block several
            // lines below the measurement, testing a sentinel:
            //
            //     if( ISINVALID( dVOLTMIN)) { dNEGTRANSIENT = 0; }
            //
            // Same decision, made out of sight of the reading it applies to,
            // and silently applied to whatever else happened to be in the
            // variable.
            //
            template<typename FallbackT>
            [[nodiscard]]
            auto whenUnmeasurable( FallbackT fallback) const -> Port
            {
                auto copy = *this;

                if constexpr( std::is_invocable_r_v<QuantityT, FallbackT, std::string_view>)
                {
                    copy.mUnmeasurable = std::move( fallback);
                }
                else
                {
                    static_assert( std::is_invocable_r_v<QuantityT, FallbackT>,
                        "whenUnmeasurable() takes a callable returning this port's quantity, "
                        "either taking the instrument's reason as a std::string_view or taking nothing");

                    copy.mUnmeasurable = [ handler = std::move( fallback)]( std::string_view) { return handler(); };
                }

                return copy;
            }

            //
            // What this port reads as when the instrument said it could not
            // measure -- the handler's answer if one was given, NaN if not.
            // Called by core::MeasureEngine, which is the one place that
            // catches core::UnmeasurableReading; a script never spells this.
            //
            [[nodiscard]]
            auto unmeasurableValue( const std::string_view reason) const -> QuantityT
            {
                if( mUnmeasurable)
                {
                    return mUnmeasurable( reason);
                }

                return QuantityT{ std::numeric_limits<double>::quiet_NaN() };
            }

            //
            // Marks this reading as needing its sense leads routed too -- a
            // 4-wire (Kelvin) measurement, most often. Always "on" when called
            // (there is no argument: a reading either wants its sense path or
            // it doesn't, and not calling this at all is how "doesn't" is
            // spelled), so an instrument's 4-wire builder method -- e.g.
            // hal::L4411A::fourWireResistance() -- chains it on
            // unconditionally, the same way riseTime()/fallTime() chain
            // lowThreshold()/highThreshold().
            //
            // Unlike every other builder here this changes the port's *type*
            // rather than its setup, which is the whole point: whether the
            // sense leads get routed is decided when the code is compiled, not
            // when the reading is taken. core::MeasureEngine branches on it
            // with if constexpr, so a 2-wire reading carries no trace of the
            // 4-wire path at all -- and the requirement is visible in the
            // port's type to anything that wants to reason about it, which a
            // bool buried in a runtime struct never was.
            //
            [[nodiscard]]
            auto requiresSensePath() const -> Port<QuantityT, InstrumentT, SensePath::Required>
            {
                Port<QuantityT, InstrumentT, SensePath::Required> copy{ mInstrument };
                copy.setup( mSetup);
                copy.qualify( mQualifier);
                copy.onUnmeasurable( mUnmeasurable);
                return copy;
            }

            //
            // Names which of an instrument's several possible answers this
            // reading is -- one phase of a three-phase source (see
            // hal::Ac6834B::measuredVoltage), or which of an oscilloscope's
            // fifteen automatic measurements is being taken about one pin (see
            // hal::DSO8064AChannel). Empty for the ordinary case of an
            // instrument that gives one answer about one thing.
            //
            // Both readings and both engines use it for the same purpose: a
            // session keys by name, and two readings that key the same are one
            // recording slot. Which is right for two calls asking the same
            // question and wrong for two calls asking different ones.
            //
            // Set by the instrument's own builder method, not by a script:
            // Measure( AcP1.measuredVoltage( Phase::B)) is the spelling, and
            // the string is this header's way of carrying that downstream
            // without knowing what a phase is. Which is the point -- core has
            // no idea what distinguishes one output from another, only that
            // something does, and that a session keying readbacks by name needs
            // to be told (see core::MeasureEngine's point-free overload, where
            // an unqualified key would make all three phases one recording
            // slot).
            //
            // A string_view rather than an owned string: every caller passes a
            // string literal or the result of a constexpr to_string, both of
            // which outlive the port -- the same assumption AdapterPointTag's
            // Name and Description already make.
            //
            [[nodiscard]]
            auto qualifiedBy( std::string_view name) const -> Port
            {
                auto copy = *this;
                copy.mQualifier = name;
                return copy;
            }

            [[nodiscard]]
            auto qualifier() const -> std::string_view
            {
                return mQualifier;
            }

            // Used only by requiresSensePath() above, to carry an already-built
            // setup and qualifier across the type change.
            auto setup( const MeasureSetup<QuantityT> & setup) -> void
            {
                mSetup = setup;
            }

            auto qualify( const std::string_view name) -> void
            {
                mQualifier = name;
            }

            auto onUnmeasurable( std::function<QuantityT( std::string_view)> fallback) -> void
            {
                mUnmeasurable = std::move( fallback);
            }

            [[nodiscard]]
            auto rawMeasure() const -> QuantityT
            {
                return mInstrument.template rawMeasure<QuantityT>( mSetup);
            }

            //
            // Deduced, not a fixed type: whatever InstrumentT::id() returns
            // (hal::InstrumentId today) is opaque as far as this header is
            // concerned -- it's only ever compared, routed on, or passed to
            // an ADL to_string() by callers that DO know its real type.
            //
            [[nodiscard]]
            auto instrumentId() const
            {
                return mInstrument.id();
            }

            [[nodiscard]]
            auto setup() const -> const MeasureSetup<QuantityT> &
            {
                return mSetup;
            }

        private:
            InstrumentT &            mInstrument;
            MeasureSetup<QuantityT>  mSetup;
            std::string_view         mQualifier;

            //
            // Empty unless whenUnmeasurable() was called -- see it, and
            // unmeasurableValue() above, for what empty means. A
            // std::function rather than a template parameter on Port
            // deliberately: the handler's type would otherwise propagate
            // into every signature a port flows through, and Measure's
            // overloads already carry four template parameters that all
            // mean something to a reader.
            //
            std::function<QuantityT( std::string_view)> mUnmeasurable;
    };
} // namespace core
