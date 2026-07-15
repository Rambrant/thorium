#pragma once

#include "core/quantity.hpp"

namespace core
{
    //
    // A single physical port on an instrument: a quantity type bound to a
    // specific instrument instance. Returned by an instrument's builder
    // methods (e.g. dmm1.voltage()) rather than constructed directly, so
    // `Measure(dmm1.voltage(), "5VOutput")` both names the port and fixes the
    // return type at the call site -- there is no separate "which quantity
    // did I ask for" argument to get out of sync with the instrument used.
    //
    // Generic over InstrumentT: this header has no idea what a real
    // instrument looks like, only that it has a templated rawMeasure<Q>()
    // and an id() -- hal::Dmm/hal::Oscilloscope/etc (see hal/instrument.hpp)
    // are simply concrete instrument types that happen to satisfy this
    // shape, not something this type depends on.
    //
    template<quantities::QuantityType QuantityT, typename InstrumentT>
    class Port
    {
        public:
            explicit Port( InstrumentT & instrument) : mInstrument( instrument) {}

            [[nodiscard]]
            auto rawMeasure() const -> QuantityT
            {
                return mInstrument.template rawMeasure<QuantityT>();
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

        private:
            InstrumentT & mInstrument;
    };
} // namespace core
