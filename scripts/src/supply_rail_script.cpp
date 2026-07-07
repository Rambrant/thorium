#include "scripts/scripts.hpp"

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/verify.hpp"

//
// Requirements / test-plan values live here, tied to the spec. Each CRIT is
// a named, traceable expected value; the test-point strings name where to
// measure, matching the spec's language ("5Vdc port", etc.).
//
GROUP( FS_Supply_1, "Supply rail voltage checks")
    CRIT( FS_Supply_5V0, core::EQ( 5.0).within( 0.05), "5Vdc port within +/-50mV")
    CRIT( FS_Supply_3V3, core::EQ( 3.3).within( 0.05), "3.3Vdc port within +/-50mV")
END_GROUP

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