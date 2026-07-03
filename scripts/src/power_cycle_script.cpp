#include "scripts/scripts.hpp"

#include "dsl/test_case.hpp"

namespace scripts {

bool power_cycle_script(dut::Device& device) {
    dsl::TestCase test_case("power_cycle_script", device);
    test_case.expectPoweredOff()
        .powerOn()
        .expectPoweredOn()
        .powerOff()
        .expectPoweredOff();
    return test_case.run();
}

}  // namespace scripts
