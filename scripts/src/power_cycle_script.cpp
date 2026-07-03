#include "scripts/scripts.hpp"

#include "dsl/test_case.hpp"

namespace scripts
{
    auto powerCycleScript( dut::Device & device) -> bool
    {
        dsl::TestCase test_case( "powerCycleScript", device);

        test_case.expectPoweredOff()
                 .powerOn()
                 .expectPoweredOn()
                 .powerOff()
                 .expectPoweredOff();

        return test_case.run();
    }
} // namespace scripts
