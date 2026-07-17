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
    template<quantities::QuantityType QuantityT>
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
    // and an id() -- hal::L4411A/hal::Oscilloscope/etc (see hal/instrument.hpp,
    // hal/l4411a.hpp) are simply concrete instrument types that happen to
    // satisfy this shape, not something this type depends on.
    //
    template<quantities::QuantityType QuantityT, typename InstrumentT>
    class Port
    {
        public:
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
