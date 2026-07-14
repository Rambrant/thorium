#include "../scripts.hpp"

#include "core/verify.hpp"

#include "core/active_criteria.hpp"

auto supplyRailScript(  std::string_view, std::string_view) -> bool
{
    bool allPassed = true;

    //
    // Reads like the test spec: check this requirement at this port. No
    // instrument, crosspoint, or rig detail is visible here.
    //
    // Measure (...)
    constexpr auto dc5Voltage = 5.0_V;
    constexpr auto dc3Voltage = 3.3_V;

    allPassed &= core::Verify( FS_Supply_1::FS_Supply_5V0, dc5Voltage);
    allPassed &= core::Verify( FS_Supply_1::FS_Supply_3V3, dc3Voltage);

    return allPassed;
}