#pragma once

#include "core/instrument.hpp"
#include "core/switch_matrix.hpp"

namespace core
{
    //
    // Abstract view of a test rig: a collection of instruments plus the switch
    // matrix that routes them. dut acquires instruments from an IRig by
    // capability (an oscilloscope, a voltmeter, ...) and receives them as the
    // core interfaces above -- it never sees a concrete hal type. Defined in
    // core (not hal) so dut can depend on the abstraction; hal provides the
    // concrete Rig that implements this.
    //
    class IRig
    {
        public:
            virtual ~IRig() = default;

            // Capability accessors. A rig without a given instrument returns nullptr,
            // letting the DUT fail loudly if it needs something the rig can't offer.
            [[nodiscard]]
            virtual IOscilloscope * oscilloscope() = 0;

            [[nodiscard]]
            virtual IVoltmeter * voltmeter() = 0;

            [[nodiscard]]
            virtual IPowerSupply * powerSupply() = 0;

            // The switch matrix in the signal chain.
            [[nodiscard]]
            virtual ISwitchMatrix & matrix() = 0;
    };
} // namespace core
