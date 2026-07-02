#include <iostream>

#include "dut/device.hpp"
#include "hal/bus.hpp"
#include "scripts/scripts.hpp"

int main() {
    hal::Bus bus;
    dut::Device device(bus);

    bool all_passed = true;

    all_passed &= scripts::power_cycle_script(device);
    all_passed &= scripts::value_script(device);

    std::cout << "\n=== " << (all_passed ? "ALL SCRIPTS PASSED" : "SOME SCRIPTS FAILED")
              << " ===\n";

    return all_passed ? 0 : 1;
}
