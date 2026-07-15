#include "../scripts.hpp"

#include "core/verify.hpp"

#include "core/active_criteria.hpp"

auto supplyRailScript(  std::string_view, std::string_view) -> bool
{
    bool allPassed = true;

    //
    // Reads like the test spec: check this requirement at this port. No
    // instrument, switch path, or rig detail is visible here.
    //
    const auto dc5Voltage = Measure( Dmm1.voltage(), "5VOutput");
    const auto dc3Voltage = Measure( Dmm1.voltage(), "3V3Output");

    allPassed &= core::Verify( FS_Supply_1::FS_Supply_5V0, dc5Voltage);
    allPassed &= core::Verify( FS_Supply_1::FS_Supply_3V3, dc3Voltage);

    return allPassed;
}
