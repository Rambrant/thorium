#include "../prelude.hpp"

auto supplyRailScript() -> void
{
    //
    // Reads like the test spec: check this requirement at this port. No
    // instrument, switch path, or rig detail is visible here. dut::Output5V
    // etc are compile-time-checked -- a typo here is "no such member", not
    // a runtime error.
    //
    const auto dc5Voltage = Measure( Dmm1.voltage(), at( dut::Output5V));
    const auto dc3Voltage = Measure( Dmm1.voltage(), at( dut::Output3V3));

    Verify( FS_Supply_1::FS_Supply_5V0, dc5Voltage);
    Verify( FS_Supply_1::FS_Supply_3V3, dc3Voltage);
}
