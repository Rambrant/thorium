#pragma once

#include <optional>

#include "core/quantity.hpp"

namespace core
{
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
    // and an id() -- hal::L4411A/hal::DSO8064/etc (see hal/instrument.hpp,
    // hal/l4411a.hpp, hal/dso8064.hpp) are simply concrete instrument types
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
                return copy;
            }

            // Used only by requiresSensePath() above, to carry an already-built
            // setup across the type change.
            auto setup( const MeasureSetup<QuantityT> & setup) -> void
            {
                mSetup = setup;
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
    };
} // namespace core
