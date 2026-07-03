#include "scripts/scripts.hpp"

#include "dsl/test_case.hpp"

namespace scripts
{
    auto valueScript( dut::Device & device) -> bool
    {
        dsl::TestCase testCase( "valueScript", device);

        testCase.setValue( 123).expectValue( 123).setValue( 0).expectValue( 0);

        return testCase.run();
    }
} // namespace scripts
