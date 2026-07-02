#include "scripts/scripts.hpp"

#include "dsl/test_case.hpp"

namespace scripts {

bool power_cycle_script(dut::Device& device) {
    dsl::TestCase test_case("power_cycle_script", device);
    test_case.expect_powered_off()
        .power_on()
        .expect_powered_on()
        .power_off()
        .expect_powered_off();
    return test_case.run();
}

}  // namespace scripts
