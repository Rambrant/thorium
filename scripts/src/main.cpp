#include <iostream>

#include "dut/device.hpp"
#include "dut/rig_device.hpp"
#include "hal/bus.hpp"
#include "hal/rig.hpp"
#include "scripts/device_x_profile.hpp"
#include "scripts/scripts.hpp"

int main() {
    // --- Register-bus based scripts (existing) ---
    hal::Bus bus;
    dut::Device device(bus);

    // Simulate fuse + voltage register values that would normally come
    // from real hardware.
    bus.write_register(dut::Device::kFuseRegister, 0xF5);
    bus.write_register(dut::Device::kVoltageRegister, 12030);  // 12.030 V

    bool all_passed = true;

    all_passed &= scripts::power_cycle_script(device);
    all_passed &= scripts::value_script(device);
    all_passed &= scripts::fuse_register_script(device);

    // --- Composition root for the instrument/matrix path ---
    // This is the one place that chooses the concrete rig and binds the DUT
    // profile (routing table) to it. The RigDevice is handed to scripts as a
    // dut::DeviceView, so nothing above sees the concrete rig or instruments.
    hal::SimRig rig;
    // Program simulated instrument readings per matrix crosspoint (would come
    // from real hardware). Crosspoints match DeviceX_StdAdapter:
    // Port5Vdc -> (3,7), Port3V3 -> (3,6).
    rig.sim_voltmeter().set_reading_at({3, 7}, core::Voltage{5.02});
    rig.sim_voltmeter().set_reading_at({3, 6}, core::Voltage{3.29});
    dut::RigDevice rig_device{rig, DeviceX_StdAdapter};

    all_passed &= scripts::supply_rail_script(rig_device);

    std::cout << "\n=== " << (all_passed ? "ALL SCRIPTS PASSED" : "SOME SCRIPTS FAILED")
              << " ===\n";

    return all_passed ? 0 : 1;
}
