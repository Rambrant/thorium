#include "scripts/scripts.hpp"

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/quantity.hpp"
#include "core/verify.hpp"

#include "core/active_criteria.hpp"

using namespace core::literals;
using namespace core::quantities;
using core::Verify;


namespace scripts
{
    auto fuseRegisterScript( std::string_view, std::string_view) -> bool
    {
        bool allPassed = true;

//      Measure( device.readFuseRegister());
        constexpr auto fuseValue = 0x05;

        allPassed &= Verify( FS_Fuse_6::FS_Fuse_01, fuseValue);
        allPassed &= Verify( FS_Fuse_6::FS_Fuse_02, fuseValue);

        //
        // Ad-hoc check (no CRIT constant declared up front) against a measured
        // quantity. Note we compare against .value() (a raw double) rather than
        // a core::Voltage directly -- EqPredicate's tolerance logic operates on
        // primitive floating-point types, so quantities get unwrapped at the
        // point they're checked.
        //
//      MEASURE( ... device.measureOutputVoltage());

        constexpr auto voltage = 12.01_V;

        allPassed &= Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0_V).epsilon( 0.05_V), voltage);

        return allPassed;
    }
} // namespace scripts