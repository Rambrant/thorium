#include "scripts/scripts.hpp"

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/quantity.hpp"
#include "dsl/verify.hpp"

using namespace core::literals;

//
// A criteria table, declared once at file scope and reused across runs.
// This mirrors the original prototype's GROUP/CRIT usage, now against real
// register addresses on dut::Device.
//
GROUP( FS_Fuse_6, "Check of Fuses @ Register CB30")
    CRIT( FS_Fuse_01, core::BAND( 0x0Fu, 0x05u), "Low nibble of fuse register must be 0x5")
    CRIT( FS_Fuse_02, core::EQ( 0xF5u), "Fuse register must read exactly 0xF5")
END_GROUP

namespace scripts
{
    auto fuseRegisterScript( const dut::Device & device) -> bool
    {
        bool allPassed = true;

        const auto fuseValue = device.readFuseRegister();

        allPassed &= dsl::Verify(FS_Fuse_6::FS_Fuse_01, fuseValue);
        allPassed &= dsl::Verify(FS_Fuse_6::FS_Fuse_02, fuseValue);

        //
        // Ad-hoc check (no CRIT constant declared up front) against a measured
        // quantity. Note we compare against .value() (a raw double) rather than
        // a core::Voltage directly -- EqPredicate's tolerance logic operates on
        // primitive floating-point types, so quantities get unwrapped at the
        // point they're checked.
        //
        const auto voltage = device.measureOutputVoltage();

        allPassed &= dsl::Verify( "Voltage", "Vout", "Supply voltage", core::EQ( 12.0).within( 0.05), voltage.value());

        return allPassed;
    }
} // namespace scripts
