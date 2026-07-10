#include "scripts/scripts.hpp"

#include "core/verify.hpp"

#include "core/active_criteria.hpp"

namespace scripts
{
    auto supplyRailScript( dut::DeviceView & device) -> bool
    {
        bool allPassed = true;

        //
        // Reads like the test spec: check this requirement at this port. No
        // instrument, crosspoint, or rig detail is visible here.
        //
        allPassed &= core::Verify( FS_Supply_1::FS_Supply_5V0, device.measure( "Port5Vdc"));
        allPassed &= core::Verify( FS_Supply_1::FS_Supply_3V3, device.measure( "Port3V3"));

        return allPassed;
    }
} // namespace scripts