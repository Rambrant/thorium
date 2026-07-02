#include "scripts/scripts.hpp"

#include "dsl/test_case.hpp"

namespace scripts {

bool value_script(dut::Device& device) {
    dsl::TestCase test_case("value_script", device);
    test_case.set_value(123).expect_value(123).set_value(0).expect_value(0);
    return test_case.run();
}

}  // namespace scripts
