#include "scripts/scripts.hpp"

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/quantity.hpp"
#include "core/verify.hpp"

#include "core/active_criteria.hpp"

using namespace core::literals;
using core::Verify;


namespace scripts
{
    auto fuseRegisterScript( const dut::Device & device) -> bool
    {
        bool allPassed = true;

        const auto fuseValue = device.readFuseRegister();

        allPassed &= Verify( FS_Fuse_6::FS_Fuse_01, fuseValue);
        allPassed &= Verify( FS_Fuse_6::FS_Fuse_02, fuseValue);

        //
        // Ad-hoc check (no CRIT constant declared up front) against a measured
        // quantity. Note we compare against .value() (a raw double) rather than
        // a core::Voltage directly -- EqPredicate's tolerance logic operates on
        // primitive floating-point types, so quantities get unwrapped at the
        // point they're checked.
        //
        const auto voltage = device.measureOutputVoltage();

        allPassed &= core::Verify( "Voltage", "Vout", "Supply voltage", core::EQ( 12.0).epsilon( 0.05), voltage.value());

        return allPassed;
    }
} // namespace scripts