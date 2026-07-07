//
// Created by Thomas Rambrant on 2026-07-05.
//
#include "core/measure.hpp"

namespace core
{
    namespace
    {
        //
        // Clear the matrix before closing the intended path, so only the
        // requested connection is ever live.
        //
        auto route( ISwitchMatrix & matrix, Crosspoint at ) -> void
        {
            matrix.openAll();
            matrix.close( at);
        }
    } // namespace

    auto Measure( ISwitchMatrix & matrix, Crosspoint at, IVoltmeter & instrument ) -> Voltage
    {
        route( matrix, at);

        return instrument.measureVoltage();
    }

    auto Measure( ISwitchMatrix & matrix, Crosspoint at, IOscilloscope & instrument ) -> Voltage
    {
        route( matrix, at);

        return instrument.measureLevel();
    }
} // namespace core