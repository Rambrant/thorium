#include "scripts/scripts.hpp"

#include "dsl/test_case.hpp"

namespace scripts {

bool value_script(dut::Device& device) {
    dsl::TestCase test_case("value_script", device);
    test_case.setValue(123).expectValue(123).setValue(0).expectValue(0);
    return test_case.run();
}

}  // namespace scripts
