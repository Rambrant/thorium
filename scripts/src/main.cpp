#include <iostream>

#include "dut/device.hpp"
#include "dut/rig_device.hpp"
#include "hal/bus.hpp"
#include "hal/rig.hpp"
#include "scripts/device_x_profile.hpp"
#include "scripts/scripts.hpp"

using core::quantities::Voltage;

int main()
{
    //
    // --- Register-bus based scripts (existing) ---
    //
    hal::Bus          bus;

    //
    // Simulate fuse + voltage register values that would normally come
    // from real hardware.
    //
    bus.writeRegister( dut::Device::kFuseRegister, 0xF5);
    bus.writeRegister( dut::Device::kVoltageRegister, 12030); // 12.030 V

    bool allPassed = true;

    allPassed &= scripts::fuseRegisterScript( "group", "test");
    allPassed &= scripts::supplyRailScript( "group", "test");

    //
    // --- Composition root for the instrument/matrix path ---
    // This is the one place that chooses the concrete rig and binds the DUT
    // profile (routing table) to it. The RigDevice is handed to scripts as a
    // dut::DeviceView, so nothing above sees the concrete rig or instruments.
    //
    hal::SimRig rig;

    //
    // Program simulated instrument readings per matrix crosspoint (would come
    // from real hardware). Crosspoints match DeviceX_StdAdapter:
    // Port5Vdc -> (3,7), Port3V3 -> (3,6).
    //
    rig.simVoltmeter().setReadingAt({ 3, 7 }, Voltage{ 5.02 });
    rig.simVoltmeter().setReadingAt({ 3, 6 }, Voltage{ 3.29 });

    dut::RigDevice rigDevice{ rig, DeviceX_StdAdapter };

    allPassed &= scripts::supplyRailScript( "group", "test");

    std::cout << "\n=== " << (allPassed ? "ALL SCRIPTS PASSED" : "SOME SCRIPTS FAILED") << " ===\n";

    return allPassed ? 0 : 1;
}
