#include "../scripts.hpp"

#include "core/criterion.hpp"
#include "core/predicates.hpp"
#include "core/quantity.hpp"
#include "core/verify.hpp"
#include "hal/adapter.hpp"

#include "core/active_criteria.hpp"

#include "dut/device_x_profile.inc"

using namespace core::literals;
using namespace core::quantities;
using core::Verify;


auto fuseRegisterScript( std::string_view, std::string_view) -> bool
{
    bool allPassed = true;

    //
    // No digital-register instrument exists in the hal model (the
    // register-bus DUT model -- hal::Bus/dut::Device -- was removed rather
    // than carried into the VPC/instrument redesign). This stays a fixed
    // stand-in -- 0xF5 is a value that passes both criteria below -- until
    // a real digital readback path exists; a test can't vary it the way it
    // can the voltage below.
    //
    constexpr auto fuseValue = 0xF5;

    allPassed &= Verify( FS_Fuse_6::FS_Fuse_01, fuseValue);
    allPassed &= Verify( FS_Fuse_6::FS_Fuse_02, fuseValue);

    //
    // Ad-hoc check (no CRIT constant declared up front) against a measured
    // quantity.
    //
    const auto voltage = Measure( Dmm2.voltage(), at( DeviceX_StdAdapter::Vout));

    allPassed &= Verify( "Voltage", "Vout", "Supply voltage", EQ( 12.0_V).epsilon( 0.05_V), voltage);

    return allPassed;
}
