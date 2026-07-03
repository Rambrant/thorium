#include "scripts/scripts.hpp"

#include "dsl/criterion.hpp"
#include "dsl/measure.hpp"
#include "dsl/predicates.hpp"

// Requirements / test-plan values live here, tied to the spec. Each CRIT is
// a named, traceable expected value; the test-point strings name where to
// measure, matching the spec's language ("5Vdc port", etc.).
GROUP(FS_Supply_1, "Supply rail voltage checks")

    CRIT(FS_Supply_5V0,  dsl::EQ(5.0).within(0.05),  "5Vdc port within +/-50mV")
    CRIT(FS_Supply_3V3,  dsl::EQ(3.3).within(0.05),  "3.3Vdc port within +/-50mV")

END_GROUP

namespace scripts {

bool supply_rail_script(dut::DeviceView& device)
{
    bool all_passed = true;

    // Reads like the test spec: check this requirement at this port. No
    // instrument, crosspoint, or rig detail is visible here.
    all_passed &= dsl::Measure(device, FS_Supply_1::FS_Supply_5V0, "Port5Vdc");
    all_passed &= dsl::Measure(device, FS_Supply_1::FS_Supply_3V3, "Port3V3");

    return all_passed;
}

}  // namespace scripts
