#include "../scripts.hpp"

#include "core/verify.hpp"

#include "core/active_criteria.hpp"

auto supplyRailScript(  std::string_view, std::string_view) -> bool
{
    bool allPassed = true;

    //
    // Reads like the test spec: check this requirement at this port. The
    // instrument, switch path, and rig detail live in suite/rig.hpp, not
    // here.
    //
    const auto dc5Voltage = dut::Measure( rig::bench(), rig::dmm1().voltage(), "5VOutput");
    const auto dc3Voltage = dut::Measure( rig::bench(), rig::dmm1().voltage(), "3V3Output");

    allPassed &= core::Verify( FS_Supply_1::FS_Supply_5V0, dc5Voltage);
    allPassed &= core::Verify( FS_Supply_1::FS_Supply_3V3, dc3Voltage);

    return allPassed;
}
