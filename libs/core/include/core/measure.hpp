#pragma once

#include "quantity.hpp"
#include "core/instrument.hpp"
#include "core/switch_matrix.hpp"

namespace core
{
    //
    // Measure is a pure value-fetching primitive: route the matrix to the given
    // crosspoint, then take one reading from the given instrument. It never
    // judges the result -- that is Verify's job:
    //
    //   core::Verify( criterion, core::Measure( matrix, at, voltmeter));
    //
    // Overloaded per instrument capability (rather than an instrument-kind enum)
    // so the right read method is selected at compile time. Callers already
    // hold a live instrument reference (obtained from an IRig), so there is no
    // "instrument missing" case to report here -- that failure belongs to
    // whatever resolves a name to a crosspoint + instrument, one layer up.
    //
    [[nodiscard]]
    auto Measure( ISwitchMatrix & matrix, Crosspoint at, IVoltmeter & instrument ) -> Voltage;

    [[nodiscard]]
    auto Measure( ISwitchMatrix & matrix, Crosspoint at, IOscilloscope & instrument ) -> Voltage;
} // namespace core