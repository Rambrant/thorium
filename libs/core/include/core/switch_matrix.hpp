#pragma once

#include <cstdint>

namespace core
{
    //
    // A crosspoint coordinate in the rig's switch matrix. The *values* that are
    // valid (which rows/cols physically exist) are a property of the concrete
    // rig and are defined in hal; this struct is just the abstract coordinate
    // type so core/dut can name a crosspoint without knowing the hardware.
    //
    struct Crosspoint
    {
        std::uint16_t mRow;
        std::uint16_t mCol;

        friend constexpr auto operator==( Crosspoint, Crosspoint) -> bool = default;
    };

    //
    // Abstract switch matrix sitting in the signal chain. dut commands it to
    // route an instrument port to a DUT-side line by closing a crosspoint.
    // Concrete implementation (and which crosspoints are legal) lives in hal.
    //
    class ISwitchMatrix
    {
        public:
            virtual ~ISwitchMatrix() = default;

            // Close a single crosspoint, connecting its row to its column.
            virtual auto close( Crosspoint point) -> void = 0;

            // Open a single crosspoint.
            virtual auto open( Crosspoint point) -> void = 0;

            // Open every crosspoint, leaving the matrix fully disconnected.
            virtual auto openAll() -> void = 0;

            // Whether a given crosspoint is currently closed.
            [[nodiscard]]
            virtual auto isClosed( Crosspoint point) const -> bool = 0;
    };
} // namespace core
